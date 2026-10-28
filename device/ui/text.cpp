#ifndef __clang__
#pragma GCC optimize("O2")
#endif

#include "text.h"

#include <string.h>

#include <unordered_map>
#include <vector>

#include "gfx.h"

namespace text {
namespace {

// The Latin font: 5 columns, 7 rows from the cap line to the baseline and 2
// below it for descenders. The capitals are the first demo's legend letters.
struct Art {
  uint32_t cp;
  const char *rows;  // up to 9 rows of 5, '#' for a dot, separated by spaces
};
const Art ART[] = {
    {'!', "..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#.."},
    {'"', ".#.#. .#.#. ..... ..... ..... ..... ....."},
    {'#', ".#.#. .#.#. ##### .#.#. ##### .#.#. .#.#."},
    {'$', "..#.. .#### #.#.. .###. ..#.# ####. ..#.."},
    {'%', "##... ##..# ...#. ..#.. .#... #..## ...##"},
    {'&', ".##.. #..#. #.#.. .#... #.#.# #..#. .##.#"},
    {'\'', "..#.. ..#.. ..... ..... ..... ..... ....."},
    {'(', "...#. ..#.. .#... .#... .#... ..#.. ...#."},
    {')', ".#... ..#.. ...#. ...#. ...#. ..#.. .#..."},
    {'*', "..... ..#.. #.#.# .###. #.#.# ..#.. ....."},
    {'+', "..... ..#.. ..#.. ##### ..#.. ..#.. ....."},
    {',', "..... ..... ..... ..... ..... .##.. .##.. ..#.. .#..."},
    {'-', "..... ..... ..... ##### ..... ..... ....."},
    {'.', "..... ..... ..... ..... ..... .##.. .##.."},
    {'/', "..... ....# ...#. ..#.. .#... #.... ....."},
    {'0', ".###. #...# #..## #.#.# ##..# #...# .###."},
    {'1', "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###."},
    {'2', ".###. #...# ....# ...#. ..#.. .#... #####"},
    {'3', "##### ...#. ..#.. ...#. ....# #...# .###."},
    {'4', "...#. ..##. .#.#. #..#. ##### ...#. ...#."},
    {'5', "##### #.... ####. ....# ....# #...# .###."},
    {'6', "..##. .#... #.... ####. #...# #...# .###."},
    {'7', "##### ....# ...#. ..#.. .#... .#... .#..."},
    {'8', ".###. #...# #...# .###. #...# #...# .###."},
    {'9', ".###. #...# #...# .#### ....# ...#. .##.."},
    {':', "..... .##.. .##.. ..... .##.. .##.. ....."},
    {';', "..... .##.. .##.. ..... .##.. .##.. ..#.. .#..."},
    {'<', "...#. ..#.. .#... #.... .#... ..#.. ...#."},
    {'=', "..... ..... ##### ..... ##### ..... ....."},
    {'>', ".#... ..#.. ...#. ....# ...#. ..#.. .#..."},
    {'?', ".###. #...# ....# ...#. ..#.. ..... ..#.."},
    {'@', ".###. #...# ....# .##.# #.#.# #.#.# .###."},
    {'A', ".###. #...# #...# ##### #...# #...# #...#"},
    {'B', "####. #...# #...# ####. #...# #...# ####."},
    {'C', ".###. #...# #.... #.... #.... #...# .###."},
    {'D', "####. #...# #...# #...# #...# #...# ####."},
    {'E', "##### #.... #.... ####. #.... #.... #####"},
    {'F', "##### #.... #.... ####. #.... #.... #...."},
    {'G', ".###. #...# #.... #.### #...# #...# .####"},
    {'H', "#...# #...# #...# ##### #...# #...# #...#"},
    {'I', ".###. ..#.. ..#.. ..#.. ..#.. ..#.. .###."},
    {'J', "..### ...#. ...#. ...#. ...#. #..#. .##.."},
    {'K', "#...# #..#. #.#.. ##... #.#.. #..#. #...#"},
    {'L', "#.... #.... #.... #.... #.... #.... #####"},
    {'M', "#...# ##.## #.#.# #.#.# #...# #...# #...#"},
    {'N', "#...# #...# ##..# #.#.# #..## #...# #...#"},
    {'O', ".###. #...# #...# #...# #...# #...# .###."},
    {'P', "####. #...# #...# ####. #.... #.... #...."},
    {'Q', ".###. #...# #...# #...# #.#.# #..#. .##.#"},
    {'R', "####. #...# #...# ####. #.#.. #..#. #...#"},
    {'S', ".#### #.... #.... .###. ....# ....# ####."},
    {'T', "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#.."},
    {'U', "#...# #...# #...# #...# #...# #...# .###."},
    {'V', "#...# #...# #...# #...# #...# .#.#. ..#.."},
    {'W', "#...# #...# #...# #.#.# #.#.# #.#.# .#.#."},
    {'X', "#...# #...# .#.#. ..#.. .#.#. #...# #...#"},
    {'Y', "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#.."},
    {'Z', "##### ....# ...#. ..#.. .#... #.... #####"},
    {'[', ".###. .#... .#... .#... .#... .#... .###."},
    {'\\', "..... #.... .#... ..#.. ...#. ....# ....."},
    {']', ".###. ...#. ...#. ...#. ...#. ...#. .###."},
    {'^', "..#.. .#.#. #...# ..... ..... ..... ....."},
    {'_', "..... ..... ..... ..... ..... ..... ..... #####"},
    {'`', ".#... ..#.. ..... ..... ..... ..... ....."},
    {'a', "..... ..... .###. ....# .#### #...# .####"},
    {'b', "#.... #.... #.##. ##..# #...# #...# ####."},
    {'c', "..... ..... .###. #.... #.... #...# .###."},
    {'d', "....# ....# .##.# #..## #...# #...# .####"},
    {'e', "..... ..... .###. #...# ##### #.... .###."},
    {'f', "..##. .#... .#... ###.. .#... .#... .#..."},
    {'g', "..... ..... .#### #...# #...# #...# .#### ....# .###."},
    {'h', "#.... #.... #.##. ##..# #...# #...# #...#"},
    {'i', "..#.. ..... .##.. ..#.. ..#.. ..#.. .###."},
    {'j', "...#. ..... ..##. ...#. ...#. ...#. ...#. #..#. .##.."},
    {'k', "#.... #.... #..#. #.#.. ##... #.#.. #..#."},
    {'l', ".##.. ..#.. ..#.. ..#.. ..#.. ..#.. .###."},
    {'m', "..... ..... ##.#. #.#.# #.#.# #.#.# #.#.#"},
    {'n', "..... ..... #.##. ##..# #...# #...# #...#"},
    {'o', "..... ..... .###. #...# #...# #...# .###."},
    {'p', "..... ..... ####. #...# #...# #...# ####. #.... #...."},
    {'q', "..... ..... .#### #...# #...# #...# .#### ....# ....#"},
    {'r', "..... ..... #.##. ##..# #.... #.... #...."},
    {'s', "..... ..... .###. #.... .###. ....# ####."},
    {'t', ".#... .#... ###.. .#... .#... .#... ..##."},
    {'u', "..... ..... #...# #...# #...# #..## .##.#"},
    {'v', "..... ..... #...# #...# #...# .#.#. ..#.."},
    {'w', "..... ..... #...# #...# #.#.# #.#.# .#.#."},
    {'x', "..... ..... #...# .#.#. ..#.. .#.#. #...#"},
    {'y', "..... ..... #...# #...# #...# #...# .#### ....# .###."},
    {'z', "..... ..... ##### ...#. ..#.. .#... #####"},
    {'{', "...#. ..#.. ..#.. .#... ..#.. ..#.. ...#."},
    {'|', "..#.. ..#.. ..#.. ..#.. ..#.. ..#.. ..#.."},
    {'}', ".#... ..#.. ..#.. ...#. ..#.. ..#.. .#..."},
    {'~', "..... ..... .#... #.#.# ...#. ..... ....."},
    {0x00B7, "..... ..... ..... .##.. .##.. ..... ....."},  // ·
    {0x00D7, "..... #...# .#.#. ..#.. .#.#. #...# ....."},  // ×
    {0x2014, "..... ..... ..... ##### ..... ..... ....."},  // —
    {0x2026, "..... ..... ..... ..... ..... ..... #.#.#"},  // …
    {0x2192, "..... ..#.. ...#. ##### ...#. ..#.. ....."},  // →
    {0x2726, "..#.. ..#.. .###. ##### .###. ..#.. ..#.."},  // ✦
};
constexpr int ROWS = 9, BASE = 7;  // rows in a glyph; the baseline sits under row 6

struct Latin {
  uint32_t cp;
  uint8_t rows[ROWS];  // bit 4 is the leftmost column
  uint8_t left, width;
};
Latin latin[sizeof ART / sizeof *ART];
constexpr int SPACE = 3;  // columns a space takes

// The hanzi font, cjk10.bin (see device/fonts/make_cjk.py)
const uint8_t *cjk;
int cjkCount;
const uint8_t *cjkCodes, *cjkOffsets, *cjkData;

inline uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

const Latin *ascii[128];  // ASCII straight to its glyph

const Latin *findLatin(uint32_t cp) {
  if (cp < 128) return ascii[cp];
  for (const Latin &g : latin)
    if (g.cp == cp) return &g;
  return nullptr;
}

const uint8_t *findCjk(uint32_t cp) {
  int lo = 0, hi = cjkCount - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t c = u32(cjkCodes + mid * 4);
    if (c == cp) return cjkData + u32(cjkOffsets + mid * 4);
    if (c < cp)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return nullptr;
}

// How far one character moves the pen, in font pixels
int advance(Face f, uint32_t cp) {
  int track = f == LABEL ? 1 : 0;
  if (cp == ' ') return f == MONO ? 6 : SPACE + 1 + track;
  if (const Latin *g = findLatin(cp)) return f == MONO ? 6 : g->width + 1 + track;
  if (const uint8_t *g = findCjk(cp)) return g[4] + track;
  return f == MONO ? 6 : findLatin('?')->width + 1 + track;
}

// ---------- Lit glyphs ----------

// A glyph at one size: for each panel pixel, how much of an LED dot covers it
// and how much glow reaches it. Placed relative to the pen and the baseline.
struct Sprite {
  int16_t x0, y0, w, h;
  uint32_t off;  // into pool: w * h pairs of (dot, glow), 0..255, then each
                 // row's first and last lit column
};
std::unordered_map<uint32_t, Sprite> sprites;
std::vector<uint8_t> pool;
constexpr size_t POOL_MAX = 6 << 20;
constexpr float GLOW = 0.5f;  // how bright the glow is against the dots

// Lists the glyph's dots, in font pixels from the pen and the baseline
int dots(Face f, uint32_t cp, int16_t (*out)[2], int max) {
  int n = 0;
  const Latin *g = cp == ' ' ? nullptr : findLatin(cp);
  const uint8_t *h = g || cp == ' ' ? nullptr : findCjk(cp);
  if (!g && !h && cp != ' ') g = findLatin('?');
  if (g) {
    int shift = f == MONO ? 0 : g->left;
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < 5; c++)
        if (g->rows[r] & 1 << (4 - c) && n < max) out[n][0] = int16_t(c - shift), out[n++][1] = int16_t(r - BASE);
  } else if (h) {
    int w = h[0], rows = h[1], xoff = int8_t(h[2]), yoff = int8_t(h[3]), stride = (w + 7) / 8;
    const uint8_t *bits = h + 5;
    int top = -(yoff + rows);
    for (int r = 0; r < rows; r++)
      for (int c = 0; c < w; c++)
        if (bits[r * stride + c / 8] & 0x80 >> (c % 8) && n < max) out[n][0] = int16_t(xoff + c), out[n++][1] = int16_t(top + r);
  }
  return n;
}

const Sprite &sprite(Face f, int scale, uint32_t cp) {
  Face shape = f == LABEL ? PROP : f;  // tracking moves the pen, not the dots
  uint32_t key = uint32_t(shape) << 30 | uint32_t(scale & 0x3F) << 24 | (cp & 0xFFFFFF);
  auto hit = sprites.find(key);
  if (hit != sprites.end()) return hit->second;
  if (pool.size() > POOL_MAX) sprites.clear(), pool.clear();

  static int16_t at[200][2];
  int n = dots(shape, cp, at, 200);
  Sprite s = {0, 0, 0, 0, uint32_t(pool.size())};
  if (!n) return sprites.emplace(key, s).first->second;

  int x0 = at[0][0], x1 = x0, y0 = at[0][1], y1 = y0;
  for (int k = 1; k < n; k++) {
    x0 = at[k][0] < x0 ? at[k][0] : x0, x1 = at[k][0] > x1 ? at[k][0] : x1;
    y0 = at[k][1] < y0 ? at[k][1] : y0, y1 = at[k][1] > y1 ? at[k][1] : y1;
  }
  float sigma = 0.85f * scale;
  int pad = int(ceilf(sigma * 2.5f)) + 1;
  s.x0 = int16_t(x0 * scale - pad), s.y0 = int16_t(y0 * scale - pad);
  s.w = int16_t((x1 - x0 + 1) * scale + 2 * pad), s.h = int16_t((y1 - y0 + 1) * scale + 2 * pad);
  int W = s.w, H = s.h;

  // Round dots, each antialiased from a 4x4 sample grid
  std::vector<float> core(W * H, 0.f), glow(W * H, 0.f), tmp(W * H, 0.f);
  float rd = scale * 0.5f - 0.22f;
  for (int k = 0; k < n; k++) {
    float cx = (at[k][0] + 0.5f) * scale - s.x0, cy = (at[k][1] + 0.5f) * scale - s.y0;
    for (int py = int(cy - rd - 1); py <= int(cy + rd + 1); py++)
      for (int px = int(cx - rd - 1); px <= int(cx + rd + 1); px++) {
        if (px < 0 || py < 0 || px >= W || py >= H) continue;
        int hits = 0;
        for (int sy = 0; sy < 4; sy++)
          for (int sx = 0; sx < 4; sx++) {
            float dx = px + (sx + 0.5f) / 4 - cx, dy = py + (sy + 0.5f) / 4 - cy;
            hits += dx * dx + dy * dy <= rd * rd;
          }
        float c = hits / 16.f;
        if (c > core[py * W + px]) core[py * W + px] = c;
      }
  }
  // The glow: the dots blurred, in two passes
  int R = pad;
  std::vector<float> kernel(2 * R + 1);
  float sum = 0;
  for (int k = -R; k <= R; k++) sum += kernel[k + R] = expf(-k * k / (2 * sigma * sigma));
  for (float &k : kernel) k /= sum;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float a = 0;
      for (int k = -R; k <= R; k++)
        if (x + k >= 0 && x + k < W) a += core[y * W + x + k] * kernel[k + R];
      tmp[y * W + x] = a;
    }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float a = 0;
      for (int k = -R; k <= R; k++)
        if (y + k >= 0 && y + k < H) a += tmp[(y + k) * W + x] * kernel[k + R];
      glow[y * W + x] = a;
    }
  pool.resize(pool.size() + W * H * 2 + H * 2);
  uint8_t *out = &pool[s.off], *spans = out + W * H * 2;
  for (int i = 0; i < W * H; i++) {
    out[i * 2] = uint8_t(core[i] * 255 + 0.5f);
    float g = glow[i] * 1.8f;
    uint8_t v = uint8_t((g > 1 ? 1 : g) * 255 + 0.5f);
    out[i * 2 + 1] = v < 6 ? 0 : v;  // the glow's last faint ring is not worth drawing
  }
  // Each row's lit stretch, so drawing skips the empty ends
  for (int y = 0; y < H; y++) {
    int a = W, b = -1;
    for (int x = 0; x < W; x++)
      if (out[(y * W + x) * 2] || out[(y * W + x) * 2 + 1]) a = x < a ? x : a, b = x;
    spans[y * 2] = uint8_t(a > 255 ? 255 : a), spans[y * 2 + 1] = uint8_t(b < 0 ? 0 : b);
    if (b < 0) spans[y * 2] = 1, spans[y * 2 + 1] = 0;
  }
  return sprites.emplace(key, s).first->second;
}

