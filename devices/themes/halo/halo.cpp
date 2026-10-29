#ifndef __clang__
#pragma GCC optimize("O3")
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
// all round and a warmer one below. So smooth that a value per 4x4 pixels,
// dithered, does.
constexpr int GQ = (gfx::W + 3) / 4;
uint8_t *glowMap;
int16_t glowSpan[GQ][2];  // each row's cells with any glow, [first, last)
// The glow's colour by dither cell and strength, worked out when they change
uint16_t glowLut[16 * 256];

// The orb's colour field: a grid over the unit disc, finer for a bigger orb.
// Each cell keeps how much of --m1, --m2, --m3 and white its colour is made
// of, from the drifting blobs and the still shading (the inset shadow, the
// rim and bounce light, the highlight). The blobs move them once a frame and
// the palette colours them, so a change of colour costs no more than holding
// still.
constexpr int MAX_N = 96;
uint16_t cells[MAX_N * MAX_N];  // in the panel's order: the inside goes straight down
uint32_t *shares;               // per cell: m1, m2, m3 and white, 0..255 each, low byte first
// A finer grid the orb is growing into, worked out a slice of rows a frame
// while the coarser one is still drawn, and taken over once complete
uint32_t *nextShares;
uint16_t *nextCells;
int nextN, nextEvery, nextDone;
bool nextRecolour;
// The still shading of a cell, folded into how much of its blob colours it
// keeps and the --m2 light added over them, out of 255 in 1/256 steps; and
// its white. In integers: the ESP32's FPU stalls on every result it needs
// next, and a cell is a long chain of them.
struct Still {
  uint8_t *inside, *white;  // inside 0 marks a cell outside the disc
  uint16_t *keep, *light;
};
// Smoothstep of t/256 in 1/32768 steps
uint16_t smoothed[257];
Still stills[3];
uint32_t frameNo, sharesFrame = ~0u;
float frameDt;
int sharesN;
int lastPal[3][3] = {{-1}};

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

const Still &still(int N);

}  // namespace

Palette palette(model::Mood mood) { return PALETTES[int(mood)]; }

Palette mix(const Palette &a, const Palette &b, float t) {
  return {gfx::mix(a.m1, b.m1, t), gfx::mix(a.m2, b.m2, t), gfx::mix(a.m3, b.m3, t)};
}

bool init() {
  glowMap = (uint8_t *)gfx::bigAlloc(GQ * GQ);
  shares = (uint32_t *)gfx::bigAlloc(MAX_N * MAX_N * 4);
  nextShares = (uint32_t *)gfx::bigAlloc(MAX_N * MAX_N * 6);
  nextCells = (uint16_t *)(nextShares + MAX_N * MAX_N);
  if (!glowMap || !shares || !nextShares) return false;
  for (int y = 0; y < GQ; y++)
    for (int x = 0; x < GQ; x++) {
      float dx = 4 * x + 2 - OX, dy = 4 * y + 2 - OY;
      float d1 = sqrtf(dx * dx + dy * dy);
      float d2 = sqrtf(dx * dx + (dy - 0.36f * ORB_R) * (dy - 0.36f * ORB_R));
      float a1 = 0.55f * 0.5f * (1 + erff((0.9f * ORB_R - d1) / (0.35f * ORB_R) / 1.41421356f));
      float a2 = 0.70f * 0.5f * (1 + erff((0.6f * ORB_R - d2) / (0.5f * ORB_R) / 1.41421356f));
      glowMap[y * GQ + x] = uint8_t((1 - (1 - a1) * (1 - a2)) * 255 + 0.5f);
    }
  for (int y = 0; y < GQ; y++) {
    int a = GQ, b = 0;
    for (int x = 0; x < GQ; x++)
      if (glowMap[y * GQ + x]) a = x < a ? x : a, b = x + 1;
    glowSpan[y][0] = int16_t(a < b ? a : 0), glowSpan[y][1] = int16_t(b);
  }
  for (int t = 0; t <= 256; t++) smoothed[t] = uint16_t(smooth(t / 256.f) * 32768 + 0.5f);
  // The shading for each size of grid, before the orb first changes size
  for (int N = MAX_N; N >= MAX_N / 4; N /= 2)
    if (!still(N).inside) return false;
  return true;
}

