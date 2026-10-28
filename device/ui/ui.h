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
// Advances to `ms` on a steady clock and draws into fb. `wall` is the epoch
// time that ages are measured against; 0 to follow the last summary's clock.
void frame(uint16_t *fb, int64_t ms, int64_t wall = 0);
// Instead of drawing whole frames into fb, draws each a band of `rows` rows
// at a time, alternately into buf[0] and buf[1] (rows x 466 pixels, in fast
// memory), and hands each band to send(), which starts it on its way and
// returns. wait(k) returns once band buffer k may be drawn into again. Only
// the drawing touches the band, so it runs in fast memory and no full frame
// is kept at all.
struct Stream {
  uint16_t *buf[2];
  int rows;
  void (*send)(int k, const uint16_t *band, int y0, int rows);
  void (*wait)(int k);
};
void stream(const Stream &s);
// The next chime to play (a synth::Cue), or -1
int cue();
// Which screen shows, for the review page
const char *screen();
// Average milliseconds a frame spent in each stage of drawing since the last
// call, for logs
void timings(char *out, size_t len);

}  // namespace ui