int rowClip0 = 0, rowClip1 = gfx::H;

void put(char *&out, char *end, uint32_t cp) {
  char buf[4];
  int n;
  if (cp < 0x80)
    buf[0] = char(cp), n = 1;
  else if (cp < 0x800)
    buf[0] = char(0xC0 | cp >> 6), buf[1] = char(0x80 | (cp & 63)), n = 2;
  else if (cp < 0x10000)
    buf[0] = char(0xE0 | cp >> 12), buf[1] = char(0x80 | (cp >> 6 & 63)), buf[2] = char(0x80 | (cp & 63)), n = 3;
  else
    buf[0] = char(0xF0 | cp >> 18), buf[1] = char(0x80 | (cp >> 12 & 63)), buf[2] = char(0x80 | (cp >> 6 & 63)),
    buf[3] = char(0x80 | (cp & 63)), n = 4;
  if (out + n >= end) return;
  memcpy(out, buf, n);
  out += n;
  *out = 0;
}

constexpr uint32_t ELLIPSIS = 0x2026;

}  // namespace

bool init(const uint8_t *data, size_t len) {
  int n = 0;
  for (const Art &a : ART) {
    Latin &g = latin[n++];
    g = {};
    g.cp = a.cp;
    const char *p = a.rows;
    for (int r = 0; r < ROWS && *p; r++) {
      for (int c = 0; c < 5 && *p; c++, p++)
        if (*p == '#') g.rows[r] |= 1 << (4 - c);
      while (*p == ' ') p++;
    }
    // Letters are as wide as their ink; digits keep five columns so timers hold still
    uint8_t ink = 0;
    for (uint8_t row : g.rows) ink |= row;
    int left = 0, right = 4;
    while (left < 4 && !(ink & 1 << (4 - left))) left++;
    while (right > left && !(ink & 1 << (4 - right))) right--;
    bool digit = g.cp >= '0' && g.cp <= '9';
    g.left = digit ? 0 : uint8_t(left);
    g.width = digit ? 5 : uint8_t(right - left + 1);
    if (g.cp < 128) ascii[g.cp] = &g;
  }
  cjk = nullptr, cjkCount = 0;
  if (data && len > 8 && !memcmp(data, "PXF1", 4)) {
    cjk = data;
    cjkCount = data[4] | data[5] << 8;
    cjkCodes = data + 8;
    cjkOffsets = cjkCodes + cjkCount * 4;
    cjkData = cjkOffsets + cjkCount * 4;
  }
  sprites.clear(), pool.clear();
  return true;
}

