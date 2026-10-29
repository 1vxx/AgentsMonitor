#ifndef __clang__
#pragma GCC optimize("O3")
#endif

#include "board.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"

namespace board {
namespace {

uint16_t *cells;          // N x N, native RGB565, 0 for dark
int16_t span[N][2];       // each row's cells inside the round, [i0, i1)
float lvl = 1;
int dxNudge, dyNudge;
int clip0, clip1 = N, clipFade;  // the columns lit from here on, and their fading edges

// How much of a lit cell's colour each of its 4 x 4 pixels takes, in 32nds:
// a round dot two pixels across with a soft edge, a hairline gap to the next
const uint8_t DOT[4][4] = {{2, 12, 12, 2}, {12, 32, 32, 12}, {12, 32, 32, 12}, {2, 12, 12, 2}};
// A dark cell's faint dot, byte-swapped as the panel takes it
uint16_t dark[4][4];
constexpr uint16_t DARK = 0x10A2;  // RGB565 of (16, 20, 16): just there on black

// The 3x5 capitals: rows top to bottom, '#' lit
struct Tiny {
  uint32_t cp;
  const char *rows;
};
const Tiny TINY[] = {
    {'A', ".#. #.# ### #.# #.#"}, {'B', "##. #.# ##. #.# ##."}, {'C', ".## #.. #.. #.. .##"},
    {'D', "##. #.# #.# #.# ##."}, {'E', "### #.. ##. #.. ###"}, {'F', "### #.. ##. #.. #.."},
    {'G', ".## #.. #.# #.# .##"}, {'H', "#.# #.# ### #.# #.#"}, {'I', "### .#. .#. .#. ###"},
    {'J', "..# ..# ..# #.# .#."}, {'K', "#.# #.# ##. #.# #.#"}, {'L', "#.. #.. #.. #.. ###"},
    {'M', "#.# ### ### #.# #.#"}, {'N', "##. #.# #.# #.# #.#"}, {'O', ".#. #.# #.# #.# .#."},
    {'P', "##. #.# ##. #.. #.."}, {'Q', ".#. #.# #.# ##. .##"}, {'R', "##. #.# ##. #.# #.#"},
    {'S', ".## #.. .#. ..# ##."}, {'T', "### .#. .#. .#. .#."}, {'U', "#.# #.# #.# #.# ###"},
    {'V', "#.# #.# #.# #.# .#."}, {'W', "#.# #.# ### ### #.#"}, {'X', "#.# #.# .#. #.# #.#"},
    {'Y', "#.# #.# .#. .#. .#."}, {'Z', "### ..# .#. #.. ###"}, {'0', "### #.# #.# #.# ###"},
    {'1', ".#. ##. .#. .#. ###"}, {'2', "##. ..# .#. #.. ###"}, {'3', "##. ..# .#. ..# ##."},
    {'4', "#.# #.# ### ..# ..#"}, {'5', "### #.. ##. ..# ##."}, {'6', ".## #.. ### #.# ###"},
    {'7', "### ..# .#. .#. .#."}, {'8', "### #.# ### #.# ###"}, {'9', "### #.# ### ..# ##."},
    {'.', "... ... ... ... .#."}, {'!', ".#. .#. .#. ... .#."}, {'?', "##. ..# .#. ... .#."},
    {'/', "..# ..# .#. #.. #.."}, {'-', "... ... ### ... ..."}, {':', "... .#. ... .#. ..."},
    {'%', "#.# ..# .#. #.. #.#"}, {'+', "... .#. ### .#. ..."}, {'~', "... ... .## ##. ..."},
    {'\'', ".#. .#. ... ... ..."}, {'(', "..# .#. .#. .#. ..#"}, {')', "#.. .#. .#. .#. #.."},
    {'#', "#.# ### #.# ### #.#"}, {'*', "... #.# .#. #.# ..."}, {'=', "... ### ... ### ..."},
    {0x00B7, "... ... .#. ... ..."},  // ·
    {0x2026, "... ... ... ... #.#"},  // …
};
uint16_t tinyBits[128];  // bit 14 is the top row's left column
uint16_t middot, ellipsis;

uint16_t parse(const char *rows) {
  uint16_t b = 0;
  int bit = 14;
  for (const char *p = rows; *p && bit >= 0; p++) {
    if (*p == ' ') continue;
    if (*p == '#') b |= 1 << bit;
    bit--;
  }
  return b;
}

uint16_t tinyOf(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  if (cp < 128) return tinyBits[cp];
  if (cp == 0x00B7) return middot;
  if (cp == 0x2026) return ellipsis;
  return tinyBits['?'];
}

}  // namespace

bool init() {
  // In PSRAM: internal RAM is kept for Wi-Fi, and rendering reads it a row at a time
  cells = (uint16_t *)gfx::bigAlloc(N * N * sizeof(uint16_t));
  if (!cells) return false;
  memset(cells, 0, N * N * sizeof(uint16_t));
  for (int j = 0; j < N; j++) {
    // Cells whose dot sits wholly inside the round
    float dy = j + 0.5f - MID, r = (gfx::CX - 3) / PITCH, o = r * r - dy * dy;
    if (o <= 0) {
      span[j][0] = span[j][1] = 0;
      continue;
    }
    float h = sqrtf(o);
    int i0 = gfx::iceil(MID - h - 0.5f), i1 = gfx::ifloor(MID + h - 0.5f) + 1;
    span[j][0] = int16_t(i0 < 0 ? 0 : i0), span[j][1] = int16_t(i1 > N ? N : i1);
  }
  for (int v = 0; v < 4; v++)
    for (int u = 0; u < 4; u++) dark[v][u] = DOT[v][u] == 32 ? gfx::swap(DARK) : 0;
  memset(tinyBits, 0, sizeof tinyBits);
  for (const Tiny &t : TINY) {
    uint16_t b = parse(t.rows);
    if (t.cp < 128) tinyBits[t.cp] = b;
    if (t.cp == 0x00B7) middot = b;
    if (t.cp == 0x2026) ellipsis = b;
  }
  return true;
}

void clear() {
  memset(cells, 0, N * N * sizeof(uint16_t));
  lvl = 1;
  dxNudge = dyNudge = 0;
  clip0 = 0, clip1 = N, clipFade = 0;
}

void level(float k) { lvl = gfx::clamp01(k); }

void nudge(int dx, int dy) { dxNudge = dx, dyNudge = dy; }

void clip(int c0, int c1, int fade) { clip0 = c0, clip1 = c1, clipFade = fade; }

void light(int i, int j, uint16_t c, float a) {
  if (i < clip0 || i >= clip1) return;
  if (clipFade) {
    int in = i - clip0 < clip1 - 1 - i ? i - clip0 : clip1 - 1 - i;
    if (in < clipFade) a *= (in + 1) / float(clipFade + 1);
  }
  i += dxNudge, j += dyNudge;
  if (j < 0 || j >= N || i < span[j][0] || i >= span[j][1]) return;
  int al = int(a * lvl * 32 + 0.5f);
  if (al <= 0) return;
  uint16_t &cell = cells[j * N + i];
  if (al >= 32) {
    cell = c;
    return;
  }
  uint32_t r = (gfx::spread(c) * al + gfx::spread(cell) * (32 - al)) >> 5 & 0x07E0F81F;
  cell = uint16_t(r | r >> 16);
}

int tinyWidth(const char *s, int k) {
  int n = 0;
  for (const char *p = s; text::next(p);) n++;
  return n ? (n * 4 - 1) * k : 0;
}

int tiny(const char *s, float x, int top, uint16_t c, float a, int k) {
  int x0 = gfx::ifloor(x + 0.5f), pen = x0;
  for (const char *p = s; uint32_t cp = text::next(p);) {
    uint16_t b = cp == ' ' ? 0 : tinyOf(cp);
    for (int r = 0; r < 5; r++)
      for (int q = 0; q < 3; q++)
        if (b & 1 << (14 - r * 3 - q))
          for (int v = 0; v < k; v++)
            for (int u = 0; u < k; u++) light(pen + q * k + u, top + r * k + v, c, a);
    pen += 4 * k;
  }
  return pen - x0 - k;
}

void tinyCentre(const char *s, float cx, int top, uint16_t c, float a, int k) {
  tiny(s, cx - tinyWidth(s, k) / 2.f, top, c, a, k);
}

void tinyFit(const char *s, int n, char *out, int outLen) {
  int count = 0;
  for (const char *p = s; text::next(p);) count++;
  if (count <= n || n < 2) {
    snprintf(out, outLen, "%s", s);
    return;
  }
  // n - 1 characters and a full stop
  const char *q = s;
  for (int k = 0; k < n - 1; k++) text::next(q);
  snprintf(out, outLen, "%.*s.", int(q - s), s);
}

int font(text::Face f, const char *s, float x, int base, uint16_t c, float a) {
  static int16_t at[200][2];
  int x0 = gfx::ifloor(x + 0.5f), pen = x0;
  for (const char *p = s; uint32_t cp = text::next(p);) {
    int n = text::glyphDots(f, cp, at, 200);
    for (int k = 0; k < n; k++) light(pen + at[k][0], base + at[k][1], c, a);
    pen += text::penAdvance(f, cp);
  }
  return pen - x0;
}

void fontCentre(text::Face f, const char *s, float cx, int base, uint16_t c, float a) {
  font(f, s, cx - (text::width(f, 1, s) - 1) / 2.f, base, c, a);
}

HOT void render(uint16_t *band, int y0, int y1) {
  for (int y = y0; y < y1; y++) {
    uint16_t *row = band + (y - y0) * gfx::W;
    int x0 = gfx::inside[y][0], x1 = gfx::inside[y][1];
    if (x1 <= x0) {
      memset(row, 0, gfx::W * sizeof(uint16_t));
      continue;
    }
    if (x0 > 0) memset(row, 0, x0 * sizeof(uint16_t));
    if (x1 < gfx::W) memset(row + x1, 0, (gfx::W - x1) * sizeof(uint16_t));
    int yy = y - ORG, j = yy >> 2;
    if (yy < 0 || j >= N) {
      memset(row + x0, 0, (x1 - x0) * sizeof(uint16_t));
      continue;
    }
    int v = yy & 3;
    const uint16_t *line = cells + j * N;
    const uint8_t *w = DOT[v];
    const uint16_t *d = dark[v];
    // Pixels left of cell 0 and right of the last are dark
    int xs = x0 < ORG ? ORG : x0, xe = x1 > ORG + N * PITCH ? ORG + N * PITCH : x1;
    for (int x = x0; x < xs; x++) row[x] = 0;
    for (int x = xe; x < x1; x++) row[x] = 0;
    int x = xs;
    // Whole cells four pixels at a time; the part cells at either end one by one
    auto one = [&](int px) {
      int u = (px - ORG) & 3;
      uint16_t c = line[(px - ORG) >> 2];
      if (!c) {
        row[px] = d[u];
        return;
      }
      uint32_t s = gfx::spread(c) * w[u] >> 5 & 0x07E0F81F;
      row[px] = gfx::swap(uint16_t(s | s >> 16));
    };
    while (x < xe && ((x - ORG) & 3)) one(x++);
    for (; x + 4 <= xe; x += 4) {
      uint16_t c = line[(x - ORG) >> 2];
      if (!c) {
        row[x] = d[0], row[x + 1] = d[1], row[x + 2] = d[2], row[x + 3] = d[3];
        continue;
      }
      uint32_t s = gfx::spread(c);
      for (int u = 0; u < 4; u++) {
        uint32_t p = s * w[u] >> 5 & 0x07E0F81F;
        row[x + u] = gfx::swap(uint16_t(p | p >> 16));
      }
    }
    while (x < xe) one(x++);
  }
}

}  // namespace board
