#pragma once

#include <stddef.h>
#include <stdint.h>

// The display's screens and behaviour, with no platform underneath: the
// firmware and the review page both feed it summaries and touches, and take
// frames and chimes from it.
namespace ui {

enum Link { CONNECTING, ONLINE, LOST };
enum Touch { DOWN, MOVE, UP };

// cjk: the hanzi bitmap font, device/fonts/cjk10.bin (Latin text needs nothing)
bool init(const uint8_t *cjk, size_t cjkLen);
void link(Link state);
// A summary, or a WebSocket message carrying one; false if it can't be read
bool summary(const char *json);
void touch(Touch kind, int x, int y, int64_t ms);
// Advances to `ms` on a steady clock and works out the frame, ready to draw.
// `wall` is the epoch time that ages are measured against; 0 to follow the
// last summary's clock.
void frame(int64_t ms, int64_t wall = 0);
// Draws rows [y0, y1) of the frame into band, rows of 466 pixels whose first
// stands for row y0: a whole frame at once, or a band at a time in fast
// memory, no full frame kept at all. Bands of one frame may be drawn on two
// cores at once, until the next frame().
void draw(uint16_t *band, int y0, int y1);
// The next chime to play (a synth::Cue), or -1
int cue();
// Which screen shows, for the review page
const char *screen();
// Average milliseconds a frame spent since the last call recording, then
// drawing each stage (on all cores together), for logs
void timings(char *out, size_t len);

}  // namespace ui