uint32_t next(const char *&s) {
  const uint8_t *p = (const uint8_t *)s;
  uint32_t c = *p;
  if (!c) return 0;
  int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
  if (n) c &= 0x3F >> n;
  for (int k = 1; k <= n; k++) {
    if ((p[k] & 0xC0) != 0x80) {
      s += k;
      return 0xFFFD;
    }
    c = c << 6 | (p[k] & 0x3F);
  }
  s += n + 1;
  return c;
}

int width(Face f, int scale, const char *s) {
  int w = 0;
  while (uint32_t cp = next(s)) w += advance(f, cp);
  return w ? (w - 1 - (f == LABEL)) * scale : 0;  // no gap after the last letter
}

void clipRows(int y0, int y1) {
  rowClip0 = y0 < 0 ? 0 : y0;
  rowClip1 = y1 > gfx::H ? gfx::H : y1;
}

int draw(Face f, int scale, const char *s, float x, int y, uint16_t color, float alpha, int clip0, int clip1, int fade) {
  if (clip0 < 0) clip0 = 0;
  if (clip1 > gfx::W) clip1 = gfx::W;
  uint32_t A = uint32_t(gfx::clamp01(alpha) * 256);
  if (!A) return 0;
  // A string clear of the rows being drawn: hanzi reach 11 dots above the
  // baseline, descenders 2 below, the glow 3 sigma past either
  int pad = 3 * scale + 1;
  if (y + 2 * scale + pad < gfx::clipY0 || y - 11 * scale - pad >= gfx::clipY1) return 0;
  uint32_t G = uint32_t(GLOW * 256), C = gfx::spread(color);
  int at = gfx::ifloor(x + 0.5f), start = at;
  while (uint32_t cp = next(s)) {
    int adv = advance(f, cp) * scale;
    const Sprite &sp = sprite(f, scale, cp);
    int gx = at + sp.x0, gy = y + sp.y0;
    at += adv;
    int r0 = rowClip0 > gfx::clipY0 ? rowClip0 : gfx::clipY0, r1 = rowClip1 < gfx::clipY1 ? rowClip1 : gfx::clipY1;
    if (!sp.w || gx >= clip1 || gx + sp.w <= clip0 || gy >= r1 || gy + sp.h <= r0) continue;
    const uint8_t *px = &pool[sp.off], *spans = px + sp.w * sp.h * 2;
    for (int r = 0; r < sp.h; r++) {
      int py = gy + r;
      if (py < r0 || py >= r1) continue;
      const uint8_t *row = px + r * sp.w * 2;
      int c0 = spans[r * 2], c1 = spans[r * 2 + 1];
      if (gx + c0 < clip0) c0 = clip0 - gx;
      if (gx + c1 >= clip1) c1 = clip1 - 1 - gx;
      uint16_t *line = gfx::fb + py * gfx::W + gx;
      for (int c = c0; c <= c1; c++) {
        uint32_t dot = row[c * 2], glow = row[c * 2 + 1];
        if (!dot && !glow) continue;
        uint32_t a = A;
        if (fade) {
          int pxx = gx + c, edge = pxx - clip0 < clip1 - 1 - pxx ? pxx - clip0 : clip1 - 1 - pxx;
          if (edge < fade) a = a * uint32_t(edge) / uint32_t(fade);
        }
        // The dot, then its light over it and round it, which runs hot at the
        // centre: one read and one write for both
        uint32_t p = gfx::spread(gfx::swap(line[c]));
        if (dot) {
          uint32_t k = (dot * a + 1024) >> 11;
          p = (C * k + p * (32 - k)) >> 5 & 0x07E0F81F;
        }
        if (glow) p = gfx::addSpread(p, C * ((glow * a * G) >> 19) >> 5 & 0x07E0F81F);
        line[c] = gfx::swap(uint16_t(p | p >> 16));
      }
    }
  }
  return at - start;
}

