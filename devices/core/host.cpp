// The display as boards see it (ui.h), run by the theme in this build
// (theme.h). Everything every theme would otherwise do the same way lives
// here: reading summaries, the clocks, what happened between two summaries,
// the shared chimes, and one press of the side button from two.

#include <string.h>

#include "gfx.h"
#include "text.h"
#include "theme.h"
#include "ui.h"

namespace theme {
namespace {

State st;
int64_t lastMs;
int64_t summaryAt, summaryTs;  // our clock and the daemon's when the last summary came
bool fingerDown;
bool remindersTaken;  // the theme took this frame's reminders itself
int cues[8], cueHead, cueTail;
struct Seen {
  char key[sizeof model::Session::key];
  int64_t endedAt;
};
Seen seens[4];  // turns looked at here, for the board to tell the daemon
int seenHead, seenTail;

// The chimes every theme shares, for what happened
void shared(const events::Event &e) {
  switch (e.kind) {
    case events::WAIT:
      chime(synth::ATTENTION);
      break;
    case events::END: {
      bool failed = e.outcome == model::Outcome::Error || e.outcome == model::Outcome::Refusal;
      if (e.outcome == model::Outcome::Answer || failed) chime(failed ? synth::ERROR : synth::DONE);
      break;
    }
    case events::REMIND:
      chime(synth::REMIND);
      break;
  }
}

}  // namespace

const State &state() { return st; }

Data blob;
const Data &data() { return blob; }

// Reading data that is in memory whole
bool readMemory(size_t at, void *out, size_t n) {
  if (at > blob.len || n > blob.len - at) return false;
  memcpy(out, blob.p + at, n);
  return true;
}

void chime(synth::Cue cue) {
  int next = (cueTail + 1) % 8;
  if (next == cueHead) return;
  cues[cueTail] = cue;
  cueTail = next;
}

void ack() { st.waits.ack(); }

void seen(const char *key, int64_t endedAt) {
  int next = (seenTail + 1) % 4;
  if (next == seenHead) return;
  strncpy(seens[seenTail].key, key, sizeof seens[seenTail].key - 1);
  seens[seenTail].key[sizeof seens[seenTail].key - 1] = 0;
  seens[seenTail].endedAt = endedAt;
  seenTail = next;
}

int reminders(events::Event *out, int max) {
  remindersTaken = true;
  int n = st.waits.remind(st.wall, out, max);
  for (int k = 0; k < n; k++) shared(out[k]);
  return n;
}

}  // namespace theme

namespace ui {

using theme::st;

bool init(const uint8_t *cjk, size_t cjkLen, const uint8_t *data, size_t dataLen, bool (*readData)(size_t, void *, size_t)) {
  theme::blob = {data, data || readData ? dataLen : 0, readData ? readData : data ? theme::readMemory : nullptr};
  return gfx::init() && text::init(cjk, cjkLen) && theme::instance().init();
}

void link(Link state) {
  if (state != ONLINE) {
    // Nothing on screen is current any more
    st.have = false;
    st.waits.clear();
    theme::instance().reset();
  }
  st.link = state;
}

bool summary(const char *json) {
  static model::Summary next;
  if (!model::parse(json, next)) return false;
  // The daemon's clock, taken again only when it has moved: a summary read
  // twice says the same time twice
  if (next.ts != theme::summaryTs) theme::summaryAt = st.now, theme::summaryTs = next.ts;
  if (!st.wall) st.wall = next.ts;

  theme::Theme &t = theme::instance();
  events::Event evs[2 * model::MAX];
  int n = st.waits.follow(st.have ? &st.summary : nullptr, next, evs, 2 * model::MAX);
  for (int k = 0; k < n; k++) {
    theme::shared(evs[k]);
    t.event(evs[k], next);
  }
  st.summary = next;
  st.have = true;
  st.waits.prune(st.summary);
  t.summary();
  return true;
}

void touch(Touch kind, int x, int y, int64_t ms) {
  if (kind == DOWN) theme::fingerDown = true;
  if (kind == UP) theme::fingerDown = false;
  theme::instance().touch(kind, x, y, ms);
}

void press(int64_t ms) {
  static int64_t lastPress = -100000;
  if (theme::fingerDown) return;  // a finger on the glass has it
  theme::ack();
  if (ms - lastPress < theme::PRESS_TWICE_MS) {
    lastPress = -100000;
    theme::instance().press(2, ms);
    return;
  }
  lastPress = ms;
  theme::instance().press(1, ms);
}

void summon(int64_t ms) { theme::instance().summon(ms); }

void frame(int64_t ms, int64_t wall) {
  st.now = ms;
  float dt = theme::lastMs ? (ms - theme::lastMs) / 1000.f : 0;
  st.dt = dt > 0.1f ? 0.1f : dt;
  theme::lastMs = ms;
  if (wall) {
    st.wall = wall;
  } else {
    // Kept by the summaries, and never turned back by one that was slower to
    // arrive than the last: timers on screen only count up. A minute or more
    // back is the clock itself being set.
    int64_t w = theme::summaryTs + (ms - theme::summaryAt);
    if (w > st.wall || st.wall - w > 60000) st.wall = w;
  }
  theme::remindersTaken = false;
  theme::instance().frame();
  if (!theme::remindersTaken) {
    events::Event due[model::MAX];
    theme::reminders(due, model::MAX);
  }
}

void draw(uint16_t *band, int y0, int y1) { theme::instance().draw(band, y0, y1); }

int cue() {
  using theme::cueHead, theme::cueTail, theme::cues;
  if (cueHead == cueTail) return -1;
  int c = cues[cueHead];
  cueHead = (cueHead + 1) % 8;
  return c;
}

bool seen(char *key, size_t len, int64_t *endedAt) {
  using theme::seenHead, theme::seenTail, theme::seens;
  if (seenHead == seenTail || !len) return false;
  strncpy(key, seens[seenHead].key, len - 1);
  key[len - 1] = 0;
  *endedAt = seens[seenHead].endedAt;
  seenHead = (seenHead + 1) % 4;
  return true;
}

const char *screen() { return theme::instance().screen(); }

void timings(char *out, size_t len) { theme::instance().timings(out, len); }

}  // namespace ui