namespace {

HOT void drawBackground(bool dark, float cx, float cy, float cover) {
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
    int x0 = gfx::inside[y][0], x1 = gfx::inside[y][1];
    if (x1 <= x0) x0 = x1 = 0;
    // What the orb will cover is left as it is
    float hy = y + 0.5f - cy, hide = cover * cover - hy * hy;
    int s0 = gfx::W, s1 = gfx::W;
    if (hide > 0) {
      int h = int(sqrtf(hide));
      s0 = int(cx) - h, s1 = int(cx) + h;
      s0 = s0 < 0 ? 0 : s0, s1 = s1 > gfx::W ? gfx::W : s1;
    }
    // Black outside the round, so a band sent straight to the panel holds
    // nothing old; and inside it too when there is no glow
    if (dark) {
      memset(out, 0, s0 * 2);
      if (s1 < gfx::W) memset(out + s1, 0, (gfx::W - s1) * 2);
      continue;
    }
    // Where the glow has died away it is black too
    int g0 = glowSpan[y >> 2][0] * 4, g1 = glowSpan[y >> 2][1] * 4;
    x0 = x0 > g0 ? x0 : g0, x1 = x1 < g1 ? x1 : g1;
    if (x1 < x0) x1 = x0;
    memset(out, 0, x0 * 2);
    memset(out + x1, 0, (gfx::W - x1) * 2);
    const uint16_t *l = glowLut + (y & 3) * 4 * 256;
    const uint8_t *g = glowMap + (y >> 2) * GQ;
    run(out, l, g, x0, s0 < x1 ? s0 : x1);
    run(out, l, g, s1 > x0 ? s1 : x0, x1);
  }
}

}  // namespace

void background(RGB m1, float strength, float cx, float cy, float cover) {
  // Worked out again only for a change that can show: colours ease toward
  // where they are going for ever, in ever smaller steps. As the screens
  // change it changes every frame, so half the strengths on each core.
  static int last[4] = {-1};
  int key[4] = {int(m1.r * 4), int(m1.g * 4), int(m1.b * 4), int(strength * 2048)};
  if (memcmp(key, last, sizeof key)) {
    memcpy(last, key, sizeof key);
    struct Glow {
      RGB m1;
      float strength;
    } glow = {m1, strength};
    gfx::split(
        [](int half, void *arg) {
          static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
          const Glow &g = *(const Glow *)arg;
          for (int a = half * 128; a < half * 128 + 128; a++) {
            RGB c = gfx::scale(g.m1, a / 255.f * g.strength);
            for (int d = 0; d < 16; d++) {
              float k = (BAYER[d] + 0.5f) / 16;
              glowLut[d * 256 + a] = a && g.strength > 0 ? gfx::swap(gfx::c565({c.r + 8 * k, c.g + 4 * k, c.b + 8 * k})) : 0;
            }
          }
        },
        &glow);
  }
  bool dark = strength <= 0;
  if (gfx::recording) return gfx::keep(0, gfx::H, [=] { drawBackground(dark, cx, cy, cover); });
  drawBackground(dark, cx, cy, cover);
}

