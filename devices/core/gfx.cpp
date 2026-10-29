#ifndef __clang__
#pragma GCC optimize("O2")
#endif

#include "gfx.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#else
#include <chrono>
#endif

namespace gfx {

__thread uint16_t *fb;
__thread int clipY0 = 0, clipY1 = H;
uint32_t *pol;
int16_t inside[H][2];
bool recording;

namespace {

// The frame's list: each kept call's header, then what it holds
constexpr size_t LIST_MAX = 12 * 1024;
uint8_t *list;
size_t listLen, listHigh;
uint8_t curTag;

void clip(int y0, int y1) {
  clipY0 = y0 < 0 ? 0 : y0;
  clipY1 = y1 > H ? H : y1;
}

// The next `size` bytes of the list, headed by how far it is to the next
Kept *take(size_t size) {
  size = (size + alignof(void *) - 1) & ~(alignof(void *) - 1);
  if (listLen + size > LIST_MAX || size > 0xFFFF) return nullptr;
  Kept *k = (Kept *)(list + listLen);
  k->size = uint16_t(size);
  listLen += size;
  if (listLen > listHigh) listHigh = listLen;
  return k;
}

}  // namespace

void *bigAlloc(size_t n) {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
#else
  return malloc(n);
#endif
}

void *fastAlloc(size_t n) {
#ifdef ESP_PLATFORM
  void *p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p ? p : bigAlloc(n);
#else
  return malloc(n);
#endif
}

bool init() {
  pol = (uint32_t *)bigAlloc(W * H * 4);
  list = (uint8_t *)fastAlloc(LIST_MAX);
  if (!pol || !list) return false;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      float dx = x + 0.5f - CX, dy = y + 0.5f - CY;
      uint32_t d = uint32_t(sqrtf(dx * dx + dy * dy) * 16 + 0.5f);
      pol[y * W + x] = (d > 0xFFFF ? 0xFFFF : d) | angle16(atan2f(dx, -dy) / TAU) << 16;
    }
    float dy = y + 0.5f - CY, s = CX * CX - dy * dy;
    int half = s > 0 ? int(sqrtf(s)) + 1 : 0, x0 = int(CX) - half, x1 = int(CX) + half;
    inside[y][0] = int16_t(s > 0 && x0 > 0 ? x0 : 0);
    inside[y][1] = int16_t(s <= 0 ? 0 : x1 > W ? W : x1);
  }
  return true;
}

// ---------- Recording and playback ----------

void record(bool on) {
  recording = on;
  if (on) listLen = 0, curTag = 0;
}

void tag(int t) { curTag = uint8_t(t < TAGS ? t : TAGS - 1); }

void *scratch(size_t n) {
  // Headed like a kept call that meets no rows, so playback steps over it
  Kept *k = take(sizeof(Kept) + n);
  if (!k) return nullptr;
  k->run = nullptr;
  k->y0 = k->y1 = 0;
  return k + 1;
}

Kept *keepRoom(size_t size, float y0, float y1) {
  int a = ifloor(y0), b = iceil(y1) + 1;
  if (a < 0) a = 0;
  if (b > H) b = H;
  if (b <= a) return nullptr;  // nowhere on the panel
  Kept *k = take(size);
  if (!k) return nullptr;
  k->y0 = int16_t(a), k->y1 = int16_t(b);
  k->tag = curTag;
  return k;
}

HOT void play(uint16_t *band, int y0, int y1, uint32_t *spent) {
  fb = band - y0 * W;
  clip(y0, y1);
  for (size_t at = 0; at < listLen;) {
    const Kept *k = (const Kept *)(list + at);
    at += k->size;
    if (k->y1 <= y0 || k->y0 >= y1) continue;
    if (!spent) {
      k->run(k + 1);
      continue;
    }
    uint32_t t0 = ticks();
    k->run(k + 1);
    spent[k->tag] += ticks() - t0;
  }
}

void (*split)(void (*fn)(int, void *), void *) = [](void (*fn)(int, void *), void *arg) {
  fn(0, arg);
  fn(1, arg);
};

size_t listUsed() { return listHigh; }

