#ifndef __clang__
#pragma GCC optimize("O2")
#endif

#include "halo.h"

#include <string.h>

namespace halo {
namespace {

using gfx::RGB;
using gfx::hex;

// --m1, --m2, --m3 in the dashboard's dark theme
const Palette PALETTES[] = {
    {hex(0x3d4160), hex(0x5b6082), hex(0x2a2d45)},  // offline
    {hex(0x5a6bb8), hex(0xa9b6ec), hex(0x3a3f86)},  // idle
    {hex(0xd6493f), hex(0xf5b2a4), hex(0x9e2e5a)},  // error
    {hex(0xe9774f), hex(0xf6cdb0), hex(0xc25f86)},  // working
    {hex(0xf0aa2e), hex(0xfbe2a4), hex(0xe2683a)},  // attention
};

const RGB TEXT = hex(0xeceaf6);

// The home orb's two outer shadows, as the box-shadows in live.css: a halo
// all round and a warmer one below. Smooth enough to keep at half resolution.
constexpr int GW = gfx::W / 2;
uint8_t *glowMap;

// The orb's colour field, one cell per 2x2 pixels
constexpr int CELLS = 100;
uint16_t cells[CELLS * CELLS];

inline float smooth(float v) { return v * v * (3 - 2 * v); }
inline float easeInOut(float t) { return 0.5f - 0.5f * cosf(t * gfx::TAU / 2); }
// Close to the normal CDF; enough for blurred edges
inline float phi(float z) { return smooth(gfx::clamp01(z / 4.4f + 0.5f)); }

inline RGB rgb(uint16_t c) {
  int r = c >> 11, g = c >> 5 & 63, b = c & 31;
  return {float(r << 3 | r >> 2), float(g << 2 | g >> 4), float(b << 3 | b >> 2)};
}

// Three blurred blobs in the mood's colours drifting inside a sphere, as in
// Orb.tsx; positions and radii in orb radii, keyframes at 50% and 100%
struct Blob {
  float x, y, r;
  float k[2][3];  // dx, dy, scale
  int color;      // which of m1, m2, m3
};
const Blob BLOBS[] = {
    {-0.58f, -0.50f, 0.78f, {{0.4056f, 0.1248f, 1.1f}, {0.156f, 0.468f, 0.95f}}, 0},
    {0.70f, 0.70f, 0.74f, {{-0.444f, -0.0888f, 0.92f}, {-0.1184f, -0.444f, 1.08f}}, 2},
    {0.10f, -0.06f, 0.50f, {{-0.30f, 0.20f, 1.2f}, {0.20f, -0.24f, 0.85f}}, 1},
};

}  // namespace

Palette palette(model::Mood mood) { return PALETTES[int(mood)]; }

Palette mix(const Palette &a, const Palette &b, float t) {
  return {gfx::mix(a.m1, b.m1, t), gfx::mix(a.m2, b.m2, t), gfx::mix(a.m3, b.m3, t)};
}

bool init() {
  glowMap = (uint8_t *)gfx::fastAlloc(GW * GW);
  if (!glowMap) return false;
  for (int y = 0; y < GW; y++)
    for (int x = 0; x < GW; x++) {
      float dx = 2 * x + 1 - OX, dy = 2 * y + 1 - OY;
      float d1 = sqrtf(dx * dx + dy * dy);
      float d2 = sqrtf(dx * dx + (dy - 0.36f * ORB_R) * (dy - 0.36f * ORB_R));
      float a1 = 0.55f * 0.5f * (1 + erff((0.9f * ORB_R - d1) / (0.35f * ORB_R) / 1.41421356f));
      float a2 = 0.70f * 0.5f * (1 + erff((0.6f * ORB_R - d2) / (0.5f * ORB_R) / 1.41421356f));
      glowMap[y * GW + x] = uint8_t((1 - (1 - a1) * (1 - a2)) * 255 + 0.5f);
    }
  return true;
}

void background(RGB m1, float strength, float cover) {
  static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  static uint16_t lut[16][256];
  for (int d = 0; d < 16; d++) {
    float k = (BAYER[d] + 0.5f) / 16;
    for (int a = 0; a < 256; a++) {
      RGB c = gfx::scale(m1, a / 255.f * strength);
      lut[d][a] = a && strength > 0 ? gfx::swap(gfx::c565({c.r + 8 * k, c.g + 4 * k, c.b + 8 * k})) : 0;
    }
  }
  gfx::rows(0, gfx::H, [&](int row, int x0, int x1) {
    int y = row / gfx::W;
    float dy = y + 0.5f - OY, hide = cover * cover - dy * dy;
    int s0 = x1, s1 = x1;
    if (hide > 0) {
      int h = int(sqrtf(hide));
      s0 = int(OX) - h, s1 = int(OX) + h;
    }
    const uint16_t *l = lut[0] + (y & 3) * 4 * 256;
    const uint8_t *g = glowMap + (y >> 1) * GW;
    uint16_t *out = gfx::fb + row;
    for (int x = x0; x < s0; x++) out[x] = l[(x & 3) * 256 + g[x >> 1]];
    for (int x = s1; x < x1; x++) out[x] = l[(x & 3) * 256 + g[x >> 1]];
  });
}

void orb(float cx, float cy, float R, const Palette &p, const Motion &m) {
  struct Placed {
    float x, y, r2, k;
    RGB c;
  } blobs[3];
  const RGB *colors[3] = {&p.m1, &p.m2, &p.m3};
  float cs = cosf(m.spin * gfx::TAU), sn = sinf(m.spin * gfx::TAU);
  const float sigma = 0.15f;  // blur(0.075 * size)
  for (int b = 0; b < 3; b++) {
    const Blob &B = BLOBS[b];
    float ph = fmodf(m.phase[b], 2);
    if (ph > 1) ph = 2 - ph;
    float dx, dy, k;
    if (ph < 0.5f) {
      float e = easeInOut(ph * 2);
      dx = B.k[0][0] * e, dy = B.k[0][1] * e, k = 1 + (B.k[0][2] - 1) * e;
    } else {
      float e = easeInOut(ph * 2 - 1);
      dx = B.k[0][0] + (B.k[1][0] - B.k[0][0]) * e;
      dy = B.k[0][1] + (B.k[1][1] - B.k[0][1]) * e;
      k = B.k[0][2] + (B.k[1][2] - B.k[0][2]) * e;
    }
    float x = B.x + dx, y = B.y + dy, r = B.r * k;
    blobs[b] = {x * cs - y * sn, x * sn + y * cs, r * r, 1 / (2 * r * sigma * 4.4f), *colors[B.color]};
  }
  RGB base = gfx::mix(p.m2, p.m1, 0.45f);

  int half = (int(ceilf(R)) + 2) & ~1;
  if (half > CELLS) half = CELLS;
  int x0 = int(floorf(cx)) - half, y0 = int(floorf(cy)) - half;
  float step = 2 / R;
  for (int gy = 0; gy < half; gy++) {
    float v = (y0 + 2 * gy + 1 - cy) / R, u = (x0 + 1 - cx) / R;
    for (int gx = 0; gx < half; gx++, u += step) {
      if (u * u + v * v > 1.1f) continue;
      float r = base.r, g = base.g, b = base.b;
      for (const Placed &B : blobs) {
        float d2 = (u - B.x) * (u - B.x) + (v - B.y) * (v - B.y);
        float t = (B.r2 - d2) * B.k + 0.5f;
        if (t <= 0) continue;
        float a = t >= 1 ? 1 : t * t * (3 - 2 * t);
        r += (B.c.r - r) * a, g += (B.c.g - g) * a, b += (B.c.b - b) * a;
      }
      // The inset shadow that darkens the lower right
      float hx = u + 0.16f, hy = v + 0.2f;
      float lit = 1 - 0.18f * (1 - phi((1 - hx * hx - hy * hy) / 0.4f));
      r *= lit, g *= lit, b *= lit;
      // Glass: a lit rim and light bounced up from below, in --m2; a soft
      // highlight up and to the left with a small glint in it
      float rr = u * u + v * v;
      float rim = rr > 0.6f ? smooth(gfx::clamp01((rr - 0.6f) / 0.4f)) * 0.3f : 0;
      float bx = u / 0.55f, by = (v - 0.66f) / 0.2f, b2 = bx * bx + by * by;
      float light = rim + (b2 < 1 ? (1 - b2) * 0.16f : 0);
      r += (p.m2.r - r) * light, g += (p.m2.g - g) * light, b += (p.m2.b - b) * light;
      float sx = (u + 0.34f) / 0.5f, sy = (v + 0.44f) / 0.32f, s2 = sx * sx + sy * sy;
      float tx = (u + 0.40f) / 0.11f, ty = (v + 0.50f) / 0.08f, t2 = tx * tx + ty * ty;
      float shine = (s2 < 1 ? (1 - s2) * (1 - s2) * 0.42f : 0) + (t2 < 1 ? (1 - t2) * 0.55f : 0);
      r += (255 - r) * shine, g += (255 - g) * shine, b += (255 - b) * shine;
      cells[gy * CELLS + gx] = gfx::c565({r, g, b});
    }
  }

  // Inside the rim the cells go straight down; the rim is antialiased and
  // carries the faint white inset line of .orb-body
  const RGB white = hex(0xffffff);
  for (int y = y0; y < y0 + 2 * half; y++) {
    if (y < 0 || y >= gfx::H) continue;
    float dy = y + 0.5f - cy, out2 = (R + 0.5f) * (R + 0.5f) - dy * dy;
    if (out2 <= 0) continue;
    float in2 = (R - 2.5f) * (R - 2.5f) - dy * dy, xo = sqrtf(out2), xi = in2 > 0 ? sqrtf(in2) : -1;
    int xa = int(floorf(cx - xo - 0.5f)), xb = int(ceilf(cx + xo - 0.5f));
    if (xa < x0) xa = x0;
    if (xb > x0 + 2 * half - 1) xb = x0 + 2 * half - 1;
    if (xa < 0) xa = 0;
    if (xb > gfx::W - 1) xb = gfx::W - 1;
    const uint16_t *row = cells + ((y - y0) >> 1) * CELLS;
    for (int x = xa; x <= xb; x++) {
      uint16_t c = row[(x - x0) >> 1];
      float dx = x + 0.5f - cx;
      int i = y * gfx::W + x;
      if (fabsf(dx) <= xi) {
        gfx::fb[i] = gfx::swap(c);
        continue;
      }
      float d = sqrtf(dx * dx + dy * dy), cov = gfx::clamp01(R + 0.5f - d);
      if (cov <= 0) continue;
      float rim = gfx::clamp01(1 - fabsf(d - (R - 1.2f)) / 1.4f);
      if (rim > 0) c = gfx::c565(gfx::mix(rgb(c), white, 0.18f * rim));
      gfx::blend(i, c, uint32_t(cov * 32));
    }
  }
}

void stars(float seconds, float alpha) {
  struct Star {
    float a, r, bright, rate, phase;
  };
  static Star sky[120];
  static bool made;
  if (!made) {
    uint32_t seed = 0x9E3779B9u;
    auto rnd = [&] {
      seed = seed * 1664525u + 1013904223u;
      return (seed >> 8) / 16777216.f;
    };
    for (Star &s : sky) s = {rnd(), 26 + 200 * sqrtf(rnd()), 0.1f + 0.5f * rnd() * rnd(), 0.06f + 0.22f * rnd(), rnd()};
    made = true;
  }
  uint16_t c = gfx::c565(hex(0xd8deff));
  float turn = seconds / 1200;
  for (const Star &s : sky) {
    float a = (s.a + turn) * gfx::TAU;
    float x = gfx::CX + sinf(a) * s.r - 0.5f, y = gfx::CY - cosf(a) * s.r - 0.5f;
    float k = s.bright * (0.55f + 0.45f * sinf((seconds * s.rate + s.phase) * gfx::TAU)) * alpha * 32;
    // Spread over four pixels so a star glides rather than steps
    int x0 = int(floorf(x)), y0 = int(floorf(y));
    float fx = x - x0, fy = y - y0;
    if (x0 < 0 || y0 < 0 || x0 >= gfx::W - 1 || y0 >= gfx::H - 1) continue;
    int i = y0 * gfx::W + x0;
    gfx::add(i, c, uint32_t(k * (1 - fx) * (1 - fy)));
    gfx::add(i + 1, c, uint32_t(k * fx * (1 - fy)));
    gfx::add(i + gfx::W, c, uint32_t(k * (1 - fx) * fy));
    gfx::add(i + gfx::W + 1, c, uint32_t(k * fx * fy));
  }
}

void scale(float turn, RGB majorColor, float alpha, float sweep) {
  static uint8_t kind[120];
  static int len16[3], half16[3];
  static uint16_t shine[128];  // the sweep's brightening, by angular distance
  constexpr int SHINE_SPAN = 0x1400;  // about 28 degrees either side
  if (!len16[0]) {
    for (int n = 0; n < 120; n++) kind[n] = n % 10 == 0 ? 2 : n % 5 == 0 ? 1 : 0;
    const float len[3] = {4.5f, 7.5f, 13}, width[3] = {1.3f, 1.3f, 1.9f};
    for (int k = 0; k < 3; k++) len16[k] = int(len[k] * 16), half16[k] = int((width[k] / 2 + 0.5f) * 16);
    for (int k = 0; k < 128; k++) {
      float d = k / 128.f * SHINE_SPAN / 65536.f / 0.035f;
      shine[k] = uint16_t(256 + 380 * expf(-d * d));
    }
  }
  uint16_t major = gfx::c565(majorColor), minor = gfx::c565(TEXT);
  uint32_t A = uint32_t(gfx::clamp01(alpha) * 256);
  constexpr int BASE16 = int(R_TICK * 16);
  uint32_t start = gfx::angle16(turn), lightAt = gfx::angle16(sweep);
  gfx::annulus(R_TICK - 1, R_TICK + 14, [&](int i) {
    uint32_t p = gfx::pol[i];
    int r16 = gfx::dist16(p);
    // Where the pixel falls between ticks, in 1/65536 of a tick from the nearest
    uint32_t u = (((p >> 16) - start) & 0xFFFF) * 120 + 32768;
    int n = int(u >> 16), off = int(u & 0xFFFF) - 32768;
    if (n >= 120) n -= 120;
    int k = kind[n];
    int along = r16 - BASE16 + 8, end = BASE16 + len16[k] + 8 - r16;
    if (end < along) along = end;
    if (along <= 0) return;
    // A tick is 2 pi r / 120 across; this is the distance off it in 1/16 px
    int across = half16[k] - int(uint32_t(off < 0 ? -off : off) * uint32_t(r16) / 1251662u);
    if (across <= 0) return;
    uint32_t a = uint32_t(across > 16 ? 16 : across) * uint32_t(along > 16 ? 16 : along) >> 3;
    int off16 = int(int16_t((p >> 16) - lightAt));
    if (off16 < 0) off16 = -off16;
    uint32_t lit = off16 < SHINE_SPAN ? shine[off16 * 128 / SHINE_SPAN] : 256;
    uint32_t AL = A * lit >> 8;
    if (k == 2) {
      uint32_t v = a * AL >> 8;
      gfx::blend(i, major, v > 32 ? 32 : v);
    } else {
      uint32_t v = (a * 33 >> 7) * AL >> 8;  // at 26%
      gfx::blend(i, minor, v > 32 ? 32 : v);
    }
  });
}

void tail(float r, float head, float span, uint16_t c, float alpha) {
  // A wide faint wake with a bright thread down its middle, both fading out
  gfx::arc(r, 7, head - span, head, c, 0, alpha * 0.35f);
  gfx::arc(r, 2.4f, head - span * 0.8f, head, c, 0, alpha);
}

void satellite(float x, float y, float radius, uint16_t c, float glow, bool hollow, float alpha) {
  if (alpha <= 0.01f) return;
  if (glow > 0.01f) gfx::glowAt(x, y, radius + 20, c, 0.5f * glow * alpha);
  gfx::discAt(x, y, radius + 2.5f, 0, alpha);
  if (hollow) {
    gfx::ringAt(x, y, radius - 1.2f, 2.2f, c, alpha);
    return;
  }
  gfx::discAt(x, y, radius, c, alpha);
  // A glint up and to the left, like the orb's
  RGB body = rgb(c);
  gfx::discAt(x - radius * 0.32f, y - radius * 0.36f, radius * 0.42f, gfx::c565(gfx::mix(body, hex(0xffffff), 0.6f)), 0.85f * alpha);
}

}  // namespace halo
