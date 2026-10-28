#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include <new>
#include <type_traits>

#ifdef ESP_PLATFORM
#include <esp_attr.h>
// The loops that run for every band, kept in internal RAM on the device
// rather than fetched from flash through its small cache
#define HOT IRAM_ATTR
#else
#define HOT
#endif

// Drawing on the round panel's 466x466 frame: big-endian RGB565, the order
// the CO5300 takes, blended in integers with 5-bit alpha. A table gives each
// pixel its distance and angle from the centre, so rings, ticks and tails are
// worked out per pixel without a square root.
//
// A frame is drawn in two passes. Recording runs the screens once: each
// drawing call works out what it can for the whole frame (layout, tables,
// positions) and keeps that. Playback then draws the kept calls once per
// band of rows, touching only that band's pixels. Playback only reads what
// recording left, so two cores can draw bands of one frame at once.
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
// floor and ceil without a call into libm, which on the ESP32 costs more than
// the arithmetic around it
inline int ifloor(float v) {
  int i = int(v);
  return i - (v < float(i));
}
inline int iceil(float v) {
  int i = int(v);
  return i + (v > float(i));
}

// sin and cos of a number of turns, without a call into libm: reduced to a
// quarter turn round the nearest axis, then polynomials good to 1e-6
inline void sincosTurns(float turns, float &s, float &c) {
  float q = (turns - float(ifloor(turns))) * 4;  // quarter turns, 0..4
  int k = int(q + 0.5f);
  float x = (q - k) * (TAU / 4), x2 = x * x;
  float sn = x * (1 + x2 * (-1 / 6.f + x2 * (1 / 120.f + x2 * (-1 / 5040.f))));
  float cs = 1 + x2 * (-0.5f + x2 * (1 / 24.f + x2 * (-1 / 720.f + x2 * (1 / 40320.f))));
  switch (k & 3) {
    case 0: s = sn, c = cs; break;
    case 1: s = cs, c = -sn; break;
    case 2: s = -sn, c = -cs; break;
    default: s = -cs, c = sn; break;
  }
}

// Large tables go to PSRAM on the device; small hot ones to internal RAM
void *bigAlloc(size_t n);
void *fastAlloc(size_t n);

bool init();

// Where playback draws, and the rows it may touch: each core its own
extern __thread uint16_t *fb;
extern __thread int clipY0, clipY1;
extern uint32_t *pol;         // per pixel: distance from the centre in 1/16 px, and angle << 16
extern int16_t inside[H][2];  // each row's columns inside the round, [x0, x1)

// ---------- Recording and playback ----------

// While recording, the drawing calls below keep themselves for playback
// instead of drawing
extern bool recording;
// true starts a frame's list, empty; false ends it, ready to play
void record(bool on);
// Plays the list for rows [y0, y1) into band, whose first row stands for
// row y0. With spent, spent[tag] gains the ticks each tag's calls took.
void play(uint16_t *band, int y0, int y1, uint32_t *spent = nullptr);
// The tag the calls kept from here on are timed under
constexpr int TAGS = 16;
void tag(int t);
// Room in the frame's list for what a call works out once and plays back
// many times; null when the list is full
void *scratch(size_t n);
// Runs fn(0, arg) and fn(1, arg): on two cores at once where the platform
// has them, else one after the other. For work while recording that splits
// in halves sharing nothing.
extern void (*split)(void (*fn)(int half, void *arg), void *arg);
// The list's high-water mark in bytes, for the logs
size_t listUsed();
// A steady count for timings, and how many of it make a millisecond
uint32_t ticks();
float ticksPerMs();

struct Kept {
  void (*run)(const void *);
  int16_t y0, y1;  // the rows it can touch
  uint16_t size;   // bytes to the next one, this header included
  uint8_t tag;
};
Kept *keepRoom(size_t size, float y0, float y1);

