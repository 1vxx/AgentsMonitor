#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Drawing on the round panel's 466x466 frame: big-endian RGB565, the order
// the CO5300 takes, blended in integers with 5-bit alpha. A table gives each
// pixel its distance and angle from the centre, so rings, ticks and tails are
// worked out per pixel without a square root.
namespace gfx {

constexpr int W = 466, H = 466;
constexpr float CX = W / 2.f, CY = H / 2.f;
constexpr float TAU = 6.28318531f;

struct RGB {
  float r, g, b;
};

constexpr RGB hex(uint32_t v) { return {float(v >> 16 & 255), float(v >> 8 & 255), float(v & 255)}; }
inline RGB mix(RGB a, RGB b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
inline RGB scale(RGB a, float k) { return {a.r * k, a.g * k, a.b * k}; }

inline int clamp8(float v) { return v <= 0 ? 0 : v >= 255 ? 255 : int(v); }
inline uint16_t c565(RGB c) {
  return uint16_t((clamp8(c.r) & 0xF8) << 8 | (clamp8(c.g) & 0xFC) << 3 | clamp8(c.b) >> 3);
}
inline uint16_t swap(uint16_t v) { return __builtin_bswap16(v); }
inline float clamp01(float v) { return v <= 0 ? 0 : v >= 1 ? 1 : v; }

// Large tables go to PSRAM on the device; small hot ones to internal RAM
void *bigAlloc(size_t n);
void *fastAlloc(size_t n);

bool init();
// Where the next drawing goes
void target(uint16_t *frame);

extern uint16_t *fb;
extern uint32_t *pol;  // per pixel: distance from the centre in 1/16 px, and angle << 16

inline int dist16(uint32_t p) { return int(p & 0xFFFF); }
// Turns (any number) to the table's angle: 65536 to the turn, clockwise from 12 o'clock
inline uint32_t angle16(float turns) { return uint32_t(int32_t(floorf((turns - floorf(turns)) * 65536))) & 0xFFFF; }

// RGB565 with green, red and blue spread apart so one multiply scales all three
inline uint32_t spread(uint32_t c) { return (c | c << 16) & 0x07E0F81F; }

// Blends colour c (native RGB565) over pixel i with alpha a in 0..32
inline void blend(int i, uint16_t c, uint32_t a) {
  if (a >= 32) {
    fb[i] = swap(c);
    return;
  }
  if (!a) return;
  uint32_t r = (spread(c) * a + spread(swap(fb[i])) * (32 - a)) >> 5 & 0x07E0F81F;
  fb[i] = swap(uint16_t(r | r >> 16));
}

// Adds colour c at strength a in 0..32, saturating
inline void add(int i, uint16_t c, uint32_t a) {
  uint32_t p = swap(fb[i]);
  uint32_t r = (p >> 11) + ((c >> 11) * a >> 5), g = (p >> 5 & 63) + ((c >> 5 & 63) * a >> 5), b = (p & 31) + ((c & 31) * a >> 5);
  fb[i] = swap(uint16_t((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b)));
}

// Calls f(index) for every pixel between radii r0 and r1 of the centre
template <typename F>
void annulus(float r0, float r1, F f) {
  if (r0 < 0) r0 = 0;
  int y0 = int(floorf(CY - r1)), y1 = int(ceilf(CY + r1));
  if (y0 < 0) y0 = 0;
  if (y1 > H - 1) y1 = H - 1;
  for (int y = y0; y <= y1; y++) {
    float dy = y + 0.5f - CY;
    float o2 = r1 * r1 - dy * dy;
    if (o2 <= 0) continue;
    float xo = sqrtf(o2), i2 = r0 * r0 - dy * dy, xi = i2 > 0 ? sqrtf(i2) : 0;
    int a0 = int(floorf(CX - xo - 1)), a1 = int(ceilf(CX - xi));
    int b0 = int(floorf(CX + xi - 1)), b1 = int(ceilf(CX + xo));
    if (a0 < 0) a0 = 0;
    if (b1 > W - 1) b1 = W - 1;
    if (a1 >= b0) a1 = b1, b0 = b1 + 1;
    int row = y * W;
    for (int x = a0; x <= a1; x++) f(row + x);
    for (int x = b0; x <= b1; x++) f(row + x);
  }
}

// Calls f(index, squared distance) for every pixel within r of (x, y)
template <typename F>
void around(float x, float y, float r, F f) {
  int x0 = int(floorf(x - r)), x1 = int(ceilf(x + r)), y0 = int(floorf(y - r)), y1 = int(ceilf(y + r));
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > W - 1) x1 = W - 1;
  if (y1 > H - 1) y1 = H - 1;
  float r2 = r * r;
  for (int py = y0; py <= y1; py++) {
    float dy = py + 0.5f - y;
    for (int px = x0; px <= x1; px++) {
      float dx = px + 0.5f - x, d2 = dx * dx + dy * dy;
      if (d2 < r2) f(py * W + px, d2);
    }
  }
}

// Calls f(row start index, x0, x1) for each row of the round panel between y0 and y1
template <typename F>
void rows(int y0, int y1, F f) {
  if (y0 < 0) y0 = 0;
  if (y1 > H) y1 = H;
  for (int y = y0; y < y1; y++) {
    float dy = y + 0.5f - CY, span = CX * CX - dy * dy;
    if (span <= 0) continue;
    int half = int(sqrtf(span)) + 1, x0 = int(CX) - half, x1 = int(CX) + half;
    f(y * W, x0 < 0 ? 0 : x0, x1 > W ? W : x1);
  }
}

// Paints the round panel black
void clear();
// Scales everything on the panel by k, for fades
void dim(float k);

// A ring round the centre, optionally dashed with `dashes` dashes of `on` px turned by `rot` turns
void ring(float r, float w, uint16_t c, float alpha, int dashes = 0, float on = 0, float rot = 0);
// Part of a ring round the centre from `from` to `to` turns, fading from `a0` to `a1` along it
void arc(float r, float w, float from, float to, uint16_t c, float a0, float a1);
void ringAt(float x, float y, float r, float w, uint16_t c, float alpha);
void discAt(float x, float y, float r, uint16_t c, float alpha = 1);
void glowAt(float x, float y, float r, uint16_t c, float alpha);
void roundRect(float x0, float y0, float x1, float y1, float radius, uint16_t c, float alpha = 1);

}  // namespace gfx
