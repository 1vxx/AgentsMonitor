#pragma once

#include <stddef.h>
#include <stdint.h>

#include "events.h"
#include "model.h"
#include "synth.h"
#include "ui.h"

// What a theme implements, and what the host (host.cpp) does for every theme.
//
// A theme is the look and feel of the display: what it draws and how it
// moves. Each firmware image and each review-page build holds exactly one,
// from devices/themes/<name>/. The host sits between it and the board: it
// reads the daemon's summaries, keeps the clocks, works out what happened
// (a session starts waiting, a turn ends, a wait goes unanswered), plays the
// chimes every theme shares, and tells a press of the side button from two.
// The theme gets all of that already worked out, and only decides how it
// looks.
//
// Per frame, the host calls in this order: input as it arrives (event,
// summary, touch, press, summon), then frame() once, then draw() for each
// band of rows.
namespace theme {

// What the host keeps up to date, current at every call into the theme
struct State {
  ui::Link link = ui::CONNECTING;
  bool have = false;      // a summary has come since the link was last lost
  model::Summary summary;  // the latest one; only current while `have`
  int64_t now = 0;        // ms on a steady clock, as of the frame being worked out
  int64_t wall = 0;       // epoch ms that ages are measured against
  float dt = 0;           // seconds since the previous frame, at most 0.1
  events::Tracker waits;  // the sessions waiting for you, oldest first
};
const State &state();

// The theme's own data, as the board keeps it (a photo pack in its data
// partition, say): there from init() on, or empty when the board has none.
// len bytes, readable with read(); p points at them as well when they are in
// memory whole (on the review page), and is null when the board can only
// read them a piece at a time (flash past what its cache can map).
struct Data {
  const uint8_t *p = nullptr;
  size_t len = 0;
  bool (*read)(size_t at, void *out, size_t n) = nullptr;
};
const Data &data();

// Queues a chime of the theme's own; the host already plays ATTENTION when a
// session starts waiting, DONE or ERROR when a turn ends, and REMIND for a
// wait nobody answers
void chime(synth::Cue cue);
// The person has seen what waits: no more reminders for the waits there are
// now. The host does this on every press of the side button.
void ack();
// The person looked at a session's finished turn on the display, the one that
// ended at endedAt: the host passes it on to the daemon, so the turn stops
// counting as new everywhere, not only here
void seen(const char *key, int64_t endedAt);
// Reminders for waits nobody has answered, due as of this frame; returns how
// many, each already chimed. Call it at most once a frame, from frame(), at
// the point where the theme wants to react to them. A theme that never calls
// it still gets the chimes, after its frame.
int reminders(events::Event *out, int max);

class Theme {
 public:
  // Once, after the core (gfx, text) is up; false if it can't run
  virtual bool init() = 0;
  // The link to the daemon was lost: nothing on screen is current any more.
  // The host has already forgotten the summary and the waits.
  virtual void reset() {}
  // Something happened between the summary on screen and `next`: a session
  // started waiting (WAIT) or finished a turn (END). Called for each, in
  // order, while state().summary is still the previous one.
  virtual void event(const events::Event & /*e*/, const model::Summary & /*next*/) {}
  // A new summary is now state().summary, after its events: match the
  // theme's own objects to its sessions
  virtual void summary() {}
  virtual void touch(ui::Touch /*kind*/, int /*x*/, int /*y*/, int64_t /*ms*/) {}
  // The side button: times is 1 for a press, then 2 if it was pressed again
  // within PRESS_TWICE_MS. Not called while a finger is on the screen.
  virtual void press(int /*times*/, int64_t /*ms*/) {}
  // The other button, held down: whatever the theme keeps for rare moments
  virtual void summon(int64_t /*ms*/) {}
  // Advances to state().now and works out the frame, ready to draw
  virtual void frame() = 0;
  // Draws rows [y0, y1) of the frame into band, rows of gfx::W pixels in
  // big-endian RGB565, the first standing for row y0. Bands of one frame may
  // be drawn on two cores at once, so this must only read what frame() left.
  virtual void draw(uint16_t *band, int y0, int y1) = 0;
  // Which screen shows, for logs and the review page
  virtual const char *screen() const { return "main"; }
  // Milliseconds per frame spent on the theme's own stages since the last
  // call, for the logs
  virtual void timings(char *out, size_t len) {
    if (len) out[0] = 0;
  }

 protected:
  ~Theme() = default;
};

// The theme in this build: each theme defines it, once
Theme &instance();

// A second press this soon after the first counts as pressing twice
constexpr int64_t PRESS_TWICE_MS = 450;

}  // namespace theme
