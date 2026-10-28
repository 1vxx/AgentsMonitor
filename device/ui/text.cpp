#ifndef __clang__
#pragma GCC optimize("O2")
#endif

#include "text.h"

#include <string.h>

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

const Latin *findLatin(uint32_t cp) {
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
  if (cp == ' ') return f == MONO ? 6 : SPACE + 1;
  if (const Latin *g = findLatin(cp)) return f == MONO ? 6 : g->width + 1;
  if (const uint8_t *g = findCjk(cp)) return g[4];
  return f == MONO ? 6 : findLatin('?')->width + 1;
}

struct Pen {
  int scale, clip0, clip1, fade;
  uint32_t A;  // alpha 0..256
  uint16_t color;

  // One font pixel at panel (x, y)
  void dot(int x, int y) const {
    for (int py = y; py < y + scale; py++) {
      if (py < 0 || py >= gfx::H) continue;
      for (int px = x; px < x + scale; px++) {
        if (px < clip0 || px >= clip1) continue;
        uint32_t a = A;
        if (fade) {
          int edge = px - clip0 < clip1 - 1 - px ? px - clip0 : clip1 - 1 - px;
          if (edge < fade) a = a * uint32_t(edge) / uint32_t(fade);
        }
        gfx::blend(py * gfx::W + px, color, a >> 3);
      }
    }
  }
};

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
  }
  cjk = nullptr, cjkCount = 0;
  if (data && len > 8 && !memcmp(data, "PXF1", 4)) {
    cjk = data;
    cjkCount = data[4] | data[5] << 8;
    cjkCodes = data + 8;
    cjkOffsets = cjkCodes + cjkCount * 4;
    cjkData = cjkOffsets + cjkCount * 4;
  }
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
  return w ? (w - 1) * scale : 0;  // no gap after the last letter
}

int draw(Face f, int scale, const char *s, float x, int y, uint16_t color, float alpha, int clip0, int clip1, int fade) {
  Pen pen = {scale, clip0 < 0 ? 0 : clip0, clip1 > gfx::W ? gfx::W : clip1, fade, uint32_t(gfx::clamp01(alpha) * 256), color};
  int at = int(floorf(x + 0.5f)), start = at;
  while (uint32_t cp = next(s)) {
    int adv = advance(f, cp) * scale;
    if (at >= pen.clip1 || at + adv <= pen.clip0) {
      at += adv;
      continue;
    }
    const Latin *g = cp == ' ' ? nullptr : findLatin(cp);
    const uint8_t *h = g || cp == ' ' ? nullptr : findCjk(cp);
    if (!g && !h && cp != ' ') g = findLatin('?');
    if (g) {
      int shift = f == MONO ? 0 : g->left;
      for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < 5; c++)
          if (g->rows[r] & 1 << (4 - c)) pen.dot(at + (c - shift) * scale, y + (r - BASE) * scale);
    } else if (h) {
      int w = h[0], rows = h[1], xoff = int8_t(h[2]), yoff = int8_t(h[3]), stride = (w + 7) / 8;
      const uint8_t *bits = h + 5;
      int top = -(yoff + rows);  // font pixels above the baseline to the first row
      for (int r = 0; r < rows; r++)
        for (int c = 0; c < w; c++)
          if (bits[r * stride + c / 8] & 0x80 >> (c % 8)) pen.dot(at + (xoff + c) * scale, y + (top + r) * scale);
    }
    at += adv;
  }
  return at - start;
}

void centre(Face f, int scale, const char *s, float cx, int y, uint16_t color, float alpha) {
  draw(f, scale, s, cx - width(f, scale, s) / 2.f, y, color, alpha);
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
    const char *cut = *p && space ? space : p;
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

}  // namespace text
