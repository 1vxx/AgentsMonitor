#ifndef __clang__
#pragma GCC optimize("O2")
#endif

#include "halo.h"

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#endif

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
// all round and a warmer one below. So smooth that a value per 4x4 pixels,
// dithered, does; small enough then for internal RAM.
constexpr int GQ = (gfx::W + 3) / 4;
uint8_t *glowMap;

// The orb's colour field: a grid over the unit disc, finer for a bigger orb.
// What does not move (the shading, the rim and bounce light, the highlight) is
// worked out once per grid; the drifting blobs once per frame, however many
// bands the frame is drawn in.
constexpr int MAX_N = 96;
uint16_t cells[MAX_N * MAX_N];
struct Still {
  uint8_t *lit, *light, *shine;  // 0..255 per cell; lit 0 marks a cell outside the disc
};
Still stills[3];
uint32_t frameNo, cellsFrame = ~0u;
int cellsN;

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
  glowMap = (uint8_t *)gfx::fastAlloc(GQ * GQ);
  if (!glowMap) return false;
  for (int y = 0; y < GQ; y++)
    for (int x = 0; x < GQ; x++) {
      float dx = 4 * x + 2 - OX, dy = 4 * y + 2 - OY;
      float d1 = sqrtf(dx * dx + dy * dy);
      float d2 = sqrtf(dx * dx + (dy - 0.36f * ORB_R) * (dy - 0.36f * ORB_R));
      float a1 = 0.55f * 0.5f * (1 + erff((0.9f * ORB_R - d1) / (0.35f * ORB_R) / 1.41421356f));
      float a2 = 0.70f * 0.5f * (1 + erff((0.6f * ORB_R - d2) / (0.5f * ORB_R) / 1.41421356f));
      glowMap[y * GQ + x] = uint8_t((1 - (1 - a1) * (1 - a2)) * 255 + 0.5f);
    }
  return true;
}

void background(RGB m1, float strength, float cover) {
  static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  static uint16_t lut[16][256];
  // Built once for a colour and strength, not once per band
  static RGB lastColor = {-1, -1, -1};
  static float lastStrength = -1;
  bool same = m1.r == lastColor.r && m1.g == lastColor.g && m1.b == lastColor.b && strength == lastStrength;
  lastColor = m1, lastStrength = strength;
  for (int d = 0; d < 16 && !same; d++) {
    float k = (BAYER[d] + 0.5f) / 16;
    for (int a = 0; a < 256; a++) {
      RGB c = gfx::scale(m1, a / 255.f * strength);
      lut[d][a] = a && strength > 0 ? gfx::swap(gfx::c565({c.r + 8 * k, c.g + 4 * k, c.b + 8 * k})) : 0;
    }
  }
  // Pixel by pixel to a multiple of four, then four at a time: one glow value covers them all
  auto run = [](uint16_t *out, const uint16_t *l, const uint8_t *g, int x, int end) {
    for (; x < end && (x & 3); x++) out[x] = l[(x & 3) * 256 + g[x >> 2]];
    for (; x + 4 <= end; x += 4) {
      uint8_t v = g[x >> 2];
      uint32_t *pair = (uint32_t *)(out + x);
      pair[0] = l[v] | uint32_t(l[256 + v]) << 16;
      pair[1] = l[512 + v] | uint32_t(l[768 + v]) << 16;
    }
    for (; x < end; x++) out[x] = l[(x & 3) * 256 + g[x >> 2]];
  };
  for (int y = gfx::clipY0; y < gfx::clipY1; y++) {
    uint16_t *out = gfx::fb + y * gfx::W;
    float dy = y + 0.5f - gfx::CY, span = gfx::CX * gfx::CX - dy * dy;
    int x0 = 0, x1 = 0;
    if (span > 0) {
      int half = int(sqrtf(span)) + 1;
      x0 = int(gfx::CX) - half, x1 = int(gfx::CX) + half;
      x0 = x0 < 0 ? 0 : x0, x1 = x1 > gfx::W ? gfx::W : x1;
    }
    // Outside the round: black, so a band sent straight to the panel holds nothing old
    memset(out, 0, x0 * 2);
    memset(out + x1, 0, (gfx::W - x1) * 2);
    float hy = y + 0.5f - OY, hide = cover * cover - hy * hy;
    int s0 = x1, s1 = x1;
    if (hide > 0) {
      int h = int(sqrtf(hide));
      s0 = int(OX) - h, s1 = int(OX) + h;
    }
    const uint16_t *l = lut[0] + (y & 3) * 4 * 256;
    const uint8_t *g = glowMap + (y >> 2) * GQ;
    run(out, l, g, x0, s0 < x1 ? s0 : x1);
    run(out, l, g, s1 > x0 ? s1 : x0, x1);
  }
}

