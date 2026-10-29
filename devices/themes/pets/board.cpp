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
uint8_t lit[N];           // rows with any cell lit, so render() can skip the rest
int16_t span[N][2];       // each row's cells inside the round, [i0, i1)
float lvl = 1;
int dxNudge, dyNudge;
int clip0, clip1 = N, clipFade;  // the columns lit from here on, and their fading edges

// How much of a lit cell's colour each of its 4 x 4 pixels takes, in 32nds:
// a round dot two pixels across with a soft edge, a hairline gap to the next
const uint8_t DOT[4][4] = {{2, 12, 12, 2}, {12, 32, 32, 12}, {12, 32, 32, 12}, {2, 12, 12, 2}};

// The 3x5 capitals, and every other printable ASCII character, since
// commands and paths are shown as they are: rows top to bottom, '#' lit
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
    {'&', ".#. #.# .#. #.# .##"}, {'_', "... ... ... ... ###"}, {'<', "..# .#. #.. .#. ..#"},
    {'>', "#.. .#. ..# .#. #.."}, {'|', ".#. .#. .#. .#. .#."}, {'"', "#.# #.# ... ... ..."},
    {'$', ".## ##. .#. .## ##."}, {',', "... ... ... .#. #.."}, {';', "... .#. ... .#. #.."},
    {'@', ".#. #.# ### #.. .##"}, {'[', "##. #.. #.. #.. ##."}, {']', ".## ..# ..# ..# .##"},
    {'{', ".## .#. ##. .#. .##"}, {'}', "##. .#. .## .#. ##."}, {'^', ".#. #.# ... ... ..."},
    {'`', "#.. .#. ... ... ..."}, {'\\', "#.. #.. .#. ..# ..#"},
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
  memset(lit, 0, sizeof lit);
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
  lit[j] = 1;
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
  // Black, then a dot for each lit cell
  memset(band, 0, size_t(y1 - y0) * gfx::W * sizeof(uint16_t));
  for (int y = y0; y < y1; y++) {
    int yy = y - ORG, j = yy >> 2;
    if (yy < 0 || j >= N || !lit[j]) continue;
    uint16_t *row = band + (y - y0) * gfx::W;
    const uint16_t *line = cells + j * N;
    const uint8_t *w = DOT[yy & 3];
    int x0 = gfx::inside[y][0], x1 = gfx::inside[y][1];
    for (int i = span[j][0]; i < span[j][1]; i++) {
      uint16_t c = line[i];
      if (!c) continue;
      uint32_t s = gfx::spread(c);
      int px = ORG + i * PITCH;
      for (int u = 0; u < PITCH; u++) {
        int x = px + u;
        if (x < x0 || x >= x1) continue;
        uint32_t p = s * w[u] >> 5 & 0x07E0F81F;
        row[x] = gfx::swap(uint16_t(p | p >> 16));
      }
    }
  }
}

}  // namespace board
