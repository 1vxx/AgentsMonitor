#ifndef __clang__
#pragma GCC optimize("O2")
#endif

// Screens: HOME (the halo, with the focused session's title and state under
// the orb), ALERT (the card that takes over when a session starts waiting for
// you), LIST (every session, swiped up from home) and DETAIL (one session).
//
// What happened is read off the difference between two summaries: a session
// that starts waiting chimes and takes over the screen for ten seconds, a turn
// that ends chimes once, softly for an answer and low for an error, and turns
// the orb green or red for a moment. A wait nobody answers chimes again at 2
// and 5 minutes, then every 10, until a tap.
//
// All text is in the pixel font; labels are in capitals, titles as given.

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "halo.h"
#include "model.h"
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
using text::MONO;
using text::PROP;

// ---------- Look ----------

const RGB TEXT = hex(0xeceaf6), TEXT_2 = hex(0xb4b4cb), TEXT_3 = hex(0x7d7e9a), TEXT_4 = hex(0x4c4d66);
const RGB BORDER_2 = hex(0x1c1f2b), CARD = hex(0x1c1a24), WAIT_ROW = hex(0x2b2110);

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
constexpr int64_t FADE_MS = 200;
constexpr int64_t SHIFT_MS = 3 * 60000;  // anti burn-in nudge
constexpr float CHEER_S = 2.6f;          // the orb's celebration of a finished turn
constexpr float WINCE_S = 2.2f;          // and its wince at a failed one

// ---------- State ----------

model::Summary cur;
bool haveSummary;
Link linkState = CONNECTING;
int64_t summaryAt, summaryTs;  // our clock and the daemon's when the last one came

enum Screen { HOME, ALERT, LIST, DETAIL };
Screen scr = HOME, back = HOME;
int64_t screenAt, lastTouch;
char focusKey[96], detailKey[96];
int64_t focusAt;
float listScroll;

struct Sat {
  char key[96];
  bool used;
  float turn, speed, tail, ping, lit, focus;
  RGB color;
  int64_t flashAt;
  RGB flashColor;
};
Sat sats[model::MAX];

struct Waiting {
  char key[96];
  int64_t since;
  int reminded;
  bool acked;
};
Waiting waits[model::MAX];
int waitCount;
int64_t alertUntil, alertAt;

int cues[8], cueHead, cueTail;

halo::Palette pal = halo::palette(Mood::Offline);
halo::Motion motion;
float spinSpeed, pulsePhase, pulseK, breatheK, rippleK, glowK = 0.5f;
int64_t lastMs, nowMs, wallMs, cheerAt = -100000, winceAt = -100000;

// Touch
bool pressed, dragged;
int downX, downY, lastY;
int64_t downAt;
float scrollAtDown;

// ---------- Helpers ----------

uint32_t hash(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ uint8_t(*s++)) * 16777619u;
  return h;
}

inline float smooth(float v) { return v * v * (3 - 2 * v); }
inline float easeOut(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }

void pushCue(synth::Cue c) {
  int next = (cueTail + 1) % 8;
  if (next == cueHead) return;
  cues[cueTail] = c;
  cueTail = next;
}

void go(Screen s) {
  if (s == scr) return;
  back = scr == DETAIL ? back : scr;
  scr = s;
  screenAt = nowMs;
}

void choose(const char *key) {
  snprintf(focusKey, sizeof focusKey, "%s", key);
  focusAt = nowMs;
}

const Session *find(const model::Summary &sum, const char *key) {
  for (int i = 0; i < sum.count; i++)
    if (!strcmp(sum.sessions[i].key, key)) return &sum.sessions[i];
  return nullptr;
}

int indexOf(const char *key) {
  for (int i = 0; i < cur.count; i++)
    if (!strcmp(cur.sessions[i].key, key)) return i;
  return -1;
}

bool isWaiting(const Session &s) { return model::waiting(s.status) && !s.stale; }
int64_t waitSince(const Session &s) { return s.waitSince ? s.waitSince : s.since; }