namespace {

// The still parts of an N x N grid's shading
const Still &still(int N) {
  int k = N == MAX_N ? 0 : N == MAX_N / 2 ? 1 : 2;
  Still &s = stills[k];
  if (s.inside) return s;
  uint8_t *bytes = (uint8_t *)gfx::bigAlloc(N * N * 2);
  uint16_t *words = (uint16_t *)gfx::bigAlloc(N * N * 4);
  if (!bytes || !words) return s;
  s.white = bytes + N * N;
  s.keep = words, s.light = words + N * N;
  s.inside = bytes;
  for (int gy = 0; gy < N; gy++)
    for (int gx = 0; gx < N; gx++) {
      int i = gy * N + gx;
      float u = (gx + 0.5f) / N * 2 - 1, v = (gy + 0.5f) / N * 2 - 1, rr = u * u + v * v;
      s.inside[i] = rr <= 1.1f;
      if (!s.inside[i]) continue;
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
      // As bytes first, as the shading has always been kept
      lit = uint8_t(fmaxf(1, lit * 255 + 0.5f)) / 255.f;
      light = uint8_t(gfx::clamp01(light) * 255 + 0.5f) / 255.f;
      s.white[i] = uint8_t(gfx::clamp01(shine) * 255 + 0.5f);
      shine = s.white[i] / 255.f;
      s.keep[i] = uint16_t(lit * (1 - light) * (1 - shine) * 255 * 256 + 0.5f);
      s.light[i] = uint16_t((light * (1 - shine) * 255 + 0.5f) * 256 + 0.5f);
    }
  return s;
}

// Where the blobs are this frame, and what the orb's grid needs done
struct Placed {
  float x, y, r2, k;
  int color;
};
struct Grid {
  int N, every, phase;  // the rows that move: those at `phase` of each `every`
  bool recolour;        // a new palette, for every row
  uint32_t *shares;
  uint16_t *cells;
  Placed blobs[3];
  // A blob's reach into a cell, (r^2 - d^2) k + 1/2, falls apart into a part
  // for the row and one for the column: the columns', worked out once, in
  // 1/256 steps
  int across[3][MAX_N];
  int pal[3][3];
};

void placeBlobs(Grid &g, const Motion &m) {
  Placed *blobs = g.blobs;
  float sn, cs;
  gfx::sincosTurns(m.spin, sn, cs);
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
    blobs[b] = {x * cs - y * sn, x * sn + y * cs, r * r, 1 / (2 * r * sigma * 4.4f), B.color};
  }
  float step = 2.f / g.N;
  for (int b = 0; b < 3; b++)
    for (int gx = 0; gx < g.N; gx++) {
      float du = (gx + 0.5f) * step - 1 - blobs[b].x;
      g.across[b][gx] = int(du * du * blobs[b].k * 256 + 0.5f);
    }
}

// Works out the shares of the moving rows in [gy0, gy1) from where the blobs are
HOT void place(const Grid &g, int gy0, int gy1) {
  const int N = g.N;
  const Placed *blobs = g.blobs;
  const Still &st = still(N);
  float step = 2.f / N;
  for (int gy = gy0 + ((g.phase - gy0) % g.every + g.every) % g.every; gy < gy1; gy += g.every) {
    float v = (gy + 0.5f) * step - 1;
    int down[3];
    for (int b = 0; b < 3; b++) {
      float dv = v - blobs[b].y;
      down[b] = gfx::ifloor(((blobs[b].r2 - dv * dv) * blobs[b].k + 0.5f) * 256 + 0.5f);
    }
    for (int gx = 0; gx < N; gx++) {
      int i = gy * N + gx;
      if (!st.inside[i]) continue;
      // The base, between --m2 and --m1, then each blob over it; shares out
      // of 32768
      int w0 = 14746, w1 = 18022, w2 = 0;
      for (int b = 0; b < 3; b++) {
        int t = down[b] - g.across[b][gx];
        if (t <= 0) continue;
        int a = t >= 256 ? 32768 : smoothed[t];
        w0 -= w0 * a >> 15, w1 -= w1 * a >> 15, w2 -= w2 * a >> 15;
        int c = blobs[b].color;
        if (c == 0)
          w0 += a;
        else if (c == 1)
          w1 += a;
        else
          w2 += a;
      }
      uint32_t keep = st.keep[i];
      g.shares[i] = ((w0 * keep >> 15) + 128) >> 8 | ((w1 * keep >> 15) + st.light[i]) >> 8 << 8 |
                    ((w2 * keep >> 15) + 128) >> 8 << 16 | uint32_t(st.white[i]) << 24;
    }
  }
}

// Colours the cells of rows [gy0, gy1) from their shares: the rows that
// moved, or all of them for a new palette
HOT void colour(const Grid &g, int gy0, int gy1) {
  const int N = g.N;
  const auto &pal = g.pal;
  const Still &st = still(N);
  int every = g.recolour ? 1 : g.every;
  for (int gy = gy0 + ((g.phase - gy0) % every + every) % every; gy < gy1; gy += every)
    for (int gx = 0; gx < N; gx++) {
      int i = gy * N + gx;
      if (!st.inside[i]) continue;
      uint32_t s = g.shares[i], s1 = s & 255, s2 = s >> 8 & 255, s3 = s >> 16 & 255, sw = s >> 24;
      int c[3];
      for (int ch = 0; ch < 3; ch++) {
        uint32_t v = (s1 * pal[0][ch] + s2 * pal[1][ch] + s3 * pal[2][ch] + sw * 255) * 257 >> 16;
        c[ch] = v > 255 ? 255 : int(v);
      }
      g.cells[i] = gfx::swap(uint16_t((c[0] & 0xF8) << 8 | (c[1] & 0xFC) << 3 | c[2] >> 3));
    }
}

