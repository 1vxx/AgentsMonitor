#ifndef __clang__
#pragma GCC optimize("O2")
#endif

// Screens: HOME (the halo, with the focused session's title and state under
// the orb), ALERT (the card that takes over when a session starts waiting for
// you), LIST (every session, swiped up from home) and DETAIL (one session).
//
// Moving between screens is one motion: the orb flies and resizes from where
// one screen keeps it to where the next does, the leaving screen fades, and
// the arriving one comes up piece by piece. Everything moves on springs, and a
// finger can drive a move and take it back halfway.
//
// What happened is read off the difference between two summaries: a session
// that starts waiting chimes and takes over the screen for ten seconds, a turn
// that ends chimes once, softly for an answer and low for an error, and turns
// the orb green or red for a moment. A wait nobody answers chimes again at 2
// and 5 minutes, then every 10, until a tap.

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "events.h"
#include "gfx.h"
#include "halo.h"
#include "model.h"
#include "spring.h"
#include "synth.h"
#include "text.h"

namespace ui {
namespace {

using gfx::RGB;
using gfx::hex;
using model::Mood;
using model::Outcome;
using model::Session;
using model::Status;
using model::Wait;
using text::LABEL;
using text::MONO;
using text::PROP;

// ---------- Look ----------

const RGB TEXT = hex(0xeceaf6), TEXT_2 = hex(0xb4b4cb), TEXT_3 = hex(0x7d7e9a), TEXT_4 = hex(0x4c4d66);
const RGB BORDER_2 = hex(0x1c1f2b), CARD = hex(0x17161f), WAIT_ROW = hex(0x2b2110);

enum Tone { ACTIVE, ATTN, GOOD, CRITICAL, REST, MUTED };
const RGB TONE_COLORS[] = {hex(0xf08c68), hex(0xf5ba4c), hex(0x8ad6a2), hex(0xf26d63), gfx::scale(TEXT, 0.55f), TEXT_4};
/** Degrees per second for each tone, as in Halo.tsx. */
const float SPEED[] = {52, 0, 3, 6, 7, 2};
// What the orb turns when a turn finishes well, and when one fails
const halo::Palette DONE_PALETTE = {hex(0x5fc48a), hex(0xd2f5dc), hex(0x2f8f6b)};
const halo::Palette FAIL_PALETTE = {hex(0xe5483e), hex(0xffb4a6), hex(0x8a1f3d)};

constexpr int64_t GOOD_FOR_MS = 3 * 60000;  // a finished turn shows green this long
constexpr int64_t TAKEOVER_MS = 10000;      // the alert card's stay
constexpr int64_t ROTATE_MS = 4000;         // between waiting sessions on the card
constexpr int64_t FOCUS_MS = 10000;         // a chosen session stays under the orb
constexpr int64_t AWAY_MS = 30000;          // list and detail fall back home
constexpr int64_t SHIFT_MS = 3 * 60000;     // anti burn-in nudge
constexpr float CHEER_S = 2.6f;             // the orb's celebration of a finished turn
constexpr float WINCE_S = 2.2f;             // and its wince at a failed one
constexpr float MOVE_PX = 220;              // a finger this far carries a move all the way
constexpr float SLIDE = 320;                // how far apart neighbouring titles sit when swiped

// ---------- State ----------

// Time spent recording frames, and drawing each stage of them, for the logs.
// Bands draw on both cores at once, so the stages add up atomically.
enum Part { P_BACK, P_SKY, P_RIM, P_GLOW, P_ORB, P_HOME, P_ALERT, P_LIST, P_DETAIL, P_PRESS, PARTS };
const char *const PART_NAMES[PARTS] = {"back", "sky", "rim", "glow", "orb", "home", "alert", "list", "detail", "press"};
std::atomic<uint32_t> partTicks[PARTS];
uint32_t recTicks, partFrames;

model::Summary cur;
bool haveSummary;
Link linkState = CONNECTING;
int64_t summaryAt, summaryTs;  // our clock and the daemon's when the last one came

enum Screen { HOME, ALERT, LIST, DETAIL };
Screen from = HOME, to = HOME, backTo = HOME;
Spring trans = {1, 0};  // 0: all `from`, 1: all `to`; overshoots a little
bool driving;           // a finger is carrying the move

int64_t lastTouch;
char focusKey[96], detailKey[96];
int64_t focusAt;

// The two lines under the orb: which session they show, the one they replaced
// while it fades, how far they are swiped, and the state line before it changed
struct Caption {
  char key[96], prevKey[96];
  Spring fade = {1, 0};
  Spring off;
  int64_t since;
  char line[96], was[96];
  int64_t changed = -100000;
  bool rolls;  // the change was digits only
} cap;

float listScroll, listVel;

struct Sat {
  char key[96];
  bool used;
  float turn, speed, tail, ping, lit, focus;
  RGB color;
  int64_t flashAt;
  RGB flashColor;
};
Sat sats[model::MAX];

events::Tracker tracker;
events::Wait *const waits = tracker.waits;
int &waitCount = tracker.count;
int64_t alertUntil, alertAt;

int cues[8], cueHead, cueTail;

halo::Palette pal = halo::palette(Mood::Offline);
halo::Motion motion;
float spinSpeed, pulsePhase, pulseK, breatheK, rippleK, glowK = 0.5f;
int64_t lastMs, nowMs, wallMs, cheerAt = -100000, winceAt = -100000;

// Touch
enum class Drag { None, Undecided, Caption, Move, Scroll };
Drag drag = Drag::None;
int downX, downY, lastX, lastY;
int64_t downAt;
float scrollAtDown;
Screen moveFrom;
struct Sample {
  int x, y;
  int64_t ms;
} samples[6];
int sampleCount;
int64_t pressAt = -100000, releaseAt = -100000;
float pressX, pressY;
bool pressTapped;

// ---------- Helpers ----------

uint32_t hash(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ uint8_t(*s++)) * 16777619u;
  return h;
}

inline float smooth(float v) { return v * v * (3 - 2 * v); }
inline float easeOut(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

void pushCue(synth::Cue c) {
  int next = (cueTail + 1) % 8;
  if (next == cueHead) return;
  cues[cueTail] = c;
  cueTail = next;
}

void choose(const char *key) {
  snprintf(focusKey, sizeof focusKey, "%s", key);
  focusAt = nowMs;
}

int indexOf(const char *key) {
  for (int i = 0; i < cur.count; i++)
    if (!strcmp(cur.sessions[i].key, key)) return i;
  return -1;
}

using events::waitSince;
bool isWaiting(const Session &s) { return events::waiting(s); }

Tone tone(const Session &s) {
  if (s.stale) return MUTED;
  if (model::waiting(s.status)) return ATTN;
  if (model::working(s.status)) return ACTIVE;
  if (s.outcome == Outcome::Error || s.outcome == Outcome::Refusal) return CRITICAL;
  if (s.outcome == Outcome::Answer && wallMs - s.endedAt < GOOD_FOR_MS) return GOOD;
  return REST;
}

halo::Palette tonePalette(Tone t) {
  switch (t) {
    case ACTIVE: return halo::palette(Mood::Working);
    case ATTN: return halo::palette(Mood::Attention);
    case CRITICAL: return halo::palette(Mood::Error);
    case GOOD: return DONE_PALETTE;
    case REST: return halo::palette(Mood::Idle);
    default: return halo::palette(Mood::Offline);
  }
}

// The colour a line of state is written in
RGB toneText(Tone t) { return t == REST || t == MUTED ? TEXT_3 : TONE_COLORS[t]; }

Mood mood() { return linkState == ONLINE && haveSummary ? cur.mood : Mood::Offline; }

const char *titleOf(const Session &s) { return s.title[0] ? s.title : s.project; }

// "mcp__server__tool" reads "server · tool"; in capitals for labels
void toolName(const char *tool, char *out, size_t n) {
  char plain[96];
  if (strncmp(tool, "mcp__", 5)) {
    snprintf(plain, sizeof plain, "%s", tool);
  } else {
    const char *rest = tool + 5, *cut = strstr(rest, "__");
    if (cut)
      snprintf(plain, sizeof plain, "%.*s · %s", int(cut - rest), rest, cut + 2);
    else
      snprintf(plain, sizeof plain, "%s", rest);
  }
  text::upper(plain, out, n);
}

void duration(int64_t ms, char *out, size_t n) {
  int64_t s = ms < 0 ? 0 : ms / 1000;
  if (s < 3600)
    snprintf(out, n, "%d:%02d", int(s / 60), int(s % 60));
  else
    snprintf(out, n, "%d:%02d:%02d", int(s / 3600), int(s / 60 % 60), int(s % 60));
}

// "2M", "3H", "1D"; with `suffix`, "2M AGO", and "JUST NOW" under a minute
void age(int64_t ms, char *out, size_t n, bool suffix) {
  int64_t m = ms / 60000;
  if (m < 1) {
    snprintf(out, n, suffix ? "JUST NOW" : "NOW");
    return;
  }
  const char *ago = suffix ? " AGO" : "";
  if (m < 60)
    snprintf(out, n, "%dM%s", int(m), ago);
  else if (m < 24 * 60)
    snprintf(out, n, "%dH%s", int(m / 60), ago);
  else
    snprintf(out, n, "%dD%s", int(m / 1440), ago);
}

// What a session is doing, in the dashboard's words, longest first: callers
// take the first that fits
int stateLines(const Session &s, char (*out)[96]) {
  char t[32], tool[64];
  if (s.stale) {
    age(wallMs - s.since, t, sizeof t, false);
    snprintf(out[0], 96, "NO SIGNAL · %s", t);
    return 1;
  }
  switch (s.status) {
    case Status::WaitingPermission:
      duration(wallMs - waitSince(s), t, sizeof t);
      toolName(s.waitTool[0] ? s.waitTool : s.tool, tool, sizeof tool);
      if (tool[0]) {
        snprintf(out[0], 96, "NEEDS APPROVAL · %s · %s", tool, t);
        snprintf(out[1], 96, "NEEDS APPROVAL · %s", t);
        snprintf(out[2], 96, "NEEDS APPROVAL");
        return 3;
      }
      snprintf(out[0], 96, "NEEDS APPROVAL · %s", t);
      snprintf(out[1], 96, "NEEDS APPROVAL");
      return 2;
    case Status::WaitingInput:
      duration(wallMs - waitSince(s), t, sizeof t);
      snprintf(out[0], 96, "NEEDS AN ANSWER · %s", t);
      snprintf(out[1], 96, "NEEDS AN ANSWER");
      return 2;
    case Status::Tool:
      toolName(s.tool, tool, sizeof tool);
      if (!tool[0]) {
        snprintf(out[0], 96, "RUNNING A TOOL");
        return 1;
      }
      snprintf(out[0], 96, "RUNNING %s", tool);
      snprintf(out[1], 96, "%s", tool);
      return 2;
    case Status::Thinking: snprintf(out[0], 96, "THINKING"); return 1;
    case Status::Compacting: snprintf(out[0], 96, "COMPACTING"); return 1;
    default: break;
  }
  age(wallMs - (s.endedAt ? s.endedAt : s.since), t, sizeof t, true);
  switch (s.outcome) {
    case Outcome::Error:
      snprintf(out[0], 96, "LAST TURN FAILED · %s", t);
      snprintf(out[1], 96, "FAILED · %s", t);
      return 2;
    case Outcome::Refusal:
      snprintf(out[0], 96, "LAST TURN REFUSED · %s", t);
      snprintf(out[1], 96, "REFUSED · %s", t);
      return 2;
    case Outcome::Aborted:
      snprintf(out[0], 96, "INTERRUPTED · %s", t);
      snprintf(out[1], 96, "INTERRUPTED");
      return 2;
    case Outcome::Answer: snprintf(out[0], 96, "DONE · %s", t); return 1;
    default: snprintf(out[0], 96, "IDLE"); return 1;
  }
}

// The first of the lines that fits maxW, else the last one cut to fit
void pickLine(char (*lines)[96], int n, text::Face face, int scale, int maxW, char *out, size_t outLen) {
  for (int k = 0; k < n; k++)
    if (text::width(face, scale, lines[k]) <= maxW) {
      snprintf(out, outLen, "%s", lines[k]);
      return;
    }
  text::fit(face, scale, lines[n - 1], maxW, out, outLen);
}

// The session under the orb: the one chosen by swiping, else the most urgent
const Session *focused() {
  if (!cur.count) return nullptr;
  if (focusKey[0]) {
    int i = indexOf(focusKey);
    if (i >= 0) return &cur.sessions[i];
    focusKey[0] = 0;
  }
  return &cur.sessions[0];
}

// The anti burn-in nudge: a few pixels, moved every few minutes
void shift(float &dx, float &dy) {
  static const int8_t STEPS[][2] = {{0, 0}, {1, 1}, {-1, 2}, {2, -1}, {-2, 0}, {0, -2}, {1, -1}, {-1, 1}};
  int k = int(nowMs / SHIFT_MS % 8);
  dx = STEPS[k][0], dy = STEPS[k][1];
}

// A title centred on cx within a band round bandX: at `scale`, a size down if
// it does not fit, then scrolling. Its ends fade at the band's edges.
void title(const char *s, int scale, float cx, float bandX, int y, int maxW, uint16_t color, float alpha, int64_t since) {
  // Within its own window as it slides, and never past the band
  int x0 = int(cx) - maxW / 2, x1 = x0 + maxW;
  int b0 = int(bandX) - maxW / 2, b1 = b0 + maxW;
  if (x0 < b0) x0 = b0;
  if (x1 > b1) x1 = b1;
  if (x1 <= x0) return;
  int w = text::width(PROP, scale, s);
  if (w > maxW && scale > 3) w = text::width(PROP, --scale, s);
  if (w <= maxW) {
    text::draw(PROP, scale, s, cx - w / 2.f, y, color, alpha, x0, x1, 20);
    return;
  }
  // Rest two seconds at the start, then scroll at 36 px/s with a gap between copies
  int loop = w + 24 * scale;
  float t = (nowMs - since) / 1000.f - 2;
  float off = t <= 0 ? 0 : fmodf(t * 36, float(loop));
  float start = cx - maxW / 2.f;
  text::draw(PROP, scale, s, start - off, y, color, alpha, x0, x1, 20);
  text::draw(PROP, scale, s, start - off + loop, y, color, alpha, x0, x1, 20);
}

// ---------- Moving between screens ----------

// How much of a screen is up: 1 for the one on show, shared out during a move
float present(Screen s) {
  if (from == to) return s == to ? 1.f : 0.f;
  float t = gfx::clamp01(trans.x);
  return s == to ? t : s == from ? 1 - t : 0;
}

// How visible a screen's content is: the leaving one is gone by 60% of the way
float shown(Screen s) {
  if (from == to) return s == to ? 1.f : 0.f;
  float t = gfx::clamp01(trans.x);
  return s == to ? t : s == from ? gfx::clamp01(1 - t * 1.7f) : 0;
}

// How far the arriving screen has come in; 1 for any other
float arrived(Screen s) { return s == to && from != to ? gfx::clamp01(trans.x) : 1; }

// The k-th piece of an arriving screen, each a beat behind the last
float piece(float e, int k) {
  float d = 0.06f * k;
  return d >= 1 ? (e >= 1 ? 1 : 0) : gfx::clamp01((e - d) / (1 - d));
}

void go(Screen s, bool byFinger = false) {
  if (from != to && s == from) {
    // Back the way it came, from wherever the move had got to
    Screen f = from;
    from = to, to = f;
    trans.x = 1 - trans.x, trans.v = -trans.v;
    driving = byFinger;
    return;
  }
  if (s == to) {
    driving = byFinger;
    return;
  }
  if (s == DETAIL) backTo = to;
  if (from != to && trans.x < 0.5f) to = from;  // start from whichever is mostly up
  from = to, to = s;
  trans.x = 0, trans.v = 0;
  driving = byFinger;
}

struct Anchor {
  float x, y, r;
};

// Where each screen keeps the orb
Anchor anchor(Screen s) {
  switch (s) {
    case ALERT: return {233, 84, 30};
    case LIST: return {233, 54, 18};
    case DETAIL: return {233, 54, 16};
    default: return {halo::OX, halo::OY, halo::ORB_R};
  }
}

// ---------- Following the summaries ----------

void follow(const model::Summary &next) {
  events::Event evs[2 * model::MAX];
  int n = tracker.follow(haveSummary ? &cur : nullptr, next, evs, 2 * model::MAX);
  for (int k = 0; k < n; k++) {
    const events::Event &e = evs[k];
    if (e.kind == events::WAIT) {
      pushCue(synth::ATTENTION);
      alertUntil = nowMs + TAKEOVER_MS;
      alertAt = nowMs;
      choose(e.key);
      drag = Drag::None;
      go(ALERT);
    } else if (e.kind == events::END) {
      bool failed = e.outcome == Outcome::Error || e.outcome == Outcome::Refusal;
      if (e.outcome == Outcome::Answer || failed) pushCue(failed ? synth::ERROR : synth::DONE);
      for (Sat &sat : sats)
        if (sat.used && !strcmp(sat.key, e.key)) {
          sat.flashAt = nowMs;
          sat.flashColor = TONE_COLORS[failed ? CRITICAL : GOOD];
        }
      if (e.outcome == Outcome::Answer || failed) {
        (failed ? winceAt : cheerAt) = nowMs;
        // Show which one finished, unless someone is waiting for you
        bool anyWait = false;
        for (int i = 0; i < next.count; i++) anyWait |= isWaiting(next.sessions[i]);
        if (!anyWait) choose(e.key);
      }
    }
  }
  cur = next;
  haveSummary = true;
  tracker.prune(cur);
  if (!waitCount && to == ALERT) go(HOME);
}

// Satellites keep their place across summaries, matched by key
void syncSats() {
  for (Sat &sat : sats)
    if (sat.used && indexOf(sat.key) < 0) sat.used = false;
  for (int i = 0; i < cur.count; i++) {
    const Session &s = cur.sessions[i];
    bool found = false;
    for (Sat &sat : sats)
      if (sat.used && !strcmp(sat.key, s.key)) found = true;
    if (found) continue;
    for (Sat &sat : sats)
      if (!sat.used) {
        sat = {};
        sat.used = true;
        snprintf(sat.key, sizeof sat.key, "%s", s.key);
        sat.turn = (hash(s.key) % 360) / 360.f;
        Tone t = tone(s);
        sat.speed = SPEED[t];
        sat.color = TONE_COLORS[t];
        sat.flashAt = -100000;
        break;
      }
  }
}

// ---------- Time ----------

float listMax();

void step(float dt) {
  Mood m = mood();
  float glide = gfx::clamp01(dt * 3.5f);
  pal = halo::mix(pal, halo::palette(m), glide);
  glowK += ((m == Mood::Offline ? 0.45f : 1) - glowK) * glide;

  // Blob periods per mood, from live.css; offline holds still
  static const float PERIOD[][3] = {{0, 0, 0}, {18, 22, 15}, {10, 13, 8}, {4.2f, 5.6f, 3.4f}, {10, 13, 8}};
  for (int b = 0; b < 3; b++)
    if (PERIOD[int(m)][b] > 0) motion.phase[b] += dt / PERIOD[int(m)][b];
  spinSpeed += ((m == Mood::Working ? 1 / 14.f : 0) - spinSpeed) * gfx::clamp01(dt * 1.6f);
  motion.spin += spinSpeed * dt;
  // A finished turn gives the blobs a swirl that dies away
  float c = (nowMs - cheerAt) / 1000.f;
  if (c >= 0 && c < CHEER_S) {
    motion.spin += 0.9f * expf(-c * 2.2f) * dt;
    for (int b = 0; b < 3; b++) motion.phase[b] += 0.6f * expf(-c * 2.2f) * dt;
  }
  bool pulsing = m == Mood::Attention || m == Mood::Error;
  pulseK += ((pulsing ? 1 : 0) - pulseK) * gfx::clamp01(dt * 2);
  breatheK += ((m == Mood::Offline ? 1 : 0) - breatheK) * gfx::clamp01(dt * 2);
  pulsePhase += dt / (m == Mood::Error ? 3.2f : 1.8f);
  rippleK += ((m == Mood::Attention ? 1 : 0) - rippleK) * gfx::clamp01(dt * 3);

  for (Sat &sat : sats) {
    if (!sat.used) continue;
    int i = indexOf(sat.key);
    if (i < 0) continue;
    Tone t = tone(cur.sessions[i]);
    sat.speed += (SPEED[t] - sat.speed) * gfx::clamp01(dt * 1.6f);
    sat.turn += sat.speed / 360 * dt;
    sat.turn -= floorf(sat.turn);
    sat.color = gfx::mix(sat.color, TONE_COLORS[t], gfx::clamp01(dt * 4));
    sat.tail += ((t == ACTIVE ? 1 : 0) - sat.tail) * gfx::clamp01(dt * 3);
    sat.ping += ((t == ATTN ? 1 : 0) - sat.ping) * gfx::clamp01(dt * 3);
    sat.lit += ((t == REST || t == MUTED ? 0 : 1) - sat.lit) * gfx::clamp01(dt * 3);
    const Session *f = focused();
    bool chosen = f && !strcmp(sat.key, f->key);
    sat.focus += ((chosen ? 1 : 0) - sat.focus) * gfx::clamp01(dt * 8);
  }

  // Reminders for waits nobody has answered
  events::Event due[model::MAX];
  for (int k = 0, n = tracker.remind(wallMs, due, model::MAX); k < n; k++) {
    pushCue(synth::REMIND);
    for (Sat &sat : sats)
      if (sat.used && !strcmp(sat.key, due[k].key)) {
        sat.flashAt = nowMs;
        sat.flashColor = TONE_COLORS[ATTN];
      }
  }

  // The move between screens, unless a finger holds it
  if (!driving) {
    trans.step(1, dt, 190, 23);
    if (from != to && trans.settled(1)) from = to, trans.x = 1, trans.v = 0;
  }

  // The caption follows the session in focus: swiped, it slides; changed
  // otherwise, the new one fades up through the old
  const Session *f = focused();
  const char *key = f ? f->key : "";
  if (strcmp(cap.key, key)) {
    snprintf(cap.prevKey, sizeof cap.prevKey, "%s", cap.key);
    snprintf(cap.key, sizeof cap.key, "%s", key);
    cap.fade.x = 0, cap.fade.v = 0;
    cap.since = nowMs;
    cap.line[0] = 0;
  }
  cap.fade.step(1, dt, 120, 22);
  if (drag != Drag::Caption) cap.off.step(0, dt, 210, 27);

  // The list coasts after a flick and settles back inside its ends
  if (drag != Drag::Scroll) {
    float max = listMax();
    if (listScroll < 0 || listScroll > max) {
      float end = listScroll < 0 ? 0 : max;
      listScroll += (end - listScroll) * gfx::clamp01(dt * 12);
      listVel = 0;
    } else {
      listScroll += listVel * dt;
      listVel *= expf(-dt * 4);
      if (fabsf(listVel) < 4) listVel = 0;
    }
  }

  // Timeouts
  if (to == ALERT && nowMs >= alertUntil) go(HOME);
  if ((to == LIST || to == DETAIL) && drag == Drag::None && nowMs - lastTouch > AWAY_MS) go(HOME);
  if (to == HOME && focusKey[0] && nowMs - focusAt > FOCUS_MS && nowMs - lastTouch > FOCUS_MS) focusKey[0] = 0;
}

// ---------- Home ----------

float pulsed(float r) {
  float p = fmodf(pulsePhase, 1);
  float pulse = p < 0.5f ? 0.95f + 0.08f * (0.5f - 0.5f * cosf(p * gfx::TAU)) : 1.03f - 0.08f * (0.5f - 0.5f * cosf((p - 0.5f) * gfx::TAU));
  float breathe = 0.985f + 0.015f * sinf(nowMs / 1000.f * gfx::TAU / 6);
  return r * (1 + (pulse - 1) * pulseK) * (1 + (breathe - 1) * breatheK);
}

void drawSatellites(float r, float a) {
  float t = nowMs / 1000.f;
  for (Sat &sat : sats)
    if (sat.used && sat.tail > 0.01f) halo::tail(r, sat.turn, 34 / 360.f, gfx::c565(sat.color), 0.55f * sat.tail * a);
  for (Sat &sat : sats) {
    if (!sat.used) continue;
    int i = indexOf(sat.key);
    if (i < 0) continue;
    float x = gfx::CX + sinf(sat.turn * gfx::TAU) * r, y = gfx::CY - cosf(sat.turn * gfx::TAU) * r;
    uint16_t c = gfx::c565(sat.color);
    if (sat.ping > 0.01f) {
      float e = easeOut(fmodf(t, 1.8f) / 1.8f), k = 1 + 2.4f * e;
      gfx::ringAt(x, y, 8 * k, 1.8f * k, c, 0.8f * (1 - e) * sat.ping * a);
    }
    float since = (nowMs - sat.flashAt) / 1000.f;
    if (since < 1.2f) {
      float e = easeOut(since / 1.2f);
      gfx::ringAt(x, y, 10 + 30 * e, 4 - 2 * e, gfx::c565(sat.flashColor), 0.9f * (1 - e) * a);
    }
    halo::satellite(x, y, 8.5f, c, sat.lit, cur.sessions[i].stale, a);
    // Which satellite the lines under the orb describe: a fine double ring
    if (sat.focus > 0.01f && cur.count > 1) {
      uint16_t w = gfx::c565(TEXT);
      gfx::ringAt(x, y, 14.5f, 1.2f, w, 0.55f * sat.focus * a);
      gfx::ringAt(x, y, 19.5f, 1, w, 0.22f * sat.focus * a);
    }
  }
}

// The orb's celebration of a finished turn: rings and sparks in green
void drawCheer(float cx, float cy, float R, float c, float a) {
  uint16_t green = gfx::c565(DONE_PALETTE.m1), light = gfx::c565(DONE_PALETTE.m2);
  for (int k = 0; k < 3; k++) {
    float tk = c - k * 0.18f;
    if (tk <= 0 || tk >= 1.2f) continue;
    float e = easeOut(tk / 1.2f);
    gfx::ringAt(cx, cy, R * (1.04f + 0.9f * e), 1.5f + 5 * (1 - e), green, 0.75f * (1 - e) * a);
  }
  if (c < 1.5f) {
    float e = easeOut(c / 1.5f);
    for (int k = 0; k < 12; k++) {
      float t = k / 12.f + c * 0.05f + (k % 2) * 0.02f;
      float d = R + 8 + (k % 2 ? 56 : 78) * e;
      gfx::discAt(cx + sinf(t * gfx::TAU) * d, cy - cosf(t * gfx::TAU) * d, 3.5f * (1 - e * 0.7f), light, (1 - e) * a);
    }
  }
}

// And its wince at a failed one: two red shock rings, close and quick
void drawWince(float cx, float cy, float R, float c, float a) {
  uint16_t red = gfx::c565(FAIL_PALETTE.m1);
  for (int k = 0; k < 2; k++) {
    float tk = c - k * 0.12f;
    if (tk <= 0 || tk >= 0.8f) continue;
    float e = easeOut(tk / 0.8f);
    gfx::ringAt(cx, cy, R * (1.04f + 0.55f * e), 1.5f + 6 * (1 - e), red, 0.8f * (1 - e) * a);
  }
}

// One session's two lines under the orb, cx across, dy down, at alpha a
void captionOf(const Session &s, float cx, float dy, float a, bool current, int64_t since) {
  float sx, sy;
  shift(sx, sy);
  int y1 = 322 + int(sy + dy), y2 = 364 + int(sy + dy);
  title(titleOf(s), 4, cx, 233 + sx, y1, 296, gfx::c565(TEXT), a, since);

  char lines[3][96], line[96];
  int n = stateLines(s, lines);
  pickLine(lines, n, LABEL, 3, 284, line, sizeof line);
  uint16_t color = gfx::c565(toneText(tone(s)));
  bool still = fabsf(cx - 233 - sx) < 0.5f;
  if (!current || !still) {
    int w = text::width(LABEL, 3, line);
    text::draw(LABEL, 3, line, cx - w / 2.f, y2, color, a, int(233 + sx) - 150, int(233 + sx) + 150, 20);
    return;
  }
  // The state line rolls its digits as a timer ticks, and fades through a change of state
  if (strcmp(cap.line, line)) {
    if (cap.line[0]) {
      snprintf(cap.was, sizeof cap.was, "%s", cap.line);
      cap.rolls = text::digitsOnly(line, cap.was);
      cap.changed = nowMs;
    }
    snprintf(cap.line, sizeof cap.line, "%s", line);
  }
  float t = (nowMs - cap.changed) / (cap.rolls ? 260.f : 320.f);
  if (t >= 1) {
    text::centre(LABEL, 3, line, cx, y2, color, a);
  } else if (cap.rolls) {
    text::roll(LABEL, 3, line, cap.was, easeOut(t), cx, y2, color, a);
  } else {
    float e = easeOut(t);
    text::centre(LABEL, 3, cap.was, cx, y2 - int(8 * e), color, a * (1 - e));
    text::centre(LABEL, 3, line, cx, y2 + int(8 * (1 - e)), color, a * e);
  }
}

void homeBack(const halo::Palette &p, float ox, float oy, float R, float a, float cheer, float wince) {
  halo::scale(nowMs / 1000.f / 140, gfx::mix(TEXT, p.m1, 0.8f), a, nowMs / 1000.f / 9);
  gfx::ring(halo::R_TRACK, 1.5f, gfx::c565(gfx::mix(BORDER_2, p.m1, 0.45f)), a);
  if (linkState == ONLINE) drawSatellites(halo::R_TRACK, a);
  if (rippleK > 0.01f) {
    float e = easeOut(fmodf(nowMs / 1000.f, 2.2f) / 2.2f), k = 1 + 0.5f * e;
    gfx::ringAt(ox, oy, (R + 1.6f) * k, 3 * k, gfx::c565(p.m1), 0.55f * (1 - e) * rippleK * a);
  }
  if (cheer >= 0) drawCheer(ox, oy, R, cheer, a);
  if (wince >= 0) drawWince(ox, oy, R, wince, a);
}

void homeText(float a, float e) {
  float sx, sy;
  shift(sx, sy);
  float cx = 233 + sx;
  float k0 = piece(e, 2), k1 = piece(e, 3);
  if (linkState != ONLINE) {
    text::centre(LABEL, 4, linkState == CONNECTING ? "CONNECTING…" : "LINK LOST", cx, 322 + int(sy + 16 * (1 - k0)),
                 gfx::c565(TEXT_2), a * k0);
    text::centre(LABEL, 3, linkState == CONNECTING ? "AGENTSMONITOR" : "RECONNECTING…", cx, 364 + int(sy + 16 * (1 - k1)),
                 gfx::c565(TEXT_3), a * k1);
    return;
  }
  const Session *s = focused();
  if (!s) {
    text::centre(LABEL, 4, "NO SESSIONS", cx, 338 + int(sy + 16 * (1 - k0)), gfx::c565(TEXT_3), a * k0);
    return;
  }
  float dy = 16 * (1 - k0);
  float off = cap.off.x, f = gfx::clamp01(cap.fade.x);
  // Swiped: the neighbour on the side it comes from slides in alongside
  if (fabsf(off) > 0.5f && cur.count > 1) {
    int i = indexOf(s->key), j = (i + (off < 0 ? 1 : cur.count - 1)) % cur.count;
    float nx = cx + off + (off < 0 ? SLIDE : -SLIDE);
    captionOf(cur.sessions[j], nx, dy, a * gfx::clamp01(fabsf(off) / SLIDE * 1.5f), false, nowMs);
  }
  if (f < 1) {
    // A change not made by a finger: the old lines lift away, then the new rise into place
    float out = gfx::clamp01(f / 0.45f), in = gfx::clamp01((f - 0.3f) / 0.7f);
    int p = indexOf(cap.prevKey);
    if (p >= 0 && out < 1) captionOf(cur.sessions[p], cx + off, dy - 14 * out, a * (1 - out), false, nowMs);
    if (in > 0) captionOf(*s, cx + off, dy + 14 * (1 - in), a * in * k0, true, cap.since);
    return;
  }
  captionOf(*s, cx + off, dy, a * k0, true, cap.since);
}

// ---------- Alert ----------

// The waiting session the card shows: the newest first, then each in turn
const Session *alertSession(int *pick) {
  if (!waitCount) return nullptr;
  int k = 0;
  for (int w = 0; w < waitCount; w++)
    if (!strcmp(waits[w].key, focusKey)) k = w;
  if (waitCount > 1) k = (k + int((nowMs - alertAt) / ROTATE_MS)) % waitCount;
  *pick = k;
  int i = indexOf(waits[k].key);
  return i < 0 ? nullptr : &cur.sessions[i];
}

void alertBack(float a) {
  float t = nowMs / 1000.f;
  uint16_t amber = gfx::c565(TONE_COLORS[ATTN]);
  float breath = 0.82f + 0.18f * sinf(t * gfx::TAU / 2.4f);
  // Amber light rising toward the rim, a fine ring, and a comet running round it
  gfx::vignette(150, 233, amber, 0.34f * a * breath);
  gfx::ring(231, 1.4f, amber, 0.45f * a);
  float head = t / 3.2f;
  gfx::arc(231, 3.6f, head - 0.24f, head, amber, 0, 0.95f * a);
  float hx = 233 + sinf(head * gfx::TAU) * 231, hy = 233 - cosf(head * gfx::TAU) * 231;
  gfx::glowAt(hx, hy, 18, amber, 0.7f * a);
  gfx::discAt(hx, hy, 2.6f, gfx::c565(hex(0xfff2d6)), a);
}

void alertText(float a, float e) {
  int pick = 0;
  const Session *s = alertSession(&pick);
  if (!s) return;
  float sx, sy;
  shift(sx, sy);
  float cx = 233 + sx;
  uint16_t amber = gfx::c565(TONE_COLORS[ATTN]);
  auto lift = [&](int k) { return 18 * (1 - piece(e, k)); };
  auto fade = [&](int k) { return a * piece(e, k); };

  const char *heading = s->wait == Wait::Input ? "NEEDS AN ANSWER" : s->wait == Wait::Idle ? "WAITING FOR YOU" : "NEEDS APPROVAL";
  text::centre(LABEL, 4, heading, cx, 158 + int(sy + lift(1)), amber, fade(1));
  static char shownKey[96];
  static int64_t shownAt;
  if (strcmp(shownKey, s->key)) snprintf(shownKey, sizeof shownKey, "%s", s->key), shownAt = nowMs;
  title(titleOf(*s), 4, cx, cx, 212 + int(sy + lift(2)), 340, gfx::c565(TEXT), fade(2), shownAt);

  // What it waits on: the tool and its subject, or the question
  char what[480], tool[64], lines[2][160];
  toolName(s->waitTool[0] ? s->waitTool : s->tool, tool, sizeof tool);
  bool command = s->wait == Wait::Permission;
  if (command)
    snprintf(what, sizeof what, "%s%s%s", tool, tool[0] && s->waitText[0] ? " " : "", s->waitText);
  else
    snprintf(what, sizeof what, "%s", s->waitText);
  float bottom = 236 + sy;
  if (what[0]) {
    text::Face face = command ? MONO : PROP;
    int n = text::wrap(face, 3, what, 296, lines, 2);
    float top = 240 + sy + lift(3), h = 24 + n * 32.f;
    gfx::roundRect(68 + sx, top, 398 + sx, top + h, 16, gfx::c565(CARD), fade(3));
    // A fine lit edge along the top, as if the card caught the amber light
    gfx::roundRect(86 + sx, top, 380 + sx, top + 1.2f, 0, gfx::c565(gfx::scale(TONE_COLORS[ATTN], 0.55f)), fade(3));
    for (int k = 0; k < n; k++) text::draw(face, 3, lines[k], 84 + sx, int(top + 12 + 21 + k * 32), gfx::c565(TEXT_2), fade(3));
    bottom = top + h - lift(3);
  }
  char wait[24], foot[140], project[64];
  duration(wallMs - waitSince(*s), wait, sizeof wait);
  text::fit(PROP, 3, s->project, 250, project, sizeof project);
  snprintf(foot, sizeof foot, "%s · %s", project, wait);
  text::centre(PROP, 3, foot, cx, int(bottom + 42 + lift(4)), gfx::c565(TEXT_3), fade(4));

  if (waitCount > 1) {
    float x0 = cx - (waitCount - 1) * 12;
    for (int k = 0; k < waitCount; k++)
      gfx::discAt(x0 + k * 24, 414 + sy + lift(5), k == pick ? 5.5f : 4, k == pick ? amber : gfx::c565(TEXT_4), fade(5));
  }
}

// ---------- List ----------

constexpr int ROW_H = 58, LIST_TOP = 138, LIST_CLIP0 = 126, LIST_CLIP1 = 452;

float listMax() { return cur.count * ROW_H > LIST_CLIP1 - LIST_TOP - 20 ? float(cur.count * ROW_H - (LIST_CLIP1 - LIST_TOP - 20)) : 0; }

int rowAt(int y) {
  int i = int(floorf((y + listScroll - LIST_TOP) / ROW_H));
  return i >= 0 && i < cur.count && y >= LIST_CLIP0 && y < LIST_CLIP1 ? i : -1;
}

void listLayer(float a, float e) {
  float sx, sy;
  shift(sx, sy);
  // The count; the orb above it already tells the mood
  char head[64];
  if (cur.count)
    snprintf(head, sizeof head, "%d SESSION%s", cur.count, cur.count == 1 ? "" : "S");
  else
    snprintf(head, sizeof head, "NO SESSIONS");
  float k0 = piece(e, 1);
  text::centre(LABEL, 3, head, 233 + sx, 106 + int(sy + 14 * (1 - k0)), gfx::c565(TEXT_3), a * k0);

  int pressed = drag == Drag::Undecided && to == LIST ? rowAt(downY) : -1;
  for (int i = 0; i < cur.count; i++) {
    const Session &s = cur.sessions[i];
    float k = piece(e, 5 + i);  // rows wait for the orb to pass on its way up
    float cy = LIST_TOP + i * ROW_H + ROW_H / 2.f - listScroll + sy + 26 * (1 - k);
    if (cy < LIST_CLIP0 - ROW_H || cy > LIST_CLIP1 + ROW_H) continue;
    // Rows fade out toward the top and bottom of the list
    float alpha = gfx::clamp01(fminf(cy - LIST_CLIP0, LIST_CLIP1 - cy) / 32) * a * k;
    if (alpha <= 0.01f) continue;
    float dy = cy - gfx::CY, half = sqrtf(fmaxf(0, 233 * 233 - dy * dy)) - 40;
    float x0 = gfx::CX - half + sx, x1 = gfx::CX + half + sx;
    Tone t = tone(s);
    if (t == ATTN) gfx::roundRect(x0 - 14, cy - 24, x1 + 14, cy + 24, 14, gfx::c565(WAIT_ROW), alpha);
    if (i == pressed) gfx::roundRect(x0 - 14, cy - 24, x1 + 14, cy + 24, 14, gfx::c565(TEXT), 0.08f * alpha);
    // A hairline between rows
    if (i > 0 && t != ATTN && tone(cur.sessions[i - 1]) != ATTN)
      gfx::roundRect(x0 + 24, cy - ROW_H / 2.f - 0.5f, x1, cy - ROW_H / 2.f + 0.5f, 0, gfx::c565(TEXT_4), 0.35f * alpha);
    if (s.stale)
      gfx::ringAt(x0 + 8, cy, 6.5f, 2, gfx::c565(TONE_COLORS[t]), alpha);
    else {
      gfx::glowAt(x0 + 8, cy, 18, gfx::c565(TONE_COLORS[t]), 0.35f * alpha * (t == REST ? 0.3f : 1));
      gfx::discAt(x0 + 8, cy, 6.5f, gfx::c565(TONE_COLORS[t]), alpha);
    }

    // On the right: how long it waits, what it runs, or how long since it finished
    char side[64], word[64];
    if (isWaiting(s)) {
      duration(wallMs - waitSince(s), side, sizeof side);
    } else if (s.stale) {
      snprintf(side, sizeof side, "—");
    } else if (s.status == Status::Tool) {
      toolName(s.tool, word, sizeof word);
      text::fit(LABEL, 3, word, 84, side, sizeof side);
    } else if (model::working(s.status)) {
      snprintf(side, sizeof side, "%s", s.status == Status::Compacting ? "PACK" : "THINK");
    } else {
      age(wallMs - (s.endedAt ? s.endedAt : s.since), side, sizeof side, false);
    }
    int sideW = text::width(LABEL, 3, side);
    text::draw(LABEL, 3, side, x1 - sideW, int(cy + 10), gfx::c565(toneText(t)), alpha);
    char name[160];
    text::fit(PROP, 3, titleOf(s), int(x1 - sideW - 16 - (x0 + 26)), name, sizeof name);
    text::draw(PROP, 3, name, x0 + 26, int(cy + 10), gfx::c565(t == REST || t == MUTED ? TEXT_2 : TEXT), alpha);
  }
}

// ---------- Detail ----------

// "950", "12.3K", "845K", "1.24M", "12.5M": the dashboard's compact counts, in capitals
void count(uint64_t n, char *out, size_t len) {
  if (n < 1000)
    snprintf(out, len, "%u", unsigned(n));
  else if (n < 1000000)
    snprintf(out, len, n < 10000 ? "%.1fK" : "%.0fK", n / 1e3);
  else if (n < 1000000000)
    snprintf(out, len, n < 10000000 ? "%.2fM" : "%.1fM", n / 1e6);
  else
    snprintf(out, len, "%.2fB", n / 1e9);
}

bool hasUsage(const Session &s) { return s.tokens || s.context >= 0; }

// What a session has used, under its state line from `top`: its tokens and
// how full its context is, side by side, the context over a row of dots that
// fill, turning amber at 70% and red at 90% as the dashboard's meters do. It
// needs the title held to two lines.
void usage(const Session &s, float cx, int top, float a) {
  char tokens[24] = "", context[16] = "";
  if (s.tokens) {
    char n[20];
    count(s.tokens, n, sizeof n);
    snprintf(tokens, sizeof tokens, "%s%s", s.tokensExact ? "" : "~", n);  // ~ for an estimate
  }
  float pct = s.context;
  if (pct >= 0) snprintf(context, sizeof context, "%d%%", int(pct + 0.5f));
  if (!tokens[0] && !context[0]) return;

  constexpr int DOTS = 12;
  constexpr float PITCH = 10, GAP = 48;
  struct Column {
    const char *value;
    bool dots;
    float w;
  } cols[2];
  int n = 0;
  if (tokens[0]) cols[n++] = {tokens, false, 0};
  if (context[0]) cols[n++] = {context, true, 0};
  float total = GAP * (n - 1);
  for (int k = 0; k < n; k++) {
    Column &c = cols[k];
    c.w = text::width(PROP, 4, c.value);
    if (c.dots && c.w < DOTS * PITCH) c.w = DOTS * PITCH;
    total += c.w;
  }
  uint16_t lit = gfx::c565(TONE_COLORS[pct >= 90 ? CRITICAL : pct >= 70 ? ATTN : ACTIVE]), unlit = gfx::c565(TEXT_4);
  float x = cx - total / 2;
  for (int k = 0; k < n; k++) {
    const Column &c = cols[k];
    float mid = x + c.w / 2;
    text::centre(PROP, 4, c.value, mid, top + 62, gfx::c565(TEXT_2), a);
    if (c.dots) {
      int on = int(pct / 100 * DOTS + 0.5f);
      if (pct > 0 && on < 1) on = 1;
      float x0 = mid - (DOTS - 1) * PITCH / 2;
      for (int d = 0; d < DOTS; d++) gfx::discAt(x0 + d * PITCH, top + 82.f, 2.8f, d < on ? lit : unlit, d < on ? a : 0.6f * a);
    }
    x += c.w + GAP;
  }
}

void detailLayer(float a, float e) {
  float sx, sy;
  shift(sx, sy);
  float cx = 233 + sx;
  auto lift = [&](int k) { return int(sy + 18 * (1 - piece(e, k))); };
  auto fade = [&](int k) { return a * piece(e, k); };
  int i = indexOf(detailKey);
  if (i < 0) {
    text::centre(LABEL, 4, "SESSION ENDED", cx, 250 + lift(1), gfx::c565(TEXT_3), fade(1));
    return;
  }
  const Session &s = cur.sessions[i];
  Tone t = tone(s);
  const char *agent = !strcmp(s.agent, "claude-code") ? "CLAUDE" : !strcmp(s.agent, "codex") ? "CODEX" : s.agent;
  char project[64];
  text::fit(PROP, 3, s.project, 280, project, sizeof project);
  text::centre(LABEL, 3, agent, cx, 108 + lift(1), gfx::c565(TEXT_3), fade(1));
  text::centre(PROP, 3, project, cx, 138 + lift(1), gfx::c565(TEXT_4), fade(1));

  // Up to three lines of title, or two leaving room for what it has used
  bool used = !isWaiting(s) && hasUsage(s);
  char lines[3][160];
  int n = text::wrap(PROP, 4, titleOf(s), 340, lines, used ? 2 : 3);
  int y = 196;
  for (int k = 0; k < n; k++, y += 46) text::centre(PROP, 4, lines[k], cx, y + lift(2), gfx::c565(TEXT), fade(2));

  char states[3][96], state[96];
  int m = stateLines(s, states);
  pickLine(states, m, LABEL, 3, 360, state, sizeof state);
  text::centre(LABEL, 3, state, cx, y + 4 + lift(3), gfx::c565(toneText(t)), fade(3));

  // What it has used, or what it waits on
  if (used) usage(s, cx, y + 4 + lift(4), fade(4));
  if (!isWaiting(s)) return;
  char what[480], tool[64], box[3][160];
  toolName(s.waitTool[0] ? s.waitTool : s.tool, tool, sizeof tool);
  bool command = s.wait == Wait::Permission;
  if (command)
    snprintf(what, sizeof what, "%s%s%s", tool, tool[0] && s.waitText[0] ? " " : "", s.waitText);
  else
    snprintf(what, sizeof what, "%s", s.waitText);
  if (!what[0]) return;
  text::Face face = command ? MONO : PROP;
  float boxTop = y + 30.f + lift(4);
  int room = int((424 - boxTop - 24) / 32);  // lines that fit above the bottom of the round
  if (room < 1) return;
  int k = text::wrap(face, 3, what, 296, box, room < 3 ? room : 3);
  float h = 24 + k * 32.f;
  gfx::roundRect(68 + sx, boxTop, 398 + sx, boxTop + h, 16, gfx::c565(CARD), fade(4));
  for (int r = 0; r < k; r++) text::draw(face, 3, box[r], 84 + sx, int(boxTop + 12 + 21 + r * 32), gfx::c565(TEXT_2), fade(4));
}

// ---------- The frame ----------

// A soft light where a finger presses, that blooms out when it lets go
void drawPress() {
  uint16_t w = gfx::c565(TEXT);
  if (drag == Drag::Undecided) {
    float e = easeOut(gfx::clamp01((nowMs - pressAt) / 350.f));
    gfx::glowAt(pressX, pressY, 30 + 14 * e, w, 0.14f * e);
    gfx::ringAt(pressX, pressY, 10 + 16 * e, 1.4f, w, 0.25f * e);
    return;
  }
  float t = (nowMs - releaseAt) / 450.f;
  if (t < 0 || t >= 1) return;
  float e = easeOut(t);
  if (pressTapped) gfx::ringAt(pressX, pressY, 26 + 44 * e, 2 - e, w, 0.3f * (1 - e));
  gfx::glowAt(pressX, pressY, 44, w, 0.14f * (1 - e));
}

void compose() {
  float sx, sy;
  shift(sx, sy);
  float home = present(HOME);

  // The orb, between the places the two screens keep it
  Anchor a = anchor(to), b = anchor(from);
  float t = from == to ? 1 : trans.x;
  float ox = lerp(b.x, a.x, t) + sx, oy = lerp(b.y, a.y, t) + sy, R = pulsed(lerp(b.r, a.r, t));

  // A finished turn: green that comes on fast, holds, then glides back, with
  // a bounce. A failed one: red, a shake of the head and a flinch. The later
  // wins; both play out on the home screen.
  halo::Palette p = pal;
  float c = (nowMs - cheerAt) / 1000.f, f = (nowMs - winceAt) / 1000.f;
  bool cheering = linkState == ONLINE && c >= 0 && c < CHEER_S && cheerAt >= winceAt;
  bool wincing = linkState == ONLINE && f >= 0 && f < WINCE_S && winceAt > cheerAt;
  float shake = 0;
  if (cheering) {
    float w = c < 0.25f ? smooth(c / 0.25f) : c < 1.3f ? 1 : 1 - smooth((c - 1.3f) / (CHEER_S - 1.3f));
    p = halo::mix(p, DONE_PALETTE, w * home);
    R *= 1 + 0.09f * sinf(c * gfx::TAU * 1.6f) * expf(-c * 2.8f) * home;
  }
  if (wincing) {
    float w = f < 0.15f ? smooth(f / 0.15f) : f < 1.0f ? 1 : 1 - smooth((f - 1.0f) / (WINCE_S - 1.0f));
    p = halo::mix(p, FAIL_PALETTE, w * home);
    shake = 7 * sinf(f * gfx::TAU * 8) * expf(-f * 4.5f) * home;
    R *= 1 - 0.05f * expf(-f * 5) * home;
  }
  ox += shake;
  // On a session's page the orb takes the session's colour
  float detail = present(DETAIL);
  if (detail > 0) {
    int i = indexOf(detailKey);
    if (i >= 0) p = halo::mix(p, tonePalette(tone(cur.sessions[i])), detail);
  }

  // Back to front: the sky and the home glow, each screen's backdrop, the orb,
  // then text. The orb covers its inside, so nothing under it is drawn.
  gfx::tag(P_BACK);
  halo::background(p.m1, glowK * home, ox, oy, R - 4);
  gfx::tag(P_SKY);
  halo::stars(nowMs / 1000.f, 0.9f);
  gfx::tag(P_RIM);
  if (home > 0.01f) homeBack(p, ox, oy, R, shown(HOME), cheering ? c : -1, wincing ? f : -1);
  if (present(ALERT) > 0.01f) alertBack(shown(ALERT));
  gfx::tag(P_GLOW);
  if (home < 0.99f) gfx::glowAt(ox, oy, R * 2.6f, gfx::c565(p.m1), 0.5f * (1 - home), R - 4);
  // Some of the blobs a frame or two behind, unless a cheer or a wince swirls
  // them fast
  gfx::tag(P_ORB);
  halo::orb(ox, oy, R, p, motion, !cheering && !wincing);
  gfx::tag(P_HOME);
  if (home > 0.01f) homeText(shown(HOME), arrived(HOME));
  gfx::tag(P_ALERT);
  if (present(ALERT) > 0.01f) alertText(shown(ALERT), arrived(ALERT));
  gfx::tag(P_LIST);
  if (present(LIST) > 0.01f) listLayer(shown(LIST), arrived(LIST));
  gfx::tag(P_DETAIL);
  if (present(DETAIL) > 0.01f) detailLayer(shown(DETAIL), arrived(DETAIL));
  gfx::tag(P_PRESS);
  drawPress();
}

// ---------- Touch ----------

void tap(int y) {
  pressTapped = true;
  // Any tap answers the reminders of the waits on screen
  tracker.ack();
  switch (to) {
    case ALERT:
      alertUntil = nowMs;
      go(HOME);
      break;
    case HOME:
      if (const Session *s = focused()) {
        snprintf(detailKey, sizeof detailKey, "%s", s->key);
        go(DETAIL);
      }
      break;
    case LIST: {
      int i = rowAt(y);
      if (i >= 0) {
        snprintf(detailKey, sizeof detailKey, "%s", cur.sessions[i].key);
        go(DETAIL);
      }
      break;
    }
    case DETAIL: go(backTo); break;
  }
}

// The finger's speed over about its last 60 ms, px/s
void velocity(float &vx, float &vy) {
  vx = vy = 0;
  if (sampleCount < 2) return;
  const Sample &last = samples[(sampleCount - 1) % 6];
  int oldest = sampleCount > 6 ? sampleCount - 6 : 0, k = sampleCount - 2;
  while (k > oldest && last.ms - samples[k % 6].ms < 60) k--;
  const Sample &s = samples[k % 6];
  float dt = (last.ms - s.ms) / 1000.f;
  if (dt > 0) vx = (last.x - s.x) / dt, vy = (last.y - s.y) / dt;
}

// How far a finger has carried a move between screens
float carried(int dx, int dy) {
  switch (moveFrom) {
    case HOME: return -dy / MOVE_PX;
    case LIST: return dy / MOVE_PX;
    case DETAIL: return (dx > dy ? dx : dy) / MOVE_PX;
    default: return sqrtf(float(dx * dx + dy * dy)) / MOVE_PX;
  }
}

// Picks what a drag is for, once the finger has moved far enough to tell
void decide(int dx, int dy) {
  bool across = abs(dx) > abs(dy);
  Drag d = Drag::None;
  Screen target = to;
  switch (to) {
    case HOME:
      if (across && cur.count > 0) d = Drag::Caption;
      else if (!across && dy < 0) d = Drag::Move, target = LIST;
      break;
    case LIST:
      if (!across && dy > 0 && listScroll <= 0) d = Drag::Move, target = HOME;
      else if (!across) d = Drag::Scroll;
      break;
    case DETAIL:
      if (dx > 0 || dy > 0) d = Drag::Move, target = backTo;
      break;
    case ALERT:
      d = Drag::Move, target = HOME;
      tracker.ack();
      break;
  }
  drag = d;
  releaseAt = nowMs;  // the press light gives way
  pressTapped = false;
  if (d == Drag::Move) {
    moveFrom = to;
    if (target == HOME && to == ALERT) alertUntil = nowMs;
    go(target, true);
  }
}

}  // namespace

bool init(const uint8_t *cjk, size_t cjkLen) { return gfx::init() && halo::init() && text::init(cjk, cjkLen); }

void link(Link state) {
  if (state != ONLINE) {
    // Nothing on screen is current any more
    haveSummary = false;
    tracker.clear();
    focusKey[0] = 0;
    drag = Drag::None;
    go(HOME);
  }
  linkState = state;
}

bool summary(const char *json) {
  static model::Summary next;
  if (!model::parse(json, next)) return false;
  // The daemon's clock, taken again only when it has moved: a summary read
  // twice says the same time twice
  if (next.ts != summaryTs) summaryAt = nowMs, summaryTs = next.ts;
  if (!wallMs) wallMs = next.ts;
  follow(next);
  syncSats();
  return true;
}

void touch(Touch kind, int x, int y, int64_t ms) {
  lastTouch = ms;
  int dx = x - downX, dy = y - downY;
  switch (kind) {
    case DOWN:
      drag = Drag::Undecided;
      downX = lastX = x, downY = lastY = y, downAt = ms;
      pressAt = nowMs, pressX = float(x), pressY = float(y);
      scrollAtDown = listScroll;
      listVel = 0;
      sampleCount = 0;
      samples[sampleCount++ % 6] = {x, y, ms};
      break;
    case MOVE: {
      if (drag == Drag::None) break;
      samples[sampleCount++ % 6] = {x, y, ms};
      lastX = x, lastY = y;
      if (drag == Drag::Undecided && dx * dx + dy * dy > 10 * 10) decide(dx, dy);
      if (drag == Drag::Caption) {
        cap.off.x = cur.count > 1 ? float(dx) : dx * 0.25f;  // with one session it only gives a little
        cap.off.v = 0;
      } else if (drag == Drag::Move) {
        float c = carried(dx, dy);
        trans.x = c < 0 ? 0 : c > 1.05f ? 1.05f : c;
        trans.v = 0;
      } else if (drag == Drag::Scroll) {
        float raw = scrollAtDown - dy, max = listMax();
        // Past either end the list gives less and less
        listScroll = raw < 0 ? raw * 0.4f : raw > max ? max + (raw - max) * 0.4f : raw;
      }
      break;
    }
    case UP: {
      if (drag == Drag::None) break;
      samples[sampleCount++ % 6] = {x, y, ms};
      float vx, vy;
      velocity(vx, vy);
      Drag was = drag;
      drag = Drag::None;
      releaseAt = nowMs;
      if (was == Drag::Undecided) {
        pressTapped = false;
        if (dx * dx + dy * dy < 14 * 14 && ms - downAt < 600) tap(y);
      } else if (was == Drag::Caption) {
        bool next = cur.count > 1 && (dx < -70 || vx < -500), prev = cur.count > 1 && (dx > 70 || vx > 500);
        if (next || prev) {
          const Session *f = focused();
          int i = f ? indexOf(f->key) : 0;
          i = (i + (next ? 1 : cur.count - 1)) % cur.count;
          choose(cur.sessions[i].key);
          // The new title is already on screen, sliding in; it carries on from there
          snprintf(cap.key, sizeof cap.key, "%s", cur.sessions[i].key);
          cap.fade.x = 1, cap.fade.v = 0;
          cap.since = nowMs;
          cap.line[0] = 0;
          cap.off.x = dx + (next ? SLIDE : -SLIDE);
        }
        cap.off.v = vx;
      } else if (was == Drag::Move) {
        float c = carried(dx, dy);
        float speed = carried(int(vx), int(vy));  // moves per second, in the move's direction
        if (c > 0.35f || speed > 2.5f) {
          driving = false;
          trans.v = speed > 0 ? speed : 0;
        } else {
          go(moveFrom);  // not far enough: back it goes
          trans.v = speed < 0 ? -speed : 0;
        }
      } else if (was == Drag::Scroll) {
        listVel = -vy;
        if (listVel > 4000) listVel = 4000;
        if (listVel < -4000) listVel = -4000;
      }
      break;
    }
  }
}

void frame(int64_t ms, int64_t wall) {
  nowMs = ms;
  float dt = lastMs ? (ms - lastMs) / 1000.f : 0;
  if (dt > 0.1f) dt = 0.1f;
  lastMs = ms;
  if (wall) {
    wallMs = wall;
  } else {
    // Kept by the summaries, and never turned back by one that was slower to
    // arrive than the last: timers on screen only count up. A minute or more
    // back is the clock itself being set.
    int64_t w = summaryTs + (ms - summaryAt);
    if (w > wallMs || wallMs - w > 60000) wallMs = w;
  }
  step(dt);
  halo::newFrame(dt);
  text::newFrame();
  uint32_t t0 = gfx::ticks();
  gfx::record(true);
  compose();
  gfx::record(false);
  recTicks += gfx::ticks() - t0;
  partFrames++;
}

void draw(uint16_t *band, int y0, int y1) {
  uint32_t spent[gfx::TAGS] = {};
  gfx::play(band, y0, y1, spent);
  for (int p = 0; p < PARTS; p++)
    if (spent[p]) partTicks[p] += spent[p];
}

int cue() {
  if (cueHead == cueTail) return -1;
  int c = cues[cueHead];
  cueHead = (cueHead + 1) % 8;
  return c;
}

const char *screen() {
  static const char *const NAMES[] = {"home", "alert", "list", "detail"};
  return NAMES[to];
}

void timings(char *out, size_t len) {
  float k = partFrames ? 1 / (gfx::ticksPerMs() * partFrames) : 0;
  size_t n = snprintf(out, len, "rec %.1f grid %.1f ", recTicks * k, halo::gridTicks * k);
  recTicks = halo::gridTicks = 0;
  for (int p = 0; p < PARTS && n < len; p++) n += snprintf(out + n, len - n, "%s %.1f ", PART_NAMES[p], partTicks[p].exchange(0) * k);
  if (n < len) snprintf(out + n, len - n, "list %uK", unsigned(gfx::listUsed() + 1023) / 1024);
  partFrames = 0;
}

}  // namespace ui