namespace {

// The still parts of an N x N grid's shading
const Still &still(int N) {
  int k = N == MAX_N ? 0 : N == MAX_N / 2 ? 1 : 2;
  Still &s = stills[k];
  if (s.lit) return s;
  s.lit = (uint8_t *)gfx::bigAlloc(N * N * 3);
  s.light = s.lit + N * N, s.shine = s.light + N * N;
  for (int gy = 0; gy < N; gy++)
    for (int gx = 0; gx < N; gx++) {
      int i = gy * N + gx;
      float u = (gx + 0.5f) / N * 2 - 1, v = (gy + 0.5f) / N * 2 - 1, rr = u * u + v * v;
      if (rr > 1.1f) {
        s.lit[i] = s.light[i] = s.shine[i] = 0;
        continue;
      }
      // The inset shadow that darkens the lower right
      float hx = u + 0.16f, hy = v + 0.2f;
      float lit = 1 - 0.18f * (1 - phi((1 - hx * hx - hy * hy) / 0.4f));
      // Glass: a lit rim and light bounced up from below, in --m2; a soft
      // highlight up and to the left with a small glint in it
      float rim = rr > 0.6f ? smooth(gfx::clamp01((rr - 0.6f) / 0.4f)) * 0.3f : 0;
      float bx = u / 0.55f, by = (v - 0.66f) / 0.2f, b2 = bx * bx + by * by;
      float light = rim + (b2 < 1 ? (1 - b2) * 0.16f : 0);
      float sx = (u + 0.34f) / 0.5f, sy = (v + 0.44f) / 0.32f, s2 = sx * sx + sy * sy;
      float tx = (u + 0.40f) / 0.11f, ty = (v + 0.50f) / 0.08f, t2 = tx * tx + ty * ty;
      float shine = (s2 < 1 ? (1 - s2) * (1 - s2) * 0.42f : 0) + (t2 < 1 ? (1 - t2) * 0.55f : 0);
      s.lit[i] = uint8_t(fmaxf(1, lit * 255 + 0.5f));
      s.light[i] = uint8_t(gfx::clamp01(light) * 255 + 0.5f);
      s.shine[i] = uint8_t(gfx::clamp01(shine) * 255 + 0.5f);
    }
  return s;
}

// Works out the cells' colours; `parity` 0 or 1 does only the even or odd rows
void colour(int N, const Palette &p, const Motion &m, int parity) {
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
  const Still &st = still(N);
  float step = 2.f / N;
  for (int gy = parity < 0 ? 0 : parity; gy < N; gy += parity < 0 ? 1 : 2) {
    float v = (gy + 0.5f) * step - 1, u = 0.5f * step - 1;
    for (int gx = 0; gx < N; gx++, u += step) {
      int i = gy * N + gx;
      if (!st.lit[i]) continue;
      float r = base.r, g = base.g, b = base.b;
      for (const Placed &B : blobs) {
        float d2 = (u - B.x) * (u - B.x) + (v - B.y) * (v - B.y);
        float t = (B.r2 - d2) * B.k + 0.5f;
        if (t <= 0) continue;
        float a = t >= 1 ? 1 : t * t * (3 - 2 * t);
        r += (B.c.r - r) * a, g += (B.c.g - g) * a, b += (B.c.b - b) * a;
      }
      float lit = st.lit[i] * (1 / 255.f), light = st.light[i] * (1 / 255.f), shine = st.shine[i] * (1 / 255.f);
      r *= lit, g *= lit, b *= lit;
      r += (p.m2.r - r) * light, g += (p.m2.g - g) * light, b += (p.m2.b - b) * light;
      r += (255 - r) * shine, g += (255 - g) * shine, b += (255 - b) * shine;
      cells[i] = gfx::swap(gfx::c565({r, g, b}));  // in the panel's order: the inside goes straight down
    }
  }
}

}  // namespace

uint32_t gridUs;

void newFrame() { frameNo++; }