Tone tone(const Session &s) {
  if (s.stale) return MUTED;
  if (model::waiting(s.status)) return ATTN;
  if (model::working(s.status)) return ACTIVE;
  if (s.outcome == Outcome::Error || s.outcome == Outcome::Refusal) return CRITICAL;
  if (s.outcome == Outcome::Answer && wallMs - s.endedAt < GOOD_FOR_MS) return GOOD;
  return REST;
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

// The first of the lines that fits maxW at `scale`, else the last one cut to fit
void pickLine(char (*lines)[96], int n, int scale, int maxW, char *out, size_t outLen) {
  for (int k = 0; k < n; k++)
    if (text::width(PROP, scale, lines[k]) <= maxW) {
      snprintf(out, outLen, "%s", lines[k]);
      return;
    }
  text::fit(PROP, scale, lines[n - 1], maxW, out, outLen);
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

// A title: at `scale`, a size down if it does not fit, then scrolling
void caption(const char *s, int scale, float cx, int y, int maxW, uint16_t color, int64_t since) {
  int w = text::width(PROP, scale, s);
  if (w > maxW && scale > 3) w = text::width(PROP, --scale, s);
  if (w <= maxW) {
    text::draw(PROP, scale, s, cx - w / 2.f, y, color);
    return;
  }
  // Rest two seconds at the start, then scroll at 36 px/s with a gap between copies
  int loop = w + 8 * scale * 3;
  float t = (nowMs - since) / 1000.f - 2;
  float off = t <= 0 ? 0 : fmodf(t * 36, float(loop));
  int x0 = int(cx) - maxW / 2, x1 = x0 + maxW;
  text::draw(PROP, scale, s, x0 - off, y, color, 1, x0, x1, 16);
  text::draw(PROP, scale, s, x0 - off + loop, y, color, 1, x0, x1, 16);
}

// Remembers when the text in a slot last changed, so scrolling restarts with it
int64_t since(char *slot, const char *key) {
  static int64_t at[4];
  static char *slots[4];
  int k = 0;
  while (k < 4 && slots[k] && slots[k] != slot) k++;
  if (k == 4) k = 0;
  slots[k] = slot;
  if (strcmp(slot, key)) {
    snprintf(slot, 96, "%s", key);
    at[k] = nowMs;
  }
  return at[k];
}

// ---------- Following the summaries ----------

void follow(const model::Summary &next) {
  bool first = !haveSummary;
  for (int i = 0; i < next.count; i++) {
    const Session &s = next.sessions[i];
    const Session *p = first ? nullptr : find(cur, s.key);
    // A new wait, or a new question while still waiting
    if (isWaiting(s) && !(p && isWaiting(*p) && waitSince(*p) == waitSince(s))) {
      Waiting *w = nullptr;
      for (int k = 0; k < waitCount; k++)
        if (!strcmp(waits[k].key, s.key)) w = &waits[k];
      if (!w && waitCount < model::MAX) w = &waits[waitCount++];
      if (w) {
        snprintf(w->key, sizeof w->key, "%s", s.key);
        w->since = waitSince(s);
        w->reminded = 0;
        w->acked = first;  // waits found at start-up don't call out
      }
      if (!first) {
        pushCue(synth::ATTENTION);
        alertUntil = nowMs + TAKEOVER_MS;
        alertAt = nowMs;
        choose(s.key);
        go(ALERT);
      }
    }
    // A turn that ended
    if (p && s.endedAt && s.endedAt != p->endedAt && !isWaiting(s)) {
      bool failed = s.outcome == Outcome::Error || s.outcome == Outcome::Refusal;
      if (s.outcome == Outcome::Answer || failed) pushCue(failed ? synth::ERROR : synth::DONE);
      for (Sat &sat : sats)
        if (sat.used && !strcmp(sat.key, s.key)) {
          sat.flashAt = nowMs;
          sat.flashColor = TONE_COLORS[failed ? CRITICAL : GOOD];
        }
      if (s.outcome == Outcome::Answer || failed) {
        (failed ? winceAt : cheerAt) = nowMs;
        // Show which one finished, unless someone is waiting for you
        bool anyWait = false;
        for (int k = 0; k < next.count; k++) anyWait |= isWaiting(next.sessions[k]);
        if (!anyWait) choose(s.key);
      }
    }
  }
  cur = next;
  haveSummary = true;
  // Waits that ended
  int keep = 0;
  for (int k = 0; k < waitCount; k++) {
    int i = indexOf(waits[k].key);
    if (i >= 0 && isWaiting(cur.sessions[i])) waits[keep++] = waits[k];
  }
  waitCount = keep;
  if (!waitCount && scr == ALERT) go(HOME);
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
    bool chosen = focusKey[0] && !strcmp(sat.key, focusKey);
    sat.focus += ((chosen ? 1 : 0) - sat.focus) * gfx::clamp01(dt * 6);
  }

  // Reminders for waits nobody has answered: at 2 and 5 minutes, then every 10
  for (int k = 0; k < waitCount; k++) {
    Waiting &w = waits[k];
    if (w.acked) continue;
    int64_t due = w.reminded == 0 ? 120000 : 300000 + int64_t(w.reminded - 1) * 600000;
    if (wallMs - w.since >= due) {
      w.reminded++;
      pushCue(synth::REMIND);
      for (Sat &sat : sats)
        if (sat.used && !strcmp(sat.key, w.key)) {
          sat.flashAt = nowMs;
          sat.flashColor = TONE_COLORS[ATTN];
        }
    }
  }

  // Timeouts
  if (scr == ALERT && nowMs >= alertUntil) go(HOME);
  if ((scr == LIST || scr == DETAIL) && nowMs - lastTouch > AWAY_MS) go(HOME);
  if (scr == HOME && focusKey[0] && nowMs - focusAt > FOCUS_MS && nowMs - lastTouch > FOCUS_MS) focusKey[0] = 0;
}

// ---------- Screens ----------

float orbRadius(float base) {
  float p = fmodf(pulsePhase, 1);
  float pulse = p < 0.5f ? 0.95f + 0.08f * (0.5f - 0.5f * cosf(p * gfx::TAU)) : 1.03f - 0.08f * (0.5f - 0.5f * cosf((p - 0.5f) * gfx::TAU));
  float breathe = 0.985f + 0.015f * sinf(nowMs / 1000.f * gfx::TAU / 6);
  return base * (1 + (pulse - 1) * pulseK) * (1 + (breathe - 1) * breatheK);
}

void drawSatellites(float r) {
  float t = nowMs / 1000.f;
  for (Sat &sat : sats) {
    if (!sat.used || sat.tail < 0.01f) continue;
    halo::tail(r, sat.turn, 32 / 360.f, gfx::c565(sat.color), 0.5f * sat.tail);
  }
  const Session *f = focused();
  for (Sat &sat : sats) {
    if (!sat.used) continue;
    int i = indexOf(sat.key);
    if (i < 0) continue;
    float x = gfx::CX + sinf(sat.turn * gfx::TAU) * r, y = gfx::CY - cosf(sat.turn * gfx::TAU) * r;
    uint16_t c = gfx::c565(sat.color);
    if (sat.ping > 0.01f) {
      float e = easeOut(fmodf(t, 1.8f) / 1.8f), k = 1 + 2.4f * e;
      gfx::ringAt(x, y, 8 * k, 2 * k, c, 0.9f * (1 - e) * sat.ping);
    }
    float since = (nowMs - sat.flashAt) / 1000.f;
    if (since < 1.2f) {
      float e = easeOut(since / 1.2f);
      gfx::ringAt(x, y, 10 + 30 * e, 4 - 2 * e, gfx::c565(sat.flashColor), 0.9f * (1 - e));
    }
    halo::satellite(x, y, 9, c, sat.lit, cur.sessions[i].stale);
    // Which satellite the lines under the orb describe
    if (f && !strcmp(f->key, sat.key) && cur.count > 1) gfx::ringAt(x, y, 16, 1.6f, gfx::c565(TEXT), 0.35f + 0.5f * sat.focus);
  }
}

// The orb's celebration of a finished turn: green, a bounce, rings and sparks
void drawCheer(float cx, float cy, float R, float c) {
  uint16_t green = gfx::c565(DONE_PALETTE.m1), light = gfx::c565(DONE_PALETTE.m2);
  for (int k = 0; k < 3; k++) {
    float tk = c - k * 0.18f;
    if (tk <= 0 || tk >= 1.2f) continue;
    float e = easeOut(tk / 1.2f);
    gfx::ringAt(cx, cy, R * (1.04f + 0.9f * e), 1.5f + 5 * (1 - e), green, 0.75f * (1 - e));
  }
  if (c < 1.5f) {
    float e = easeOut(c / 1.5f);
    for (int k = 0; k < 12; k++) {
      float a = k / 12.f + c * 0.05f + (k % 2) * 0.02f;
      float d = R + 8 + (k % 2 ? 56 : 78) * e;
      gfx::discAt(cx + sinf(a * gfx::TAU) * d, cy - cosf(a * gfx::TAU) * d, 3.5f * (1 - e * 0.7f), light, 1 - e);
    }
  }
}

// The orb's wince at a failed turn: two red shock rings, close and quick
void drawWince(float cx, float cy, float R, float c) {
  uint16_t red = gfx::c565(FAIL_PALETTE.m1);
  for (int k = 0; k < 2; k++) {
    float tk = c - k * 0.12f;
    if (tk <= 0 || tk >= 0.8f) continue;
    float e = easeOut(tk / 0.8f);
    gfx::ringAt(cx, cy, R * (1.04f + 0.55f * e), 1.5f + 6 * (1 - e), red, 0.8f * (1 - e));
  }
}

void drawHome() {
  float sx, sy;
  shift(sx, sy);
  float ox = halo::OX + sx, oy = halo::OY + sy;
  float R = orbRadius(halo::ORB_R);

  // A finished turn: green that comes on fast, holds, then glides back, with
  // a bounce. A failed one: red, a shake of the head and a flinch. The later wins.
  halo::Palette p = pal;
  float c = (nowMs - cheerAt) / 1000.f, f = (nowMs - winceAt) / 1000.f;
  bool cheering = linkState == ONLINE && c >= 0 && c < CHEER_S && cheerAt >= winceAt;
  bool wincing = linkState == ONLINE && f >= 0 && f < WINCE_S && winceAt > cheerAt;
  if (cheering) {
    float w = c < 0.25f ? smooth(c / 0.25f) : c < 1.3f ? 1 : 1 - smooth((c - 1.3f) / (CHEER_S - 1.3f));
    p = halo::mix(pal, DONE_PALETTE, w);
    R *= 1 + 0.09f * sinf(c * gfx::TAU * 1.6f) * expf(-c * 2.8f);
  }
  if (wincing) {
    float w = f < 0.15f ? smooth(f / 0.15f) : f < 1.0f ? 1 : 1 - smooth((f - 1.0f) / (WINCE_S - 1.0f));
    p = halo::mix(pal, FAIL_PALETTE, w);
    ox += 7 * sinf(f * gfx::TAU * 8) * expf(-f * 4.5f);
    R *= 1 - 0.05f * expf(-f * 5);
  }

  halo::background(p.m1, glowK, halo::ORB_R * 0.94f - 1);
  halo::scale(nowMs / 1000.f / 140, gfx::mix(TEXT, p.m1, 0.8f), 1);
  gfx::ring(halo::R_TRACK, 1.5f, gfx::c565(gfx::mix(BORDER_2, p.m1, 0.45f)), 1);
  if (linkState == ONLINE) drawSatellites(halo::R_TRACK);
  if (rippleK > 0.01f) {
    float e = easeOut(fmodf(nowMs / 1000.f, 2.2f) / 2.2f), k = 1 + 0.5f * e;
    gfx::ringAt(ox, oy, (R + 1.6f) * k, 3 * k, gfx::c565(p.m1), 0.6f * (1 - e) * rippleK);
  }
  if (cheering) drawCheer(ox, oy, R, c);
  if (wincing) drawWince(ox, oy, R, f);
  halo::orb(ox, oy, R, p, motion);

  // The two lines under the orb
  int y1 = 322 + int(sy), y2 = 362 + int(sy);
  if (linkState != ONLINE) {
    text::centre(PROP, 4, linkState == CONNECTING ? "CONNECTING…" : "LINK LOST", ox, y1, gfx::c565(TEXT_2));
    text::centre(PROP, 3, linkState == CONNECTING ? "AGENTSMONITOR" : "RECONNECTING…", ox, y2, gfx::c565(TEXT_3));
    return;
  }
  const Session *s = focused();
  if (!s) {
    text::centre(PROP, 4, "NO SESSIONS", ox, y1 + 16, gfx::c565(TEXT_3));
    return;
  }
  static char shown[96];
  caption(titleOf(*s), 4, ox, y1, 296, gfx::c565(TEXT), since(shown, s->key));
  char lines[3][96], line[96];
  int n = stateLines(*s, lines);
  pickLine(lines, n, 3, 284, line, sizeof line);
  text::centre(PROP, 3, line, ox, y2, gfx::c565(toneText(tone(*s))));
}

void drawAlert() {
  gfx::clear();
  float t = nowMs / 1000.f, sx, sy;
  shift(sx, sy);
  if (!waitCount) return;
  // Which waiting session the card shows: the newest first, then each in turn
  int pick = 0;
  for (int k = 0; k < waitCount; k++)
    if (!strcmp(waits[k].key, focusKey)) pick = k;
  if (waitCount > 1) pick = (pick + int((nowMs - alertAt) / ROTATE_MS)) % waitCount;
  int i = indexOf(waits[pick].key);
  if (i < 0) return;
  const Session &s = cur.sessions[i];
  uint16_t amber = gfx::c565(TONE_COLORS[ATTN]);
  float cx = 233 + sx;

  float pulse = 0.5f + 0.5f * sinf(t * gfx::TAU / 1.8f);
  gfx::ring(222, 10, amber, 0.55f + 0.35f * pulse);
  gfx::ring(205, 2.5f, amber, 0.3f);
  gfx::glowAt(cx, 86 + sy, 64, gfx::c565(pal.m1), 0.5f);
  halo::orb(cx, 86 + sy, 30 * (0.97f + 0.05f * pulse), pal, motion);

  const char *heading = s.wait == Wait::Input ? "NEEDS AN ANSWER" : s.wait == Wait::Idle ? "WAITING FOR YOU" : "NEEDS APPROVAL";
  text::centre(PROP, 4, heading, cx, 160 + int(sy), amber);
  static char shown[96];
  caption(titleOf(s), 4, cx, 214 + int(sy), 340, gfx::c565(TEXT), since(shown, s.key));

  // What it waits on: the tool and its subject, or the question
  char what[480], tool[64], lines[2][160];
  toolName(s.waitTool[0] ? s.waitTool : s.tool, tool, sizeof tool);
  bool command = s.wait == Wait::Permission;
  if (command)
    snprintf(what, sizeof what, "%s%s%s", tool, tool[0] && s.waitText[0] ? " " : "", s.waitText);
  else
    snprintf(what, sizeof what, "%s", s.waitText);
  int bottom = 236 + int(sy);
  if (what[0]) {
    text::Face face = command ? MONO : PROP;
    int n = text::wrap(face, 3, what, 300, lines, 2);
    float top = 240 + sy, h = 22 + n * 32.f;
    gfx::roundRect(70 + sx, top, 396 + sx, top + h, 14, gfx::c565(CARD));
    for (int k = 0; k < n; k++) text::draw(face, 3, lines[k], 84 + sx, int(top + 11 + 21 + k * 32), gfx::c565(TEXT_2));
    bottom = int(top + h);
  }
  char wait[24], foot[140], project[64];
  duration(wallMs - waitSince(s), wait, sizeof wait);
  text::fit(PROP, 3, s.project, 250, project, sizeof project);
  snprintf(foot, sizeof foot, "%s · %s", project, wait);
  text::centre(PROP, 3, foot, cx, bottom + 42, gfx::c565(TEXT_3));

  if (waitCount > 1) {
    float x0 = cx - (waitCount - 1) * 12;
    for (int k = 0; k < waitCount; k++) gfx::discAt(x0 + k * 24, 414 + sy, 5.5f, k == pick ? amber : gfx::c565(TEXT_4));
  }
}

const char *moodWord(Mood m) {
  switch (m) {
    case Mood::Attention: return "NEEDS YOU";
    case Mood::Working: return "WORKING";
    case Mood::Error: return "FAILED";
    case Mood::Idle: return "IDLE";
    default: return "NO SESSIONS";
  }
}

constexpr int ROW_H = 58, LIST_TOP = 136, LIST_CLIP0 = 124, LIST_CLIP1 = 452;

float listMax() { return cur.count * ROW_H > LIST_CLIP1 - LIST_TOP - 20 ? float(cur.count * ROW_H - (LIST_CLIP1 - LIST_TOP - 20)) : 0; }

int rowAt(int y) {
  int i = int(floorf((y + listScroll - LIST_TOP) / ROW_H));
  return i >= 0 && i < cur.count && y >= LIST_CLIP0 && y < LIST_CLIP1 ? i : -1;
}

void drawList() {
  gfx::clear();
  float sx, sy;
  shift(sx, sy);
  halo::orb(233 + sx, 56 + sy, 18, pal, motion);
  char head[64];
  if (cur.count)
    snprintf(head, sizeof head, "%s · %d SESSION%s", moodWord(mood()), cur.count, cur.count == 1 ? "" : "S");
  else
    snprintf(head, sizeof head, "NO SESSIONS");
  text::centre(PROP, 3, head, 233 + sx, 108 + int(sy), gfx::c565(TEXT_3));

  for (int i = 0; i < cur.count; i++) {
    const Session &s = cur.sessions[i];
    float cy = LIST_TOP + i * ROW_H + ROW_H / 2.f - listScroll + sy;
    if (cy < LIST_CLIP0 - ROW_H || cy > LIST_CLIP1 + ROW_H) continue;
    // Fade rows out at the top and bottom of the list
    float alpha = gfx::clamp01(fminf(cy - LIST_CLIP0, LIST_CLIP1 - cy) / 30);
    if (alpha <= 0) continue;
    float dy = cy - gfx::CY, half = sqrtf(fmaxf(0, 233 * 233 - dy * dy)) - 40;
    float x0 = gfx::CX - half + sx, x1 = gfx::CX + half + sx;
    Tone t = tone(s);
    if (t == ATTN) gfx::roundRect(x0 - 12, cy - 25, x1 + 12, cy + 25, 12, gfx::c565(WAIT_ROW), alpha);
    if (s.stale)
      gfx::ringAt(x0 + 8, cy, 7, 2.2f, gfx::c565(TONE_COLORS[t]), alpha);
    else
      gfx::discAt(x0 + 8, cy, 8, gfx::c565(TONE_COLORS[t]), alpha);

    // On the right: how long it waits, what it runs, or how long since it finished
    char side[64], word[64];
    if (isWaiting(s)) {
      duration(wallMs - waitSince(s), side, sizeof side);
    } else if (s.stale) {
      snprintf(side, sizeof side, "—");
    } else if (s.status == Status::Tool) {
      toolName(s.tool, word, sizeof word);
      text::fit(PROP, 3, word, 84, side, sizeof side);
    } else if (model::working(s.status)) {
      snprintf(side, sizeof side, "%s", s.status == Status::Compacting ? "PACK" : "THINK");
    } else {
      age(wallMs - (s.endedAt ? s.endedAt : s.since), side, sizeof side, false);
    }
    int sideW = text::width(PROP, 3, side);
    text::draw(PROP, 3, side, x1 - sideW, int(cy + 10), gfx::c565(toneText(t)), alpha);
    char title[160];
    text::fit(PROP, 3, titleOf(s), int(x1 - sideW - 16 - (x0 + 26)), title, sizeof title);
    text::draw(PROP, 3, title, x0 + 26, int(cy + 10), gfx::c565(t == REST || t == MUTED ? TEXT_2 : TEXT), alpha);
  }
}

void drawDetail() {
  gfx::clear();
  float sx, sy;
  shift(sx, sy);
  float cx = 233 + sx;
  int i = indexOf(detailKey);
  if (i < 0) {
    text::centre(PROP, 4, "SESSION ENDED", cx, 240 + int(sy), gfx::c565(TEXT_3));
    return;
  }
  const Session &s = cur.sessions[i];
  Tone t = tone(s);
  const char *agent = !strcmp(s.agent, "claude-code") ? "CLAUDE CODE" : !strcmp(s.agent, "codex") ? "CODEX" : s.agent;
  char project[64];
  text::fit(PROP, 3, s.project, 280, project, sizeof project);
  gfx::discAt(cx, 50 + sy, 7, gfx::c565(TONE_COLORS[t]));
  text::centre(PROP, 3, agent, cx, 94 + int(sy), gfx::c565(TEXT_3));
  text::centre(PROP, 3, project, cx, 126 + int(sy), gfx::c565(TEXT_4));

  char lines[3][160];
  int n = text::wrap(PROP, 4, titleOf(s), 340, lines, 3);
  int y = 186 + int(sy);
  for (int k = 0; k < n; k++, y += 46) text::centre(PROP, 4, lines[k], cx, y, gfx::c565(TEXT));

  char states[3][96], state[96];
  int m = stateLines(s, states);
  pickLine(states, m, 3, 360, state, sizeof state);
  text::centre(PROP, 3, state, cx, y + 4, gfx::c565(toneText(t)));

  // What it waits on
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
  float boxTop = y + 28.f;
  int room = int((424 - boxTop - 22) / 32);  // lines that fit above the bottom of the round
  if (room < 1) return;
  int k = text::wrap(face, 3, what, 300, box, room < 3 ? room : 3);
  float h = 22 + k * 32.f;
  gfx::roundRect(70 + sx, boxTop, 396 + sx, boxTop + h, 14, gfx::c565(CARD));
  for (int r = 0; r < k; r++) text::draw(face, 3, box[r], 84 + sx, int(boxTop + 11 + 21 + r * 32), gfx::c565(TEXT_2));
}

// ---------- Touch ----------

void tap(int y) {
  // Any tap answers the reminders of the waits on screen
  for (int k = 0; k < waitCount; k++) waits[k].acked = true;
  switch (scr) {
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
    case DETAIL: go(back == LIST ? LIST : HOME); break;
  }
}

void swipe(int dx, int dy) {
  bool across = abs(dx) > abs(dy);
  switch (scr) {
    case ALERT:
      for (int k = 0; k < waitCount; k++) waits[k].acked = true;
      alertUntil = nowMs;
      go(HOME);
      break;
    case HOME:
      if (across && cur.count > 1) {
        const Session *f = focused();
        int i = f ? indexOf(f->key) : 0;
        i = (i + (dx < 0 ? 1 : cur.count - 1)) % cur.count;
        choose(cur.sessions[i].key);
      } else if (!across && dy < 0) {
        listScroll = 0;
        go(LIST);
      }
      break;
    case LIST:
      if (!across && dy > 0 && scrollAtDown <= 0) go(HOME);
      break;
    case DETAIL:
      if (dx > 0 || dy > 0) go(back == LIST ? LIST : HOME);
      break;
  }
}

}  // namespace