// Moves the blobs and colours the rows g asks for: the top half on one
// core, the bottom on the other
void update(Grid &g, const Motion &m) {
  placeBlobs(g, m);
  still(g.N);
  gfx::split(
      [](int half, void *arg) {
        const Grid &g = *(const Grid *)arg;
        int gy0 = half ? g.N / 2 : 0, gy1 = half ? g.N : g.N / 2;
        place(g, gy0, gy1);
        colour(g, gy0, gy1);
      },
      &g);
}

HOT void drawOrb(float cx, float cy, float R, int N) {
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
        float rim = gfx::clamp01(1 - fabsf(d - (R - 1.2f)) * (1 / 1.4f));
        if (rim > 0) c = gfx::c565(gfx::mix(rgb(c), white, 0.18f * rim));
        gfx::blend(y * gfx::W + x, c, uint32_t(cov * 32));
      }
    }
  }
}

}  // namespace

uint32_t gridTicks;

void newFrame(float dt) { frameNo++, frameDt = dt; }

void orb(float cx, float cy, float R, const Palette &p, const Motion &m, bool settled) {
  int want = R >= 48 ? MAX_N : R >= 24 ? MAX_N / 2 : MAX_N / 4;
  if (sharesFrame != frameNo) {
    uint32_t t0 = gfx::ticks();
    static Grid live, next;
    // A new colour reaches every cell; otherwise only the rows that moved
    // need colouring again
    int pal[3][3];
    const RGB *cs[3] = {&p.m1, &p.m2, &p.m3};
    for (int k = 0; k < 3; k++) {
      pal[k][0] = gfx::clamp8(cs[k]->r + 0.5f), pal[k][1] = gfx::clamp8(cs[k]->g + 0.5f), pal[k][2] = gfx::clamp8(cs[k]->b + 0.5f);
    }
    bool recolour = memcmp(pal, lastPal, sizeof pal) != 0;
    memcpy(lastPal, pal, sizeof pal);
    // Drifting slowly, the blobs move only some of the rows each frame, so
    // that none is more than about 60 ms behind
    bool steady = settled && sharesN && sharesFrame + 1 == frameNo;
    int every = frameDt > 0.03f ? 2 : frameDt > 0.015f ? 3 : 4;
    // The finest grid is the one that costs. An orb growing fast toward it
    // has it worked out ahead, a slice of rows a frame, while it is drawn
    // from the next grid down; and keeps it once it has it.
    static float lastR;
    bool rising = R - lastR > 0.5f;
    lastR = R;
    bool keepFine = sharesN == MAX_N && rising;
    bool grow = !keepFine && steady && sharesN < MAX_N && (want == MAX_N || (rising && R >= 30));
    int N = keepFine ? MAX_N : grow && want == MAX_N ? MAX_N / 2 : want;
    live.N = N, live.every = steady && sharesN == N ? every : 1, live.phase = int(frameNo % uint32_t(live.every));
    live.recolour = recolour, live.shares = shares, live.cells = cells;
    memcpy(live.pal, pal, sizeof pal);
    update(live, m);
    sharesN = N;
    if (!grow) {
      nextN = 0;
    } else {
      if (nextN != MAX_N) nextN = MAX_N, nextEvery = every, nextDone = 0, nextRecolour = false;
      next.N = MAX_N, next.every = nextEvery, next.phase = nextDone, next.recolour = false;
      next.shares = nextShares, next.cells = nextCells;
      memcpy(next.pal, pal, sizeof pal);
      update(next, m);
      nextRecolour |= nextDone > 0 && recolour;
      if (++nextDone == nextEvery) {
        memcpy(shares, nextShares, MAX_N * MAX_N * 4);
        memcpy(cells, nextCells, MAX_N * MAX_N * 2);
        sharesN = MAX_N, nextN = 0;
        // Rows worked out under an older colour take the new one
        if (nextRecolour) {
          live.N = MAX_N, live.every = 1, live.phase = 0, live.recolour = true;
          gfx::split(
              [](int half, void *arg) {
                const Grid &g = *(const Grid *)arg;
                colour(g, half ? g.N / 2 : 0, half ? g.N : g.N / 2);
              },
              &live);
        }
      }
    }
    gridTicks += gfx::ticks() - t0;
    sharesFrame = frameNo;
  }
  int N = sharesN;
  if (gfx::recording) return gfx::keep(cy - R - 1, cy + R + 1, [=] { drawOrb(cx, cy, R, N); });
  drawOrb(cx, cy, R, N);
}