void orb(float cx, float cy, float R, const Palette &p, const Motion &m, bool settled) {
  int N = R >= 48 ? MAX_N : R >= 24 ? MAX_N / 2 : MAX_N / 4;
  if (cellsFrame != frameNo || cellsN != N) {
    // Settled, the blobs drift slowly enough that each frame can redo half the
    // rows, the other half being a frame old
    bool half = settled && cellsN == N && cellsFrame + 1 == frameNo;
#ifdef ESP_PLATFORM
    int64_t t0 = esp_timer_get_time();
#endif
    colour(N, p, m, half ? int(frameNo & 1) : -1);
#ifdef ESP_PLATFORM
    gridUs += uint32_t(esp_timer_get_time() - t0);
#endif
    cellsFrame = frameNo, cellsN = N;
  }

  // Inside the rim the cells go straight down, stepped across in 16.16 fixed
  // point; only the rim, antialiased with the faint white inset line of
  // .orb-body, is worked out per pixel
  const RGB white = hex(0xffffff);
  float toCell = N / (2 * R);
  int32_t step = int32_t(toCell * 65536);
  int y0 = gfx::ifloor(cy - R - 1), y1 = gfx::iceil(cy + R + 1);
  if (y0 < gfx::clipY0) y0 = gfx::clipY0;
  if (y1 > gfx::clipY1) y1 = gfx::clipY1;
  for (int y = y0; y < y1; y++) {
    float dy = y + 0.5f - cy, out2 = (R + 0.5f) * (R + 0.5f) - dy * dy;
    if (out2 <= 0) continue;
    float in2 = (R - 2.5f) * (R - 2.5f) - dy * dy, xo = sqrtf(out2), xi = in2 > 0 ? sqrtf(in2) : -1;
    int xa = gfx::ifloor(cx - xo - 0.5f), xb = gfx::iceil(cx + xo - 0.5f);
    if (xa < 0) xa = 0;
    if (xb > gfx::W - 1) xb = gfx::W - 1;
    // The inside: pixel centres within xi of the middle
    int ia = xi < 0 ? xb + 1 : gfx::iceil(cx - xi - 0.5f), ib = xi < 0 ? xb : gfx::ifloor(cx + xi - 0.5f);
    if (ia < xa) ia = xa;
    if (ib > xb) ib = xb;
    int gy = int((dy + R) * toCell);
    gy = gy < 0 ? 0 : gy >= N ? N - 1 : gy;
    const uint16_t *row = cells + gy * N;
    uint16_t *out = gfx::fb + y * gfx::W;
    int32_t at = int32_t((ia + 0.5f - cx + R) * toCell * 65536);
    for (int x = ia; x <= ib; x++, at += step) {
      int gx = at >> 16;
      out[x] = row[gx < 0 ? 0 : gx >= N ? N - 1 : gx];
    }
    // The rim, either side of the inside
    for (int side = 0; side < 2; side++) {
      int from = side ? (ib + 1 > xa ? ib + 1 : xa) : xa, to = side ? xb : (ia - 1 < xb ? ia - 1 : xb);
      for (int x = from; x <= to; x++) {
        float dx = x + 0.5f - cx, d = sqrtf(dx * dx + dy * dy), cov = gfx::clamp01(R + 0.5f - d);
        if (cov <= 0) continue;
        int gx = int((dx + R) * toCell);
        uint16_t c = gfx::swap(row[gx < 0 ? 0 : gx >= N ? N - 1 : gx]);
        float rim = gfx::clamp01(1 - fabsf(d - (R - 1.2f)) / 1.4f);
        if (rim > 0) c = gfx::c565(gfx::mix(rgb(c), white, 0.18f * rim));
        gfx::blend(y * gfx::W + x, c, uint32_t(cov * 32));
      }
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
  // Where each star is and how bright, worked out once a frame
  // Where each star falls and how bright, once a frame: spread over four
  // pixels, so a star glides rather than steps
  struct Lit {
    int x, y;
    float w[4];  // weight for each of the four pixels, 0..32
  };
  static Lit lit[120];
  static uint32_t litFrame = ~0u;
  if (litFrame != frameNo) {
    float turn = seconds / 1200;
    for (int n = 0; n < 120; n++) {
      const Star &s = sky[n];
      float a = (s.a + turn) * gfx::TAU;
      float x = gfx::CX + sinf(a) * s.r - 0.5f, y = gfx::CY - cosf(a) * s.r - 0.5f;
      float k = s.bright * (0.55f + 0.45f * sinf((seconds * s.rate + s.phase) * gfx::TAU)) * 32;
      int x0 = gfx::ifloor(x), y0 = gfx::ifloor(y);
      float fx = x - x0, fy = y - y0;
      lit[n] = {x0, y0, {k * (1 - fx) * (1 - fy), k * fx * (1 - fy), k * (1 - fx) * fy, k * fx * fy}};
    }
    litFrame = frameNo;
  }
  uint16_t c = gfx::c565(hex(0xd8deff));
  for (const Lit &s : lit) {
    if (s.y + 1 < gfx::clipY0 || s.y >= gfx::clipY1 || s.x < 0 || s.y < 0 || s.x >= gfx::W - 1 || s.y >= gfx::H - 1) continue;
    int i = s.y * gfx::W + s.x;
    if (s.y >= gfx::clipY0) {
      gfx::add(i, c, uint32_t(s.w[0] * alpha));
      gfx::add(i + 1, c, uint32_t(s.w[1] * alpha));
    }
    if (s.y + 1 < gfx::clipY1) {
      gfx::add(i + gfx::W, c, uint32_t(s.w[2] * alpha));
      gfx::add(i + gfx::W + 1, c, uint32_t(s.w[3] * alpha));
    }
  }
}

void scale(float turn, RGB majorColor, float alpha, float sweep) {
  // Each tick's direction and how much the sweep lights it, once a frame
  static const float LEN[3] = {4.5f, 7.5f, 13}, HALF[3] = {0.65f, 0.65f, 0.95f};
  struct Tick {
    float dx, dy, lit;
    int16_t x0, x1, y0, y1;  // the pixels it can touch
    uint8_t kind;            // 0 every 3 degrees, 1 every 15, 2 every 30
  };
  static Tick ticks[120];
  static uint32_t madeFor = ~0u;
  if (madeFor != frameNo) {
    for (int n = 0; n < 120; n++) {
      Tick &t = ticks[n];
      float u = turn + n / 120.f, a = u * gfx::TAU;
      float d = u - sweep;
      d -= float(gfx::ifloor(d + 0.5f));
      d /= 0.035f;
      t.kind = uint8_t(n % 10 == 0 ? 2 : n % 5 == 0 ? 1 : 0);
      t.dx = sinf(a), t.dy = -cosf(a), t.lit = 1 + 1.48f * expf(-d * d);
      float len = LEN[t.kind], half = HALF[t.kind];
      float ax = gfx::CX + t.dx * R_TICK, ay = gfx::CY + t.dy * R_TICK, bx = ax + t.dx * len, by = ay + t.dy * len;
      t.x0 = int16_t(gfx::ifloor((ax < bx ? ax : bx) - half - 1)), t.x1 = int16_t(gfx::iceil((ax > bx ? ax : bx) + half + 1));
      t.y0 = int16_t(gfx::ifloor((ay < by ? ay : by) - half - 1)), t.y1 = int16_t(gfx::iceil((ay > by ? ay : by) + half + 1));
    }
    madeFor = frameNo;
  }
  uint16_t major = gfx::c565(majorColor), minor = gfx::c565(TEXT);
  float A = gfx::clamp01(alpha);
  for (const Tick &t : ticks) {
    if (t.y1 <= gfx::clipY0 || t.y0 >= gfx::clipY1) continue;
    float len = LEN[t.kind], half = HALF[t.kind];
    int x0 = t.x0 < 0 ? 0 : t.x0, x1 = t.x1 > gfx::W ? gfx::W : t.x1;
    int y0 = t.y0 < gfx::clipY0 ? gfx::clipY0 : t.y0, y1 = t.y1 > gfx::clipY1 ? gfx::clipY1 : t.y1;
    float strength = A * t.lit * (t.kind == 2 ? 1 : 0.26f);
    uint16_t c = t.kind == 2 ? major : minor;
    for (int py = y0; py < y1; py++) {
      float ry = py + 0.5f - gfx::CY;
      for (int px = x0; px < x1; px++) {
        float rx = px + 0.5f - gfx::CX;
        float along = rx * t.dx + ry * t.dy, across = fabsf(rx * t.dy - ry * t.dx);
        float in = along - R_TICK + 0.5f, out = R_TICK + len - along + 0.5f;
        float cov = gfx::clamp01(half + 0.5f - across) * gfx::clamp01(in < out ? in : out) * strength;
        if (cov > 0) gfx::blend(py * gfx::W + px, c, uint32_t((cov < 1 ? cov : 1) * 32));
      }
    }
  }
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
