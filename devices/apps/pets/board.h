#pragma once

#include <stdint.h>

#include "text.h"

// The panel as a board of LED dots: 116 x 116 cells 4 px apart, each lit in
// one colour or dark. A frame lights cells; render() turns them into pixels,
// a round dot per lit cell on pure black, so whatever is drawn sits on the
// same grid and moves a whole dot at a time.
namespace board {

constexpr int N = 116, PITCH = 4, ORG = 1;  // cells across; px per cell; px left of cell 0
constexpr float MID = 57.5f;                // the panel's centre, in cells

bool init();
// Every cell dark, ready for a frame
void clear();
// How bright what is lit from here on comes out, 0..1, for fades and dimming
void level(float k);
// Moves what is lit from here on by whole cells, against burn-in
void nudge(int dx, int dy);
// Lights only columns [c0, c1) from here on, fading over `fade` cells at
// either edge, for text that scrolls through a window; clip(0, N, 0) to end
void clip(int c0, int c1, int fade);

// Lights cell (i, j) in native RGB565 c at alpha a (0..1) over what is there
void light(int i, int j, uint16_t c, float a);
// The cell a panel pixel falls in
inline int cellOf(int px) { return (px - ORG) >> 2; }

// The board's own 3x5 capitals and digits, 4 cells apart (k times that at
// size k); lower case comes out in capitals
int tinyWidth(const char *s, int k = 1);
// Draws s with its top row at `top`, from column x; returns the width
int tiny(const char *s, float x, int top, uint16_t c, float a, int k = 1);
void tinyCentre(const char *s, float cx, int top, uint16_t c, float a, int k = 1);
// Cuts s to n characters, ending in '.' if cut
void tinyFit(const char *s, int n, char *out, int outLen);

// The core's 5x7 Latin and 10 px hanzi, one cell per font pixel, the
// baseline at row `base`; returns the width
int font(text::Face f, const char *s, float x, int base, uint16_t c, float a);
void fontCentre(text::Face f, const char *s, float cx, int base, uint16_t c, float a);

// Rows [y0, y1) of the frame into band, its first row standing for y0. Only
// reads the board, so two cores may render bands of one frame at once.
void render(uint16_t *band, int y0, int y1);

}  // namespace board
