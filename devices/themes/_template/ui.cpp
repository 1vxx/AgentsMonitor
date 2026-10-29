// A starting point for a theme: `devices/tools/new_theme.sh <name>` copies it.
//
// One screen. A ring round the edge takes the colour of the overall mood,
// with a dot on it for each session; the session in focus shows its project,
// title and state in the middle. Swipe sideways for the next session, tap to
// quiet the reminders. A session that starts waiting for you takes the focus,
// and a turn that ends, or a wait nobody answers, flashes its dot.
//
// The host (core/theme.h) keeps the summary and the clocks and plays the
// chimes every theme shares, so a theme only decides how things look and
// move. Try it with `THEME=<name> devices/sim/build.sh`.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gesture.h"
#include "gfx.h"
#include "spring.h"
#include "text.h"
#include "theme.h"

namespace starter {
namespace {

using gfx::hex;
using gfx::RGB;
using model::Mood;
using model::Outcome;
using model::Session;
using model::Status;

// ---------- Look ----------

// By model::Mood: offline, idle, error, working, attention
const RGB MOOD_COLORS[] = {hex(0x3d4160), hex(0x5a6bb8), hex(0xd6493f), hex(0xe9774f), hex(0xf0aa2e)};
const RGB TEXT = hex(0xeceaf6), DIM = hex(0x7d7e9a), GOOD = hex(0x8ad6a2), BAD = hex(0xf26d63);

constexpr float RING_R = 205;       // the ring's radius, inside the panel's edge
constexpr float SLIDE_PX = 300;     // how far apart neighbouring sessions sit when swiped
constexpr int SWIPE_PX = 60;        // a sideways drag this long moves to the next session
constexpr int64_t FLASH_MS = 1500;  // a dot flashes this long

// ---------- State ----------

const theme::State &host = theme::state();

gesture::Finger finger;
bool swiping;      // a finger is carrying the middle sideways
char focusKey[96];  // the session in the middle, chosen; empty for the most urgent
Spring slide;      // the middle's offset while it slides, px
RGB ring = MOOD_COLORS[0];

struct Flash {
  char key[96];
  int64_t at;
  RGB color;
} flashes[model::MAX];
int nextFlash;

// ---------- Helpers ----------

int indexOf(const char *key) {
  for (int i = 0; i < host.summary.count; i++)
    if (!strcmp(host.summary.sessions[i].key, key)) return i;
  return -1;
}

// The session in the middle: the chosen one while it lasts, else the summary's
// first, which is the one that most needs you
const Session *focused() {
  if (!host.have || !host.summary.count) return nullptr;
  int i = focusKey[0] ? indexOf(focusKey) : -1;
  return &host.summary.sessions[i < 0 ? 0 : i];
}

void flash(const char *key, RGB color) {
  Flash &f = flashes[nextFlash++ % model::MAX];
  snprintf(f.key, sizeof f.key, "%s", key);
  f.at = host.now;
  f.color = color;
}

// What a session is doing, in a few capitals, and the colour to say it in
void stateOf(const Session &s, char *out, size_t n, RGB &color) {
  color = TEXT;
  if (s.stale) {
    snprintf(out, n, "NO SIGNAL"), color = DIM;
    return;
  }
  switch (s.status) {
    case Status::WaitingPermission:
      if (s.waitTool[0]) snprintf(out, n, "APPROVE %s?", s.waitTool);
      else snprintf(out, n, "NEEDS APPROVAL");
      color = MOOD_COLORS[int(Mood::Attention)];
      break;
    case Status::WaitingInput: snprintf(out, n, "HAS A QUESTION"), color = MOOD_COLORS[int(Mood::Attention)]; break;
    case Status::Tool: snprintf(out, n, "RUNNING %s", s.tool), color = MOOD_COLORS[int(Mood::Working)]; break;
    case Status::Thinking: snprintf(out, n, "THINKING"), color = MOOD_COLORS[int(Mood::Working)]; break;
    case Status::Compacting: snprintf(out, n, "COMPACTING"), color = MOOD_COLORS[int(Mood::Working)]; break;
    case Status::Ended: snprintf(out, n, "ENDED"), color = DIM; break;
    case Status::Idle:
      if (s.outcome == Outcome::Answer) snprintf(out, n, "DONE"), color = GOOD;
      else if (s.outcome == Outcome::Error || s.outcome == Outcome::Refusal) snprintf(out, n, "FAILED"), color = BAD;
      else snprintf(out, n, "IDLE"), color = DIM;
      break;
  }
  text::upper(out, out, n);
}

// Moves the focus `step` sessions on, round the ring
void stepFocus(int step) {
  const Session *f = focused();
  int count = host.summary.count;
  if (!f || count < 2) return;
  int i = (indexOf(f->key) + step + count) % count;
  snprintf(focusKey, sizeof focusKey, "%s", host.summary.sessions[i].key);
}

// ---------- Drawing ----------

void compose() {
  gfx::clear();
  gfx::vignette(RING_R - 60, gfx::CX, gfx::c565(ring), 0.18f);
  gfx::ring(RING_R, 1.5f, gfx::c565(ring), 0.8f);

  // A dot on the ring for each session, from the top, clockwise
  const Session *f = focused();
  int count = host.have ? host.summary.count : 0;
  for (int i = 0; i < count; i++) {
    const Session &s = host.summary.sessions[i];
    float sn, cs;
    gfx::sincosTurns(float(i) / count, sn, cs);
    float x = gfx::CX + RING_R * sn, y = gfx::CY - RING_R * cs;
    char line[64];
    RGB color;
    stateOf(s, line, sizeof line, color);
    bool here = &s == f;
    for (const Flash &fl : flashes)
      if (!strcmp(fl.key, s.key) && host.now - fl.at < FLASH_MS)
        gfx::glowAt(x, y, 40, gfx::c565(fl.color), 1 - float(host.now - fl.at) / FLASH_MS);
    gfx::discAt(x, y, here ? 9 : 6, gfx::c565(color));
  }

  // The session in the middle
  float cx = gfx::CX + slide.x;
  if (!f) {
    const char *say = host.link == ui::ONLINE ? "NO SESSIONS" : host.link == ui::CONNECTING ? "CONNECTING" : "LINK LOST";
    text::centre(text::LABEL, 3, say, gfx::CX, 245, gfx::c565(DIM));
    return;
  }
  char project[64], title[160], line[64], shown[64];
  RGB color;
  text::upper(f->project, project, sizeof project);
  text::centre(text::LABEL, 2, project, cx, 175, gfx::c565(DIM));
  text::fit(text::PROP, 3, f->title[0] ? f->title : f->project, 300, title, sizeof title);
  text::centre(text::PROP, 3, title, cx, 245, gfx::c565(TEXT));
  stateOf(*f, line, sizeof line, color);
  text::fit(text::LABEL, 3, line, 300, shown, sizeof shown);
  text::centre(text::LABEL, 3, shown, cx, 305, gfx::c565(color));
}

// ---------- The theme ----------

class Starter final : public theme::Theme {
 public:
  bool init() override { return true; }

