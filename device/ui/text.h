#pragma once

#include <stddef.h>
#include <stdint.h>

// Pixel text, as on the first demo's rim: a 5x7 dot-matrix font for Latin
// and a 10 px bitmap font (Fusion Pixel) for hanzi, both drawn at a whole
// number of panel pixels per font pixel so the dots stay square and crisp.
namespace text {

// PROP packs letters by their width; MONO gives every Latin letter six columns
enum Face { PROP, MONO };

// cjk: the cjk10.bin bitmap font; may be null, and hanzi then draw as boxes
bool init(const uint8_t *cjk, size_t len);

// Decodes the code point at s and moves s past it; 0 at the end
uint32_t next(const char *&s);

// Height of a capital, from the baseline, at `scale` panel pixels per dot
inline int cap(int scale) { return 7 * scale; }

int width(Face f, int scale, const char *s);
// Draws s with its baseline at y, starting at x, within columns [clip0, clip1).
// `fade` px at each clip edge fade out, for scrolling text. Returns the width.
int draw(Face f, int scale, const char *s, float x, int y, uint16_t color, float alpha = 1, int clip0 = 0,
         int clip1 = 466, int fade = 0);
// Centred on cx
void centre(Face f, int scale, const char *s, float cx, int y, uint16_t color, float alpha = 1);

// Copies as much of s as fits in maxW, ending in an ellipsis if cut
void fit(Face f, int scale, const char *s, int maxW, char *out, size_t outLen);
// Breaks s into lines of at most maxW, at spaces where it can; the last line
// is cut to fit. Returns the count.
int wrap(Face f, int scale, const char *s, int maxW, char (*lines)[160], int maxLines);
// ASCII letters to capitals; everything else as it is
void upper(const char *s, char *out, size_t outLen);

}  // namespace text