namespace {

// Where each star falls and how bright, once a frame: spread over four
// pixels, so a star glides rather than steps
struct Lit {
  int x, y;
  float w[4];  // weight for each of the four pixels, 0..32
};
Lit litStars[120];

HOT void drawStars(float alpha) {
  uint16_t c = gfx::c565(hex(0xd8deff));
  for (const Lit &s : litStars) {
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

}  // namespace

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
    for (Star &s : sky) s = {rnd(), 26 + 206 * sqrtf(rnd()), 0.1f + 0.5f * rnd() * rnd(), 0.06f + 0.22f * rnd(), rnd()};
    made = true;
  }
  float turn = seconds / 1200;
  for (int n = 0; n < 120; n++) {
    const Star &s = sky[n];
    float sn, cs, tw, unused;
    gfx::sincosTurns(s.a + turn, sn, cs);
    gfx::sincosTurns(seconds * s.rate + s.phase, tw, unused);
    float x = gfx::CX + sn * s.r - 0.5f, y = gfx::CY - cs * s.r - 0.5f;
    float k = s.bright * (0.55f + 0.45f * tw) * 32;
    int x0 = gfx::ifloor(x), y0 = gfx::ifloor(y);
    float fx = x - x0, fy = y - y0;
    litStars[n] = {x0, y0, {k * (1 - fx) * (1 - fy), k * fx * (1 - fy), k * (1 - fx) * fy, k * fx * fy}};
  }
  if (gfx::recording) return gfx::keep(0, gfx::H, [=] { drawStars(alpha); });
  drawStars(alpha);
}

namespace {

// Each tick's direction and how much the sweep lights it, once a frame
const float LEN[3] = {4.5f, 7.5f, 13}, HALF[3] = {0.65f, 0.65f, 0.95f};
struct Tick {
  float dx, dy, lit;
  int16_t x0, x1, y0, y1;  // the pixels it can touch
  uint8_t kind;            // 0 every 3 degrees, 1 every 15, 2 every 30
};
Tick ticks[120];

HOT void drawScale(uint16_t major, uint16_t minor, float A) {
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

}  // namespace

void scale(float turn, RGB majorColor, float alpha, float sweep) {
  for (int n = 0; n < 120; n++) {
    Tick &t = ticks[n];
    float u = turn + n / 120.f, sn, cs;
    float d = u - sweep;
    d -= float(gfx::ifloor(d + 0.5f));
    d *= 1 / 0.035f;
    t.kind = uint8_t(n % 10 == 0 ? 2 : n % 5 == 0 ? 1 : 0);
    gfx::sincosTurns(u, sn, cs);
    // Far from the sweep the light is 1 to the last bit
    t.dx = sn, t.dy = -cs, t.lit = d * d > 20 ? 1 : 1 + 1.48f * expf(-d * d);
    float len = LEN[t.kind], half = HALF[t.kind];
    float ax = gfx::CX + t.dx * R_TICK, ay = gfx::CY + t.dy * R_TICK, bx = ax + t.dx * len, by = ay + t.dy * len;
    t.x0 = int16_t(gfx::ifloor((ax < bx ? ax : bx) - half - 1)), t.x1 = int16_t(gfx::iceil((ax > bx ? ax : bx) + half + 1));
    t.y0 = int16_t(gfx::ifloor((ay < by ? ay : by) - half - 1)), t.y1 = int16_t(gfx::iceil((ay > by ? ay : by) + half + 1));
  }
  uint16_t major = gfx::c565(majorColor), minor = gfx::c565(TEXT);
  float A = gfx::clamp01(alpha);
  if (gfx::recording) return gfx::keep(gfx::CY - R_TICK - 16, gfx::CY + R_TICK + 16, [=] { drawScale(major, minor, A); });
  drawScale(major, minor, A);
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