uint32_t ticks() {
#ifdef ESP_PLATFORM
  return esp_cpu_get_cycle_count();
#else
  using namespace std::chrono;
  return uint32_t(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

float ticksPerMs() {
#ifdef ESP_PLATFORM
  return esp_rom_get_cpu_ticks_per_us() * 1000.f;
#else
  return 1e6f;
#endif
}

// ---------- Pixels ----------

HOT void blendRun(int i0, int i1, uint16_t c, uint32_t a) {
  if (!a || i1 <= i0) return;
  uint16_t *p = fb + i0, *end = fb + i1;
  if (a >= 32) {
    uint16_t v = swap(c);
    while (p < end) *p++ = v;
    return;
  }
  uint32_t ca = spread(c) * a, k = 32 - a;
  for (; p < end; p++) {
    uint32_t r = (ca + spread(swap(*p)) * k) >> 5 & 0x07E0F81F;
    *p = swap(uint16_t(r | r >> 16));
  }
}

// ---------- Drawing calls ----------

void clear() {
  if (recording) return keep(0, H, [] { clear(); });
  rows(0, H, [](int row, int x0, int x1) { memset(fb + row + x0, 0, (x1 - x0) * 2); });
}

void dim(float k) {
  uint32_t a = uint32_t(clamp01(k) * 32 + 0.5f);
  if (a >= 32) return;
  if (recording) return keep(0, H, [=] { dim(k); });
  rows(0, H, [&](int row, int x0, int x1) {
    for (int i = row + x0; i < row + x1; i++) {
      uint32_t r = spread(swap(fb[i])) * a >> 5 & 0x07E0F81F;
      fb[i] = swap(uint16_t(r | r >> 16));
    }
  });
}

HOT void ring(float r, float w, uint16_t c, float alpha, int dashes, float on, float rot) {
  int R16 = int(r * 16 + 0.5f), H16 = int((w / 2 + 0.5f) * 16 + 0.5f), A = int(alpha * 32 + 0.5f);
  if (A <= 0) return;
  if (recording) return keep(CY - r - w / 2 - 2, CY + r + w / 2 + 2, [=] { ring(r, w, c, alpha, dashes, on, rot); });
  uint32_t turn = angle16(rot);
  int P16 = dashes ? int(TAU * r / dashes * 16) : 0, ON16 = int(on * 16);
  annulus(r - w / 2 - 1, r + w / 2 + 1, [&](int i) {
    uint32_t p = pol[i];
    int cov = H16 - abs(dist16(p) - R16);
    if (cov <= 0) return;
    if (cov > 16) cov = 16;
    if (dashes) {
      uint32_t s = (((p >> 16) - turn) & 0xFFFF) * dashes & 0xFFFF;
      int at = int(s * P16 >> 16), e = at + 8 < ON16 + 8 - at ? at + 8 : ON16 + 8 - at;
      if (e <= 0) return;
      if (e < 16) cov = cov * e >> 4;
    }
    blend(i, c, uint32_t(cov * A) >> 4);
  });
}

namespace {

// An arc as recording works it out: where it runs and the box round it
struct Arc {
  float ri, ro;
  uint32_t start, len;
  int R16, H16, A0, dA;
  int16_t x0, x1, y0, y1;  // the box, inclusive
  uint16_t c;
};

HOT void drawArc(const Arc &a) {
  auto pixel = [&](int i) {
    uint32_t p = pol[i];
    uint32_t along = ((p >> 16) - a.start) & 0xFFFF;
    if (along >= a.len) return;
    int cov = a.H16 - abs(dist16(p) - a.R16);
    if (cov <= 0) return;
    if (cov > 16) cov = 16;
    int al = a.A0 + a.dA * int(along) / int(a.len);  // alpha in 1/256 steps of 0..32
    if (al <= 0) return;
    blend(i, a.c, uint32_t(cov * al) >> 12);
  };
  // The ring's two stretches on each row, cut to the box
  int y0 = a.y0 < clipY0 ? clipY0 : a.y0, y1 = a.y1 > clipY1 - 1 ? clipY1 - 1 : a.y1;
  for (int y = y0; y <= y1; y++) {
    float dy = y + 0.5f - CY;
    float o2 = a.ro * a.ro - dy * dy;
    if (o2 <= 0) continue;
    float xo = sqrtf(o2), i2 = a.ri * a.ri - dy * dy, xi = i2 > 0 ? sqrtf(i2) : 0;
    int s[2][2] = {{ifloor(CX - xo - 1), iceil(CX - xi)}, {ifloor(CX + xi - 1), iceil(CX + xo)}};
    if (s[0][1] >= s[1][0]) s[0][1] = s[1][1], s[1][0] = s[1][1] + 1;
    int row = y * W;
    for (auto &st : s) {
      int from = st[0] < a.x0 ? a.x0 : st[0], to = st[1] > a.x1 ? a.x1 : st[1];
      for (int x = from; x <= to; x++) pixel(row + x);
    }
  }
}

}  // namespace

void arc(float r, float w, float from, float to, uint16_t c, float a0, float a1) {
  float span = to - from;
  if (span <= 0) return;
  if (span > 1) span = 1;
  Arc a;
  a.start = angle16(from), a.len = uint32_t(span * 65536);
  a.R16 = int(r * 16 + 0.5f), a.H16 = int((w / 2 + 0.5f) * 16 + 0.5f);
  a.A0 = int(a0 * 32 * 256), a.dA = int((a1 - a0) * 32 * 256);
  a.ri = r - w / 2 - 1 > 0 ? r - w / 2 - 1 : 0, a.ro = r + w / 2 + 1;
  a.c = c;
  float x0 = CX - a.ro, x1 = CX + a.ro, y0 = CY - a.ro, y1 = CY + a.ro;
  if (span <= 0.45f) {
    // A short arc: only the box round it, from its two ends and any point due
    // up, down, left or right of the centre that it passes
    x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    auto take = [&](float turn, float rad) {
      float s, c;
      sincosTurns(turn, s, c);
      float x = CX + s * rad, y = CY - c * rad;
      x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
    };
    take(from, a.ri), take(from, a.ro), take(from + span, a.ri), take(from + span, a.ro);
    for (int q = 0; q < 4; q++) {
      float d = q * 0.25f - from;
      d -= float(ifloor(d));
      if (d < span) take(q * 0.25f, a.ro);
    }
  }
  a.x0 = int16_t(ifloor(x0) < 0 ? 0 : ifloor(x0)), a.x1 = int16_t(iceil(x1) > W - 1 ? W - 1 : iceil(x1));
  a.y0 = int16_t(ifloor(y0) < 0 ? 0 : ifloor(y0)), a.y1 = int16_t(iceil(y1) > H - 1 ? H - 1 : iceil(y1));
  if (recording) return keep(a.y0, a.y1, [=] { drawArc(a); });
  drawArc(a);
}

HOT void ringAt(float x, float y, float r, float w, uint16_t c, float alpha) {
  float ro = r + w / 2 + 1, ri = r - w / 2 - 1;
  if (recording) return keep(y - ro - 1, y + ro + 1, [=] { ringAt(x, y, r, w, c, alpha); });
  // Row by row, only the band between the ring's inner and outer edges
  int y0 = ifloor(y - ro), y1 = iceil(y + ro);
  if (y0 < clipY0) y0 = clipY0;
  if (y1 > clipY1 - 1) y1 = clipY1 - 1;
  for (int py = y0; py <= y1; py++) {
    float dy = py + 0.5f - y, o2 = ro * ro - dy * dy;
    if (o2 <= 0) continue;
    float xo = sqrtf(o2), i2 = ri * ri - dy * dy, xi = ri > 0 && i2 > 0 ? sqrtf(i2) : 0;
    int spans[2][2] = {{ifloor(x - xo - 1), iceil(x - xi)}, {ifloor(x + xi - 1), iceil(x + xo)}};
    if (spans[0][1] >= spans[1][0]) spans[0][1] = spans[1][1], spans[1][0] = spans[1][1] + 1;
    for (auto &s : spans)
      for (int px = s[0] < 0 ? 0 : s[0]; px <= s[1] && px < W; px++) {
        float dx = px + 0.5f - x;
        float cov = clamp01(w / 2 + 0.5f - fabsf(sqrtf(dx * dx + dy * dy) - r));
        if (cov > 0) blend(py * W + px, c, uint32_t(cov * alpha * 32));
      }
  }
}

HOT void discAt(float x, float y, float r, uint16_t c, float alpha) {
  if (recording) return keep(y - r - 1, y + r + 1, [=] { discAt(x, y, r, c, alpha); });
  // Row by row: solid between the edges, worked out per pixel only at them
  float in2 = (r - 0.5f) * (r - 0.5f), out = r + 0.5f;
  uint32_t full = uint32_t(clamp01(alpha) * 32 + 0.5f);
  int y0 = ifloor(y - out), y1 = iceil(y + out);
  if (y0 < clipY0) y0 = clipY0;
  if (y1 > clipY1 - 1) y1 = clipY1 - 1;
  for (int py = y0; py <= y1; py++) {
    float dy = py + 0.5f - y, o2 = out * out - dy * dy;
    if (o2 <= 0) continue;
    float xo = sqrtf(o2);
    int x0 = ifloor(x - xo), x1 = iceil(x + xo);
    if (x0 < 0) x0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    // Pixels whose centres lie within r - 0.5 are wholly inside
    int s0 = x1 + 1, s1 = x1;
    float i2 = in2 - dy * dy;
    if (r > 0.5f && i2 >= 0) {
      float xi = sqrtf(i2);
      s0 = iceil(x - xi - 0.5f), s1 = ifloor(x + xi - 0.5f);
      // Rounding can put the edge one pixel either way: check those two
      if (s0 < x0) s0 = x0;
      if (s1 > x1) s1 = x1;
      if (s0 <= s1) {
        float d0 = s0 + 0.5f - x, d1 = s1 + 0.5f - x;
        if (d0 * d0 + dy * dy > in2) s0++;
        if (d1 * d1 + dy * dy > in2) s1--;
      }
    }
    int row = py * W;
    auto edge = [&](int px) {
      float dx = px + 0.5f - x, d2 = dx * dx + dy * dy;
      if (d2 >= out * out) return;
      if (d2 <= in2)
        blend(row + px, c, full);
      else
        blend(row + px, c, uint32_t(clamp01(r + 0.5f - sqrtf(d2)) * alpha * 32));
    };
    if (s0 > s1) {
      for (int px = x0; px <= x1; px++) edge(px);
      continue;
    }
    for (int px = x0; px < s0; px++) edge(px);
    blendRun(row + s0, row + s1 + 1, c, full);
    for (int px = s1 + 1; px <= x1; px++) edge(px);
  }
}

namespace {

// A glow as recording works it out: its falloff by squared distance, and how
// far out that is still worth drawing
struct Glow {
  float x, y, r, reach2, hole;
  const uint8_t *fall;
  int reach;
  uint16_t c;
};

HOT void drawGlow(const Glow &g) {
  float scale = 256 / (g.r * g.r), rr = sqrtf(g.reach2);
  int y0 = ifloor(g.y - rr), y1 = iceil(g.y + rr);
  if (y0 < clipY0) y0 = clipY0;
  if (y1 > clipY1 - 1) y1 = clipY1 - 1;
  // The colour added at each strength
  uint32_t cs = spread(g.c), add[33];
  for (uint32_t a = 0; a <= 32; a++) add[a] = cs * a >> 5 & 0x07E0F81F;
  for (int py = y0; py <= y1; py++) {
    float dy = py + 0.5f - g.y, dy2 = dy * dy, o2 = g.reach2 - dy2;
    if (o2 <= 0) continue;
    // The row's stretch within reach, one pixel generous each side; the
    // table's end stops anything past it
    float xo = sqrtf(o2);
    int x0 = ifloor(g.x - xo - 0.5f), x1 = iceil(g.x + xo - 0.5f);
    if (x0 < 0) x0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    // Skip the hole, from the first pixel to the last wholly inside it
    int h0 = x1 + 1, h1 = x1;
    float h2 = g.hole * g.hole - dy2;
    if (h2 > 0) {
      float xh = sqrtf(h2);
      h0 = iceil(g.x - xh + 0.5f), h1 = ifloor(g.x + xh - 1.5f);
    }
    uint16_t *row = fb + py * W;
    float dx = x0 + 0.5f - g.x;
    for (int px = x0; px <= x1; px++, dx += 1) {
      if (px == h0 && h1 >= h0) {
        dx += float(h1 - px);
        px = h1;
        continue;
      }
      int k = int((dx * dx + dy2) * scale);
      if (k >= g.reach) continue;
      uint32_t a = g.fall[k];
      if (!a) continue;
      uint32_t r = addSpread(spread(swap(row[px])), add[a]);
      row[px] = swap(uint16_t(r | r >> 16));
    }
  }
}

}  // namespace

void glowAt(float x, float y, float r, uint16_t c, float alpha, float hole) {
  // The falloff (1 - d^2/r^2)^3 by squared distance, and how far it is still
  // worth drawing: past where it adds less than one step of c's channels
  uint8_t own[257], *fall = recording ? (uint8_t *)scratch(257) : own;
  if (!fall) return;
  for (int k = 0; k <= 256; k++) {
    float v = 1 - k / 256.f;
    fall[k] = uint8_t(clamp01(alpha) * v * v * v * 32 + 0.5f);
  }
  int reach = 256;
  while (reach > 0 && !(spread(c) * fall[reach - 1] >> 5 & 0x07E0F81F)) reach--;
  if (!reach) return;
  float rr = r * sqrtf(reach / 256.f);
  Glow g = {x, y, r, rr * rr, hole, fall, reach, c};
  if (recording) return keep(y - rr - 1, y + rr + 1, [=] { drawGlow(g); });
  drawGlow(g);
}

HOT void vignette(float r0, float r1, uint16_t c, float alpha) {
  int R0 = int(r0 * 16), span = int((r1 - r0) * 16);
  uint32_t A = uint32_t(clamp01(alpha) * 32 * 256);
  if (!A || span <= 0) return;
  if (recording) return keep(CY - r1 - 1, CY + r1 + 1, [=] { vignette(r0, r1, c, alpha); });
  annulus(r0, r1, [&](int i) {
    int d = dist16(pol[i]) - R0;
    if (d <= 0) return;
    if (d > span) d = span;
    uint32_t t = uint32_t(d) * 256 / uint32_t(span);  // 0..256
    add(i, c, (t * t >> 8) * A >> 16);
  });
}

HOT void roundRect(float x0, float y0, float x1, float y1, float radius, uint16_t c, float alpha) {
  if (recording) return keep(y0 - 1, y1 + 1, [=] { roundRect(x0, y0, x1, y1, radius, c, alpha); });
  int px0 = ifloor(x0), px1 = iceil(x1), py0 = ifloor(y0), py1 = iceil(y1);
  if (px0 < 0) px0 = 0;
  if (py0 < clipY0) py0 = clipY0;
  if (px1 > W) px1 = W;
  if (py1 > clipY1) py1 = clipY1;
  // Columns clear of the corners and a pixel in from the sides take the
  // row's own coverage, even on a corner's rows, so they go in one run
  int m0 = iceil(fmaxf(x0 + radius - 0.5f, x0)), m1 = ifloor(fminf(x1 - radius - 0.5f, x1 - 1));
  if (m0 < px0) m0 = px0;
  if (m1 > px1 - 1) m1 = px1 - 1;
  for (int y = py0; y < py1; y++) {
    float cy = y + 0.5f, dy = cy < y0 + radius ? y0 + radius - cy : cy > y1 - radius ? cy - (y1 - radius) : 0;
    float top = cy - y0 + 0.5f, bottom = y1 - cy + 0.5f, vy = clamp01(top < bottom ? top : bottom);
    auto pixel = [&](int x) {
      float cx = x + 0.5f, dx = cx < x0 + radius ? x0 + radius - cx : cx > x1 - radius ? cx - (x1 - radius) : 0;
      float cov;
      if (dx > 0 && dy > 0) {
        cov = clamp01(radius + 0.5f - sqrtf(dx * dx + dy * dy));
      } else {
        float left = cx - x0 + 0.5f, right = x1 - cx + 0.5f, vx = clamp01(left < right ? left : right);
        cov = vy < vx ? vy : vx;
      }
      if (cov > 0) blend(y * W + x, c, uint32_t(cov * alpha * 32));
    };
    if (m0 > m1) {
      for (int x = px0; x < px1; x++) pixel(x);
      continue;
    }
    for (int x = px0; x < m0; x++) pixel(x);
    if (vy > 0) blendRun(y * W + m0, y * W + m1 + 1, c, uint32_t(vy * alpha * 32));
    for (int x = m1 + 1; x < px1; x++) pixel(x);
  }
}

}  // namespace gfx