void centre(Face f, int scale, const char *s, float cx, int y, uint16_t color, float alpha) {
  draw(f, scale, s, cx - width(f, scale, s) / 2.f, y, color, alpha);
}

void roll(Face f, int scale, const char *now, const char *was, float roll, float cx, int y, uint16_t color, float alpha) {
  if (roll >= 1 || !digitsOnly(now, was)) {
    centre(f, scale, now, cx, y, color, alpha);
    return;
  }
  float x = cx - width(f, scale, now) / 2.f;
  int lift = cap(scale) + 3 * scale;
  int keep0 = rowClip0, keep1 = rowClip1;
  clipRows(y - cap(scale) - 2 * scale, y + scale);
  const char *a = now, *b = was;
  while (*a) {
    const char *a0 = a, *b0 = b;
    uint32_t ca = next(a), cb = next(b);
    char one[8] = {0}, old[8] = {0};
    memcpy(one, a0, a - a0);
    memcpy(old, b0, b - b0);
    if (ca == cb) {
      draw(f, scale, one, x, y, color, alpha);
    } else {
      // The old digit leaves upward as the new one comes up from below
      draw(f, scale, old, x, y - int(roll * lift), color, alpha * (1 - roll));
      draw(f, scale, one, x, y + int((1 - roll) * lift), color, alpha * roll);
    }
    x += advance(f, ca) * scale;
  }
  rowClip0 = keep0, rowClip1 = keep1;
}

