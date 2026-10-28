#ifndef __clang__
#pragma GCC optimize("O2")
#endif

#include "gfx.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace gfx {

uint16_t *fb;
uint32_t *pol;

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
  if (!pol) return false;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float dx = x + 0.5f - CX, dy = y + 0.5f - CY;
      uint32_t d = uint32_t(sqrtf(dx * dx + dy * dy) * 16 + 0.5f);
      pol[y * W + x] = (d > 0xFFFF ? 0xFFFF : d) | angle16(atan2f(dx, -dy) / TAU) << 16;
    }
  return true;
}

void target(uint16_t *frame) { fb = frame; }

void clear() {
  rows(0, H, [](int row, int x0, int x1) { memset(fb + row + x0, 0, (x1 - x0) * 2); });
}

void dim(float k) {
  uint32_t a = uint32_t(clamp01(k) * 32 + 0.5f);
  if (a >= 32) return;
  rows(0, H, [&](int row, int x0, int x1) {
    for (int i = row + x0; i < row + x1; i++) {
      uint32_t r = spread(swap(fb[i])) * a >> 5 & 0x07E0F81F;
      fb[i] = swap(uint16_t(r | r >> 16));
    }
  });
}

void ring(float r, float w, uint16_t c, float alpha, int dashes, float on, float rot) {
  int R16 = int(r * 16 + 0.5f), H16 = int((w / 2 + 0.5f) * 16 + 0.5f), A = int(alpha * 32 + 0.5f);
  if (A <= 0) return;
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

void arc(float r, float w, float from, float to, uint16_t c, float a0, float a1) {
  float span = to - from;
  if (span <= 0) return;
  if (span > 1) span = 1;
  uint32_t start = angle16(from), len = uint32_t(span * 65536);
  int R16 = int(r * 16 + 0.5f), H16 = int((w / 2 + 0.5f) * 16 + 0.5f);
  int A0 = int(a0 * 32 * 256), dA = int((a1 - a0) * 32 * 256);
  annulus(r - w / 2 - 1, r + w / 2 + 1, [&](int i) {
    uint32_t p = pol[i];
    uint32_t along = ((p >> 16) - start) & 0xFFFF;
    if (along >= len) return;
    int cov = H16 - abs(dist16(p) - R16);
    if (cov <= 0) return;
    if (cov > 16) cov = 16;
    int a = A0 + int(int64_t(dA) * along / len);  // alpha in 1/256 steps of 0..32
    if (a <= 0) return;
    blend(i, c, uint32_t(cov * a) >> 12);
  });
}

void ringAt(float x, float y, float r, float w, uint16_t c, float alpha) {
  around(x, y, r + w / 2 + 1, [&](int i, float d2) {
    float cov = clamp01(w / 2 + 0.5f - fabsf(sqrtf(d2) - r));
    if (cov > 0) blend(i, c, uint32_t(cov * alpha * 32));
  });
}

void discAt(float x, float y, float r, uint16_t c, float alpha) {
  float in2 = (r - 0.5f) * (r - 0.5f);
  uint32_t full = uint32_t(clamp01(alpha) * 32 + 0.5f);
  around(x, y, r + 0.5f, [&](int i, float d2) {
    if (d2 <= in2)
      blend(i, c, full);
    else
      blend(i, c, uint32_t(clamp01(r + 0.5f - sqrtf(d2)) * alpha * 32));
  });
}

void glowAt(float x, float y, float r, uint16_t c, float alpha) {
  float inv = 1 / (r * r);
  around(x, y, r, [&](int i, float d2) {
    float k = 1 - d2 * inv;
    add(i, c, uint32_t(alpha * k * k * k * 32));
  });
}

void vignette(float r0, float r1, uint16_t c, float alpha) {
  int R0 = int(r0 * 16), span = int((r1 - r0) * 16);
  uint32_t A = uint32_t(clamp01(alpha) * 32 * 256);
  if (!A || span <= 0) return;
  annulus(r0, r1, [&](int i) {
    int d = dist16(pol[i]) - R0;
    if (d <= 0) return;
    if (d > span) d = span;
    uint32_t t = uint32_t(d) * 256 / uint32_t(span);  // 0..256
    add(i, c, (t * t >> 8) * A >> 16);
  });
}

void roundRect(float x0, float y0, float x1, float y1, float radius, uint16_t c, float alpha) {
  int px0 = int(floorf(x0)), px1 = int(ceilf(x1)), py0 = int(floorf(y0)), py1 = int(ceilf(y1));
  if (px0 < 0) px0 = 0;
  if (py0 < 0) py0 = 0;
  if (px1 > W) px1 = W;
  if (py1 > H) py1 = H;
  for (int y = py0; y < py1; y++) {
    float cy = y + 0.5f, dy = cy < y0 + radius ? y0 + radius - cy : cy > y1 - radius ? cy - (y1 - radius) : 0;
    float vy = clamp01(fminf(cy - y0 + 0.5f, y1 - cy + 0.5f));
    for (int x = px0; x < px1; x++) {
      float cx = x + 0.5f, dx = cx < x0 + radius ? x0 + radius - cx : cx > x1 - radius ? cx - (x1 - radius) : 0;
      float cov;
      if (dx > 0 && dy > 0)
        cov = clamp01(radius + 0.5f - sqrtf(dx * dx + dy * dy));
      else
        cov = fminf(vy, clamp01(fminf(cx - x0 + 0.5f, x1 - cx + 0.5f)));
      if (cov > 0) blend(y * W + x, c, uint32_t(cov * alpha * 32));
    }
  }
}

}  // namespace gfx