bool init(const uint8_t *cjk, size_t cjkLen) { return gfx::init() && halo::init() && text::init(cjk, cjkLen); }

void link(Link state) {
  if (state != ONLINE) {
    // Nothing on screen is current any more
    haveSummary = false;
    waitCount = 0;
    focusKey[0] = 0;
    go(HOME);
  }
  linkState = state;
}

bool summary(const char *json) {
  static model::Summary next;
  if (!model::parse(json, next)) return false;
  summaryAt = nowMs;
  summaryTs = next.ts;
  if (!wallMs) wallMs = next.ts;
  follow(next);
  syncSats();
  return true;
}

void touch(Touch kind, int x, int y, int64_t ms) {
  lastTouch = ms;
  switch (kind) {
    case DOWN:
      pressed = true, dragged = false;
      downX = x, downY = y, lastY = y, downAt = ms;
      scrollAtDown = listScroll;
      break;
    case MOVE:
      if (!pressed) break;
      if (scr == LIST && (dragged || abs(y - downY) > 10)) {
        dragged = true;
        listScroll += lastY - y;
        float top = -80, bottom = listMax() + 40;  // a little give past the ends
        listScroll = listScroll < top ? top : listScroll > bottom ? bottom : listScroll;
      }
      lastY = y;
      break;
    case UP: {
      if (!pressed) break;
      pressed = false;
      int dx = x - downX, dy = y - downY;
      if (scr == LIST && dragged) {
        // Pulled down past the top: back home; otherwise settle within the list
        if (dy > 70 && scrollAtDown <= 0) go(HOME);
        listScroll = listScroll < 0 ? 0 : listScroll > listMax() ? listMax() : listScroll;
      } else if (dx * dx + dy * dy < 14 * 14 && ms - downAt < 600) {
        tap(y);
      } else if (dx * dx + dy * dy > 50 * 50) {
        swipe(dx, dy);
      }
      break;
    }
  }
}

void frame(uint16_t *fb, int64_t ms, int64_t wall) {
  nowMs = ms;
  float dt = lastMs ? (ms - lastMs) / 1000.f : 0;
  if (dt > 0.1f) dt = 0.1f;
  lastMs = ms;
  wallMs = wall ? wall : summaryTs + (ms - summaryAt);
  step(dt);

  gfx::target(fb);
  switch (scr) {
    case HOME: drawHome(); break;
    case ALERT: drawAlert(); break;
    case LIST: drawList(); break;
    case DETAIL: drawDetail(); break;
  }
  int64_t since = ms - screenAt;
  if (since < FADE_MS) gfx::dim(1 - powf(1 - since / float(FADE_MS), 2));
}

int cue() {
  if (cueHead == cueTail) return -1;
  int c = cues[cueHead];
  cueHead = (cueHead + 1) % 8;
  return c;
}

const char *screen() {
  static const char *const NAMES[] = {"home", "alert", "list", "detail"};
  return NAMES[scr];
}

}  // namespace ui