void fit(Face f, int scale, const char *s, int maxW, char *out, size_t outLen) {
  char *o = out, *end = out + outLen;
  *o = 0;
  if (width(f, scale, s) <= maxW) {
    const char *p = s;
    while (uint32_t cp = next(p)) put(o, end, cp);
    return;
  }
  int room = maxW / scale - advance(f, ELLIPSIS), w = 0;
  while (uint32_t cp = next(s)) {
    int a = advance(f, cp);
    if (w + a > room) break;
    w += a;
    put(o, end, cp);
  }
  while (o > out && o[-1] == ' ') *--o = 0;
  put(o, end, ELLIPSIS);
}

int wrap(Face f, int scale, const char *s, int maxW, char (*lines)[160], int maxLines) {
  int n = 0, room = maxW / scale + 1;  // +1: the last letter's gap is not drawn
  while (*s && n < maxLines) {
    while (*s == ' ') s++;
    if (!*s) break;
    if (n == maxLines - 1) {
      fit(f, scale, s, maxW, lines[n++], 160);
      break;
    }
    // Take letters while they fit, remembering the last space to break at
    const char *p = s, *space = nullptr;
    int w = 0;
    while (*p) {
      const char *q = p;
      uint32_t cp = next(q);
      int a = advance(f, cp);
      if (w + a > room && w > 0) break;
      if (cp == ' ') space = p;
      w += a;
      p = q;
    }
    // Commands and paths (MONO) break anywhere, so a line holds as much as it can
    const char *cut = *p && space && f != MONO ? space : p;
    char *o = lines[n], *end = lines[n] + 160;
    *o = 0;
    for (const char *q = s; q < cut;) put(o, end, next(q));
    s = cut;
    n++;
  }
  return n;
}

void upper(const char *s, char *out, size_t outLen) {
  size_t i = 0;
  for (; s[i] && i + 1 < outLen; i++) out[i] = s[i] >= 'a' && s[i] <= 'z' ? char(s[i] - 32) : s[i];
  out[i] = 0;
}

bool digitsOnly(const char *a, const char *b) {
  if (strlen(a) != strlen(b)) return false;
  for (; *a; a++, b++)
    if (*a != *b && !((*a >= '0' && *a <= '9') && (*b >= '0' && *b <= '9'))) return false;
  return true;
}

}  // namespace text