  void reset() override {
    focusKey[0] = 0;
    swiping = false;
  }

  void event(const events::Event &e, const model::Summary &) override {
    if (e.kind == events::WAIT) {
      snprintf(focusKey, sizeof focusKey, "%s", e.key);
    } else if (e.kind == events::END) {
      bool failed = e.outcome == Outcome::Error || e.outcome == Outcome::Refusal;
      flash(e.key, failed ? BAD : GOOD);
    }
  }

  void summary() override {
    if (focusKey[0] && indexOf(focusKey) < 0) focusKey[0] = 0;  // its session has gone
  }

  void touch(ui::Touch kind, int x, int y, int64_t ms) override {
    switch (kind) {
      case ui::DOWN:
        finger.down(x, y, ms);
        break;
      case ui::MOVE:
        finger.move(x, y, ms);
        if (!swiping && finger.dragging() && abs(finger.dx()) > abs(finger.dy())) swiping = true;
        if (swiping) slide.x = float(finger.dx()), slide.v = 0;
        break;
      case ui::UP:
        finger.up(x, y, ms);
        if (swiping) {
          float vx, vy;
          finger.velocity(vx, vy);
          int step = finger.dx() < -SWIPE_PX || vx < -500 ? 1 : finger.dx() > SWIPE_PX || vx > 500 ? -1 : 0;
          stepFocus(step);
          // The next one is already under the finger: it carries on from there
          if (step) slide.x += step * SLIDE_PX;
          slide.v = vx;
          swiping = false;
        } else if (finger.tapped(ms)) {
          theme::ack();
        }
        break;
    }
  }

  // Once or twice, back to whoever most needs you; a theme with more than one
  // screen would send twice somewhere else
  void press(int, int64_t) override { focusKey[0] = 0; }

  void frame() override {
    float dt = host.dt;
    Mood mood = host.have ? host.summary.mood : Mood::Offline;
    ring = gfx::mix(ring, MOOD_COLORS[int(mood)], gfx::clamp01(dt * 4));
    if (!swiping) slide.step(0, dt);
    events::Event due[model::MAX];
    for (int k = 0, n = theme::reminders(due, model::MAX); k < n; k++) flash(due[k].key, MOOD_COLORS[int(Mood::Attention)]);

    text::newFrame();
    gfx::record(true);
    compose();
    gfx::record(false);
  }

  void draw(uint16_t *band, int y0, int y1) override { gfx::play(band, y0, y1); }
};

}  // namespace
}  // namespace starter

theme::Theme &theme::instance() {
  static starter::Starter starter;
  return starter;
}