// Keeps f, to be called back during playback for each band meeting rows
// [y0, y1]. f holds plain values: what it points at must last the frame.
template <typename Fn>
void keep(float y0, float y1, const Fn &f) {
  static_assert(std::is_trivially_destructible<Fn>::value && alignof(Fn) <= alignof(void *), "a kept call holds plain values");
  Kept *k = keepRoom(sizeof(Kept) + sizeof(Fn), y0, y1);
  if (!k) return;
  k->run = [](const void *p) { (*(const Fn *)p)(); };
  new (k + 1) Fn(f);
}

// ---------- Pixels ----------

inline int dist16(uint32_t p) { return int(p & 0xFFFF); }
// Turns (any number) to the table's angle: 65536 to the turn, clockwise from 12 o'clock
inline uint32_t angle16(float turns) { return uint32_t(ifloor((turns - float(ifloor(turns))) * 65536)) & 0xFFFF; }

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

// Blends colour c over pixels [i0, i1) at one alpha a in 0..32
void blendRun(int i0, int i1, uint16_t c, uint32_t a);

// Adds spread colours, each channel saturating: a channel that overflows
// sets the bit just above it, and that bit fills the channel back to its top
// (o - (o >> 5) fills blue's and red's five bits; green's six need one more)
inline uint32_t addSpread(uint32_t p, uint32_t c) {
  uint32_t s = p + c, o = s & 0x08010020;
  return (s | (o - (o >> 5) + (o >> 6 & 0x200000))) & 0x07E0F81F;
}

// Adds colour c at strength a in 0..32, saturating
inline void add(int i, uint16_t c, uint32_t a) {
  if (!a) return;
  uint32_t r = addSpread(spread(swap(fb[i])), spread(c) * (a > 32 ? 32 : a) >> 5 & 0x07E0F81F);
  fb[i] = swap(uint16_t(r | r >> 16));
}

// Calls f(index) for every pixel between radii r0 and r1 of the centre
template <typename F>
void annulus(float r0, float r1, F f) {
  if (r0 < 0) r0 = 0;
  int y0 = ifloor(CY - r1), y1 = iceil(CY + r1);
  if (y0 < clipY0) y0 = clipY0;
  if (y1 > clipY1 - 1) y1 = clipY1 - 1;
  for (int y = y0; y <= y1; y++) {
    float dy = y + 0.5f - CY;
    float o2 = r1 * r1 - dy * dy;
    if (o2 <= 0) continue;
    float xo = sqrtf(o2), i2 = r0 * r0 - dy * dy, xi = i2 > 0 ? sqrtf(i2) : 0;
    int a0 = ifloor(CX - xo - 1), a1 = iceil(CX - xi);
    int b0 = ifloor(CX + xi - 1), b1 = iceil(CX + xo);
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
  int x0 = ifloor(x - r), x1 = iceil(x + r), y0 = ifloor(y - r), y1 = iceil(y + r);
  if (x0 < 0) x0 = 0;
  if (y0 < clipY0) y0 = clipY0;
  if (x1 > W - 1) x1 = W - 1;
  if (y1 > clipY1 - 1) y1 = clipY1 - 1;
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
  if (y0 < clipY0) y0 = clipY0;
  if (y1 > clipY1) y1 = clipY1;
  for (int y = y0; y < y1; y++)
    if (inside[y][1] > inside[y][0]) f(y * W, inside[y][0], inside[y][1]);
}

// ---------- Drawing calls, kept while recording ----------

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
// A soft light round (x, y); none within `hole` of it, where something
// opaque will cover it anyway
void glowAt(float x, float y, float r, uint16_t c, float alpha, float hole = 0);
void roundRect(float x0, float y0, float x1, float y1, float radius, uint16_t c, float alpha = 1);
// Light added in colour c that rises from nothing at radius r0 to `alpha` at r1
void vignette(float r0, float r1, uint16_t c, float alpha);

}  // namespace gfx
