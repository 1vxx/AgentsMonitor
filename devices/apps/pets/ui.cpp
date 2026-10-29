#ifndef __clang__
#pragma GCC optimize("O2")
#endif

// Screens: ONE (the most pressing session's pet, large, with what it is
// doing; the default), ALL (every pet on the board at once, swiped up from
// ONE) and ALERT (the pet that waits for you, with what it waits on).
//
// ONE follows the summary's order, which puts first whoever waits for you,
// then whoever works, newest turn first, then the idle: a pet that just
// finished a turn leads for a while, and one a finger picks holds until the
// finger has been away ten seconds. Swiping sideways steps through them in
// that order; dots along the bottom show where it is.
//
// A pet keeps its animal and its place for as long as its session lives.
// Thinking it nods, running a tool it jitters and throws sparks, compacting
// it squashes, waiting for you it hops under a "!", a finished turn makes it
// smile and hop beside a heart, a failed one makes it shake under a puff of
// smoke, and idle it naps. Moving between screens is one motion on springs
// that a finger can carry and take back halfway; everything lands on the
// board's grid, so it moves a dot at a time.

#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "board.h"
#include "events.h"
#include "gfx.h"
#include "model.h"
#include "spring.h"
#include "sprites.h"
#include "synth.h"
#include "text.h"

namespace ui {
namespace {

using board::MID;
using board::N;
using gfx::hex;
using gfx::RGB;
using model::Outcome;
using model::Session;
using model::Status;

// ---------- Look ----------

const RGB WHITE = hex(0xeceaf6), DIM = hex(0x8a8ca8), AMBER = hex(0xffba46), GREEN = hex(0x8ce1a0), RED = hex(0xff6a5e),
          PINK = hex(0xff7ea0), TEAL = hex(0x6ee1d7), SMOKE = hex(0x9a9aa8);
uint16_t white, dim, amber, green, red, pink, teal, smoke;

constexpr int64_t TAKEOVER_MS = 10000;  // the alert's stay
constexpr int64_t ROTATE_MS = 4000;     // between waiting pets on the alert
constexpr int64_t AWAY_MS = 30000;      // ALL falls back to ONE
constexpr int64_t FOCUS_MS = 10000;     // a pet a finger picks stays on ONE
constexpr int64_t STAR_MS = 10000;      // a pet that just finished leads ONE
constexpr int64_t SHIFT_MS = 3 * 60000; // anti burn-in nudge
constexpr int64_t NAP_MS = 10 * 60000;  // all quiet this long, the board dims
constexpr int64_t CHEER_MS = 4000;      // a finished turn's smile and heart
constexpr int64_t NEWS_MS = 4000;       // and the line about it at the top
constexpr float MOVE_PX = 220;          // a finger this far carries a move all the way

// ---------- State ----------

enum Screen { ONE, ALL, ALERT };
Screen from = ONE, to = ONE;
Spring trans = {1, 0};  // 0: all `from`, 1: all `to`; overshoots a little
bool driving;           // a finger is carrying the move
Spring slide;           // ONE or the alert carried sideways, in cells

Link linkState = CONNECTING;
model::Summary cur;
bool haveSummary;
events::Tracker tracker;
int64_t nowMs, lastMs, wallMs, summaryAt, summaryTs, lastTouch, alertUntil, alertAt, busyAt;
char alertKey[96], oneKey[96], chosenKey[96];  // on the alert; on ONE; picked by a finger
int64_t chosenAt;
float dimK = 1;

// The latest finished turn, for the line at the top
struct {
  char key[96];
  Outcome outcome;
  int64_t at = -100000;
} news;

int cues[8], cueHead, cueTail;

struct Pet {
  bool used, leaving, placed;
  char key[96];
  int animal, order;
  Spring x, y, k;  // its place at home, in cells: centre column, top row, size
  float shown;     // comes up as it arrives, goes as it leaves
  float phase;     // its own beat, so no two bob together
  int64_t bornAt, goneAt, endAt, pokeAt;
  Outcome endHow;
  char label[24];  // its project, for when its session has gone
};
constexpr int PETS = model::MAX + 4;
Pet pets[PETS];
int arrivals;

uint16_t inks[sprites::COUNT][128];

// Touch
enum class Drag { None, Undecided, Slide, Move };
Drag drag = Drag::None;
int downX, downY;
int64_t downAt;
Screen moveFrom;
struct Sample {
  int x, y;
  int64_t ms;
} samples[6];
int sampleCount;
int pressed = -1;  // the pet a finger rests on

// Timings, for the logs
uint32_t composeTicks, frames;
std::atomic<uint32_t> renderTicks;

// ---------- Helpers ----------

uint32_t hash(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ uint8_t(*s++)) * 16777619u;
  return h;
}

inline int iround(float v) { return gfx::ifloor(v + 0.5f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

void pushCue(synth::Cue c) {
  int next = (cueTail + 1) % 8;
  if (next == cueHead) return;
  cues[cueTail] = c;
  cueTail = next;
}

const Session *find(const char *key) { return key[0] ? events::find(cur, key) : nullptr; }

Pet *petOf(const char *key) {
  for (Pet &p : pets)
    if (p.used && !p.leaving && !strcmp(p.key, key)) return &p;
  return nullptr;
}

bool online() { return linkState == ONLINE && haveSummary; }

// "mcp__server__tool" reads "server · tool"; in capitals
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

// "2M AGO", "3H AGO", "JUST NOW"
void age(int64_t ms, char *out, size_t n) {
  int64_t m = ms / 60000;
  if (m < 1)
    snprintf(out, n, "JUST NOW");
  else if (m < 60)
    snprintf(out, n, "%dM AGO", int(m));
  else if (m < 24 * 60)
    snprintf(out, n, "%dH AGO", int(m / 60));
  else
    snprintf(out, n, "%dD AGO", int(m / 1440));
}

// "950", "12.3K", "845K", "1.24M": the dashboard's compact counts
void count(uint64_t v, char *out, size_t len) {
  double d = double(v);
  if (v < 1000)
    snprintf(out, len, "%u", unsigned(v));
  else if (v < 100000)
    snprintf(out, len, "%.1fK", d / 1e3);
  else if (v < 1000000)
    snprintf(out, len, "%.0fK", d / 1e3);
  else if (v < 100000000)
    snprintf(out, len, "%.2fM", d / 1e6);
  else
    snprintf(out, len, "%.0fM", d / 1e6);
}

// Claude Code is just CLAUDE on the display
void agentName(const char *agent, char *out, size_t n) {
  if (!strncmp(agent, "claude", 6))
    snprintf(out, n, "CLAUDE");
  else
    text::upper(agent, out, n);
}

// How wide a line of the board can be at row j, a little in from the round
int roomAt(float j) {
  float dy = j - MID, o = 56.f * 56.f - dy * dy;
  return o <= 0 ? 0 : int(2 * sqrtf(o)) - 4;
}

// The first line whose tiny width fits, else the last cut to fit
void pickTiny(char (*lines)[64], int n, int maxW, char *out, size_t len) {
  for (int k = 0; k < n; k++)
    if (board::tinyWidth(lines[k]) <= maxW) {
      snprintf(out, len, "%s", lines[k]);
      return;
    }
  board::tinyFit(lines[n - 1], (maxW + 1) / 4, out, int(len));
}

bool ascii(const char *s) {
  for (; *s; s++)
    if (uint8_t(*s) >= 0x80) return false;
  return true;
}

// Breaks s into lines of at most `chars` tiny characters, at spaces where it
// can; the last line is cut. Returns the count.
int wrapTiny(const char *s, int chars, char (*out)[64], int maxLines) {
  int n = 0;
  while (*s && n < maxLines) {
    while (*s == ' ') s++;
    int len = int(strlen(s));
    if (len <= chars) {
      snprintf(out[n++], 64, "%s", s);
      break;
    }
    if (n == maxLines - 1) {
      board::tinyFit(s, chars, out[n++], 64);
      break;
    }
    // At a space, or after a slash, dash or dot, else wherever it must
    int cut = chars;
    while (cut > chars / 2 && s[cut] != ' ' && !strchr("/-.", s[cut - 1])) cut--;
    if (cut <= chars / 2) cut = chars;
    snprintf(out[n++], 64, "%.*s", cut, s);
    s += cut;
  }
  return n;
}

// ---------- What a pet does ----------

enum Act { THINK, TOOL, PACK, WAIT, NAP, DONE, FAIL, DARK, GONE };

Act actOf(const Pet &p, const Session *s) {
  if (p.leaving || (!s && online())) return GONE;
  if (!online() || !s || s->stale) return DARK;
  if (events::waiting(*s)) return WAIT;
  switch (s->status) {
    case Status::Thinking: return THINK;
    case Status::Tool: return TOOL;
    case Status::Compacting: return PACK;
    default: break;
  }
  if (nowMs - p.endAt < CHEER_MS) {
    if (p.endHow == Outcome::Answer) return DONE;
    if (p.endHow == Outcome::Error || p.endHow == Outcome::Refusal) return FAIL;
  }
  return NAP;
}

uint16_t actColor(Act a) {
  switch (a) {
    case THINK:
    case TOOL:
    case PACK: return teal;
    case WAIT: return amber;
    case DONE: return green;
    case FAIL: return red;
    default: return dim;
  }
}

// What a session is doing, longest first: callers take the first that fits
int stateLines(const Session &s, char (*out)[64]) {
  char t[32], tool[48];
  if (s.stale) {
    snprintf(out[0], 64, "NO SIGNAL");
    return 1;
  }
  switch (s.status) {
    case Status::WaitingPermission:
      duration(wallMs - events::waitSince(s), t, sizeof t);
      snprintf(out[0], 64, "NEEDS APPROVAL %s", t);
      snprintf(out[1], 64, "NEEDS APPROVAL");
      return 2;
    case Status::WaitingInput:
      duration(wallMs - events::waitSince(s), t, sizeof t);
      snprintf(out[0], 64, "NEEDS AN ANSWER %s", t);
      snprintf(out[1], 64, "NEEDS AN ANSWER");
      return 2;
    case Status::Tool:
      toolName(s.tool, tool, sizeof tool);
      duration(wallMs - s.since, t, sizeof t);
      snprintf(out[0], 64, "RUNNING %s %s", tool, t);
      snprintf(out[1], 64, "RUNNING %s", tool);
      snprintf(out[2], 64, "%s", tool);
      return 3;
    case Status::Thinking:
      duration(wallMs - s.since, t, sizeof t);
      snprintf(out[0], 64, "THINKING %s", t);
      return 1;
    case Status::Compacting: snprintf(out[0], 64, "COMPACTING"); return 1;
    default: break;
  }
  age(wallMs - (s.endedAt ? s.endedAt : s.since), t, sizeof t);
  switch (s.outcome) {
    case Outcome::Error: snprintf(out[0], 64, "FAILED %s", t); return 1;
    case Outcome::Refusal: snprintf(out[0], 64, "REFUSED %s", t); return 1;
    case Outcome::Aborted: snprintf(out[0], 64, "INTERRUPTED %s", t); return 1;
    case Outcome::Answer: snprintf(out[0], 64, "DONE %s", t); return 1;
    default: snprintf(out[0], 64, "NAPPING"); return 1;
  }
}

// ---------- Places ----------

struct Pose {
  float cx, top, k;  // centre column, top row, cells per sprite pixel
};

struct Slot {
  Pose pose;
  int chars;  // how long its label may be
};

// Where the r-th of n pets sits at home: one large, two side by side, three
// in a row, then rows of two and three
Slot slot(int n, int r) {
  struct Layout {
    int8_t rows[3];
    uint8_t top[3];
    uint8_t k;
  };
  static const Layout L[9] = {
      {{0}, {0}, 2},           {{1}, {30}, 4},          {{2}, {38}, 3},
      {{3}, {44}, 2},          {{2, 2}, {28, 62}, 2},   {{3, 2}, {28, 62}, 2},
      {{3, 3}, {28, 62}, 2},   {{2, 3, 2}, {17, 47, 77}, 2}, {{2, 3, 3}, {17, 47, 77}, 2},
  };
  const Layout &l = L[n < 1 ? 1 : n > 8 ? 8 : n];
  int row = 0, start = 0;
  while (row < 2 && r >= start + l.rows[row] && l.rows[row + 1]) start += l.rows[row++];
  int m = l.rows[row];
  float gap = l.k == 3 ? 44 : (n == 8 && row == 2) ? 29 : m == 2 ? 36 : 34;
  Slot s;
  s.pose = {MID + (r - start - (m - 1) / 2.f) * gap, float(l.top[row]), float(l.k)};
  s.chars = n == 1 ? 20 : (n == 8 && row == 2) ? 4 : int((gap - 3) / 4);
  return s;
}

// The pets at home in their order, leaving ones not counted
int ranked(Pet **out) {
  int n = 0;
  for (Pet &p : pets)
    if (p.used && !p.leaving) out[n++] = &p;
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && out[j - 1]->order > out[j]->order; j--) {
      Pet *t = out[j];
      out[j] = out[j - 1], out[j - 1] = t;
    }
  return n;
}

// ---------- Moving between screens ----------

// The leaving screen's words are gone by 60% of the way
float shown(Screen s) {
  if (from == to) return s == to ? 1.f : 0.f;
  float t = gfx::clamp01(trans.x);
  return s == to ? t : s == from ? gfx::clamp01(1 - t * 1.7f) : 0;
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
  if (from != to && trans.x < 0.5f) to = from;  // start from whichever is mostly up
  from = to, to = s;
  trans.x = 0, trans.v = 0;
  driving = byFinger;
  slide.x = slide.v = 0;
}

void callOut(const char *key) {
  snprintf(alertKey, sizeof alertKey, "%s", key);
  alertAt = nowMs;
  alertUntil = nowMs + TAKEOVER_MS;
  drag = Drag::None;
  pressed = -1;
  go(ALERT);
}

// A pet's place and how much of it shows on a screen
Pose poseOn(Screen sc, const Pet &p) {
  if (sc == ALERT && !strcmp(p.key, alertKey)) return {MID + slide.x, 14, 4};
  if (sc == ONE && !strcmp(p.key, oneKey)) return {MID + slide.x, 17, 4};
  return {p.x.x, p.y.x, p.k.x};
}

float alphaOn(Screen sc, const Pet &p) {
  if (sc == ALERT) return !strcmp(p.key, alertKey) ? 1 : 0;
  if (sc == ONE) return !strcmp(p.key, oneKey) ? 1 : 0;
  return 1;
}

// Who ONE shows: the pet a finger picked, while it holds; the one that just
// finished a turn, unless someone waits; else the first in the summary
const Session *focus() {
  if (chosenKey[0]) {
    const Session *s = find(chosenKey);
    int64_t last = chosenAt > lastTouch ? chosenAt : lastTouch;
    if (s && (drag != Drag::None || nowMs - last < FOCUS_MS)) return s;
    chosenKey[0] = 0;
  }
  if (!tracker.count && nowMs - news.at < STAR_MS)
    if (const Session *s = find(news.key)) return s;
  return cur.count ? &cur.sessions[0] : nullptr;
}

void choose(const char *key) {
  snprintf(chosenKey, sizeof chosenKey, "%s", key);
  snprintf(oneKey, sizeof oneKey, "%s", key);
  chosenAt = nowMs;
}

// ---------- Following the summaries ----------

void follow(const model::Summary &next) {
  events::Event evs[2 * model::MAX];
  int n = tracker.follow(haveSummary ? &cur : nullptr, next, evs, 2 * model::MAX);
  const char *call = nullptr;
  for (int k = 0; k < n; k++) {
    const events::Event &e = evs[k];
    if (e.kind == events::WAIT) {
      pushCue(synth::ATTENTION);
      call = e.key;
    } else if (e.kind == events::END) {
      bool failed = e.outcome == Outcome::Error || e.outcome == Outcome::Refusal;
      if (e.outcome == Outcome::Answer || failed) pushCue(failed ? synth::ERROR : synth::DONE);
      if (Pet *p = petOf(e.key)) p->endAt = nowMs, p->endHow = e.outcome;
      snprintf(news.key, sizeof news.key, "%s", e.key);
      news.outcome = e.outcome;
      news.at = nowMs;
    }
  }
  cur = next;
  haveSummary = true;
  tracker.prune(cur);
  if (call) callOut(call);
  if (!tracker.count && to == ALERT) go(ONE);
}

// Pets keep their animal and place across summaries, matched by key
void syncPets() {
  for (Pet &p : pets)
    if (p.used && !p.leaving && !find(p.key)) p.leaving = true, p.goneAt = nowMs;
  for (int i = 0; i < cur.count; i++) {
    const Session &s = cur.sessions[i];
    if (Pet *p = petOf(s.key)) {
      text::upper(s.project, p->label, sizeof p->label);
      continue;
    }
    Pet *slotFor = nullptr;
    for (Pet &p : pets)
      if (!p.used) {
        slotFor = &p;
        break;
      }
    if (!slotFor) break;
    // Its own animal, or the next one no pet here has
    bool taken[sprites::COUNT] = {};
    for (const Pet &p : pets)
      if (p.used && !p.leaving) taken[p.animal] = true;
    int a = int(hash(s.key) % sprites::COUNT);
    for (int k = 0; k < sprites::COUNT && taken[a]; k++) a = (a + 1) % sprites::COUNT;
    Pet &p = *slotFor;
    p = Pet{};
    p.used = true;
    snprintf(p.key, sizeof p.key, "%s", s.key);
    p.animal = a;
    p.order = ++arrivals;
    p.phase = (hash(s.key) >> 8 & 1023) / 97.f;
    p.bornAt = nowMs;
    p.endAt = p.pokeAt = -100000;
    text::upper(s.project, p.label, sizeof p.label);
  }
}

// ---------- Time ----------

void step(float dt) {
  if (!driving) {
    trans.step(1, dt, 190, 23);
    if (from != to && trans.settled(1)) from = to, trans.x = 1, trans.v = 0;
  }
  if (drag != Drag::Slide) slide.step(0, dt, 210, 27);

  // Pets glide to their places; a new one hops in from the nearer edge
  Pet *order[PETS];
  int n = ranked(order);
  for (int r = 0; r < n; r++) {
    Pet &p = *order[r];
    Pose t = slot(n, r).pose;
    if (!p.placed) {
      p.x.x = t.cx < MID ? -14.f : N + 14.f;
      p.y.x = t.top, p.k.x = t.k;
      p.placed = true;
    }
    p.x.step(t.cx, dt, 150, 22);
    p.y.step(t.top, dt, 150, 22);
    p.k.step(t.k, dt, 150, 22);
    p.shown += (1 - p.shown) * gfx::clamp01(dt * 6);
  }
  for (Pet &p : pets)
    if (p.used && p.leaving) {
      p.shown -= dt * 1.4f;
      if (p.shown <= 0) p.used = false;
    }

  // Reminders for waits nobody has answered call the pet back out
  events::Event due[model::MAX];
  for (int k = 0, m = tracker.remind(wallMs, due, model::MAX); k < m; k++) {
    pushCue(synth::REMIND);
    if (drag == Drag::None) callOut(due[k].key);
  }

  // The alert shows each waiting pet in turn, while it lasts
  if (to == ALERT) {
    int at = -1;
    for (int k = 0; k < tracker.count; k++)
      if (!strcmp(tracker.waits[k].key, alertKey)) at = k;
    if (!tracker.count || nowMs >= alertUntil) {
      go(ONE);
    } else if (at < 0 || (tracker.count > 1 && drag == Drag::None && nowMs - alertAt >= ROTATE_MS)) {
      snprintf(alertKey, sizeof alertKey, "%s", tracker.waits[(at + 1) % tracker.count].key);
      alertAt = nowMs;
      if (at >= 0) slide.x = 40, slide.v = 0;  // the next one comes in from the right
    }
  }
  if (to == ALL && drag == Drag::None && nowMs - lastTouch > AWAY_MS) go(ONE);

  // ONE follows whoever leads; a new one comes in from the right
  const Session *f = focus();
  if (!f) {
    oneKey[0] = 0;
  } else if (strcmp(f->key, oneKey)) {
    bool had = oneKey[0] && find(oneKey);
    snprintf(oneKey, sizeof oneKey, "%s", f->key);
    if (had && to == ONE && drag != Drag::Slide) slide.x = 48, slide.v = 0;
  }

  // All quiet for a while, the board dims; lost, it dims a little
  for (int i = 0; i < cur.count; i++)
    if (events::waiting(cur.sessions[i]) || model::working(cur.sessions[i].status)) busyAt = nowMs;
  float want = !online() ? 0.75f : nowMs - busyAt > NAP_MS ? 0.55f : 1;
  dimK += (want - dimK) * gfx::clamp01(dt * 1.5f);
}

// ---------- Drawing a pet ----------

enum Eyes { OPEN, SHUT, SMILE };

struct Box {
  int x0, y0, x1, y1;  // the cells it covers, [x0, x1) x [y0, y1)
  int head;            // the top of its head, below any ears that stand above it
};

// The animal at size k, bottom-aligned on top + 10k, squashed from the top
Box animal(int which, float cx, float top, float k, float squash, Eyes eyes, float a, int dx, int dy) {
  const sprites::Animal &an = sprites::ANIMALS[which];
  int h = 0;
  while (h < sprites::MAX_ROWS && an.rows[h]) h++;
  int cols = iround(sprites::W * k), full = iround(h * k);
  if (cols < 1) cols = 1;
  int rows = iround(full * squash);
  if (rows < 1) rows = 1;
  int x0 = iround(cx - cols / 2.f) + dx, bottom = iround(top + sprites::BASE_ROWS * k) + dy, y0 = bottom - rows;
  const uint16_t *ink = inks[which];
  for (int yy = 0; yy < rows; yy++) {
    int sy = yy * h / rows;
    const char *line = an.rows[sy];
    for (int xx = 0; xx < cols; xx++) {
      int sx = xx * sprites::W / cols;
      char ch = line[sx];
      if (ch == 'E') {
        bool up = sy > 0 && an.rows[sy - 1][sx] == 'E', down = sy < h - 1 && an.rows[sy + 1][sx] == 'E';
        if ((eyes == SHUT && !up) || (eyes == SMILE && !down))
          ch = an.lid;
        else
          continue;
      }
      if (ch == '.') continue;
      board::light(x0 + xx, y0 + yy, ink[uint8_t(ch) & 127], ch == 'C' ? a * 0.75f : a);
    }
  }
  return {x0, y0, x0 + cols, bottom, bottom - iround(sprites::BASE_ROWS * k * squash)};
}

// A small picture, `u` cells a pixel
void mark(const char *const *rows, int h, int x, int y, int u, uint16_t c, float a) {
  for (int r = 0; r < h; r++)
    for (int q = 0; rows[r][q]; q++)
      if (rows[r][q] == '#')
        for (int v = 0; v < u; v++)
          for (int w = 0; w < u; w++) board::light(x + q * u + w, y + r * u + v, c, a);
}

void block(int x, int y, int u, uint16_t c, float a) {
  for (int v = 0; v < u; v++)
    for (int w = 0; w < u; w++) board::light(x + w, y + v, c, a);
}

void drawPet(const Pet &p, const Session *s, Pose at, float a, bool press) {
  Act act = actOf(p, s);
  float t = nowMs / 1000.f + p.phase;
  int u = iround(at.k / 2) < 1 ? 1 : iround(at.k / 2);  // cells a pixel for its marks
  Eyes eyes = fmodf(t * 0.27f, 1) < 0.035f ? SHUT : OPEN;  // a blink now and then
  int dx = 0, dy = 0;
  float squash = 1;
  float since = (nowMs - p.endAt) / 1000.f;
  switch (act) {
    case THINK: dy = (int(t * 1.6f) & 1) * u; break;
    case TOOL: dy = (int(t * 7) & 1) * u; break;
    case PACK: squash = 0.72f + 0.28f * fabsf(cosf(t * 2.5f)); break;
    case WAIT: dy = -iround(fabsf(sinf(t * 5)) * 2 * u); eyes = OPEN; break;
    case DONE:
      eyes = SMILE;
      if (since < 1.2f) dy = -iround(fabsf(sinf(since * 7)) * 1.5f * u);
      break;
    case FAIL:
      eyes = SHUT;
      if (since < 0.8f) dx = (int(since * 22) & 1) ? u : -u;
      break;
    case NAP: eyes = SHUT, a *= 0.55f, squash = 1 - 0.05f * (0.5f + 0.5f * sinf(t * 1.3f)); break;  // breathing
    case DARK: eyes = SHUT, a *= 0.32f; break;
    case GONE: eyes = SMILE, a *= 0.6f; break;
  }
  // Arriving, it hops in; poked, it hops
  float born = (nowMs - p.bornAt) / 1000.f, poke = (nowMs - p.pokeAt) / 1000.f;
  if (born < 0.7f) dy -= iround(fabsf(sinf(born * 9)) * 2 * u);
  if (poke < 0.7f) dy -= iround(fabsf(sinf(poke * 9)) * 2 * u), eyes = SMILE;
  if (press) squash *= 0.86f;

  // Its shadow on the ground, narrower as it leaves it
  int ground = iround(at.top + sprites::BASE_ROWS * at.k) + 1;
  float lift = dy < 0 ? gfx::clamp01(-dy / (3.f * u)) : 0;
  int sw = iround(sprites::W * at.k * (0.64f - 0.24f * lift));
  int sx = iround(at.cx - sw / 2.f) + dx;
  for (int q = 0; q < sw; q++) {
    float edge = gfx::clamp01((q < sw - 1 - q ? q + 1 : sw - q) / 3.f);
    board::light(sx + q, ground, dim, 0.5f * edge * a);
  }

  Box b = animal(p.animal, at.cx, at.top, at.k, squash, eyes, a, dx, dy);

  // Marks sit by its head; on a large pet, lower, clear of the round's edge
  int side = b.head + (at.k >= 3 ? 3 * u : 0);
  float ph;
  switch (act) {
    case THINK:  // three dots by its head, one lit after another
      for (int q = 0; q < 3; q++) block(b.x1 + u + q * 2 * u, side + u, u, white, (int(t * 3) % 4 > q ? 0.85f : 0.18f) * a);
      break;
    case TOOL:  // sparks thrown up and out
      for (int q = 0; q < 3; q++) {
        ph = fmodf(t * 1.8f + q / 3.f, 1);
        float ang = -2.0f + q * 1.0f, r = (b.x1 - b.x0) / 2.f + ph * 5 * u;
        block(iround((b.x0 + b.x1) / 2.f + cosf(ang) * r), iround(b.head + 3 * u + sinf(ang) * r), u, teal, (1 - ph) * a);
      }
      break;
    case WAIT: board::tiny("!", float(b.x1 + u), side, amber, (0.6f + 0.4f * sinf(t * 8)) * a, u); break;
    case DONE: {
      float rise = since < 1 ? since : 1, fade = since < 3 ? 1 : gfx::clamp01(4 - since);
      mark(sprites::HEART, 6, b.x1 + u, side + 2 * u - iround(rise * 3 * u), u > 1 ? u - 1 : 1, pink, fade * a);
      break;
    }
    case FAIL: {
      float fade = since < 2.5f ? 1 : gfx::clamp01((4 - since) / 1.5f);
      mark(sprites::PUFF, 4, b.x1 + u, side - iround(since * 1.5f * u), u, smoke, 0.8f * fade * a);
      break;
    }
    case NAP:  // Zs drifting up
      for (int q = 0; q < 2; q++) {
        ph = fmodf(t * 0.35f + q * 0.5f, 1);
        board::tiny("Z", float(b.x1 + u + iround(ph * 3 * u)), side + 3 * u - iround(ph * 6 * u), white, 0.6f * (1 - ph) * a / 0.55f, u);
      }
      break;
    case DARK:
      if (s && s->stale) board::tiny("?", float(b.x1 + u), side, dim, 0.7f, u);
      break;
    default: break;
  }
}

// ---------- All ----------

void allText(float a) {
  Pet *order[PETS];
  int n = ranked(order);
  int headY = n >= 7 ? 7 : 18;
  float t = nowMs / 1000.f;

  // The line at the top: who needs you, what just happened, else how they are
  char lines[3][64];
  int m = 0;
  uint16_t c = white;
  float al = 0.55f;
  if (!online()) {
    static const char *const DOTS[] = {"", ".", "..", "..."};
    snprintf(lines[m++], 64, "%s%s", linkState == LOST ? "OFFLINE" : "CONNECTING", linkState == LOST ? "" : DOTS[int(t * 2.5f) % 4]);
    c = dim;
  } else if (tracker.count) {
    Pet *p = petOf(tracker.waits[0].key);
    if (tracker.count > 1 || !p) {
      snprintf(lines[m++], 64, "%d PETS NEED YOU", tracker.count);
      snprintf(lines[m++], 64, "%d NEED YOU", tracker.count);
    } else {
      snprintf(lines[m++], 64, "%s NEEDS YOU", sprites::ANIMALS[p->animal].name);
      snprintf(lines[m++], 64, "NEEDS YOU");
    }
    c = amber, al = 0.7f + 0.3f * sinf(t * 4);
  } else if (nowMs - news.at < NEWS_MS && petOf(news.key)) {
    const char *who = sprites::ANIMALS[petOf(news.key)->animal].name;
    switch (news.outcome) {
      case Outcome::Answer: snprintf(lines[m++], 64, "%s IS DONE!", who), c = green, al = 0.9f; break;
      case Outcome::Error:
      case Outcome::Refusal: snprintf(lines[m++], 64, "%s GOT STUCK", who), c = red, al = 0.9f; break;
      default: snprintf(lines[m++], 64, "%s STOPPED", who); break;
    }
  }
  if (!m) {
    int busy = 0, nap = 0;
    for (int i = 0; i < cur.count; i++) {
      if (model::working(cur.sessions[i].status) && !cur.sessions[i].stale)
        busy++;
      else
        nap++;
    }
    if (!n) {
      snprintf(lines[m++], 64, "NO SESSIONS");
    } else if (!busy) {
      snprintf(lines[m++], 64, "ALL NAPPING");
    } else if (!nap) {
      snprintf(lines[m++], 64, "%d BUSY", busy);
    } else {
      snprintf(lines[m++], 64, "%d BUSY · %d NAPPING", busy, nap);
      snprintf(lines[m++], 64, "%d BUSY %d NAP", busy, nap);
      snprintf(lines[m++], 64, "%d BUSY", busy);
    }
  }
  char line[64];
  pickTiny(lines, m, roomAt(headY + 2.5f), line, sizeof line);
  if (n || online()) board::tinyCentre(line, MID, headY, c, al * a);
  if (!n && online()) {
    // Nobody here: a Z drifts where the pets would be
    float ph = fmodf(t * 0.3f, 1);
    board::tinyCentre("Z", MID + iround(ph * 4), 60 - iround(ph * 10), white, 0.5f * (1 - ph) * a, 2);
  }

  // Each pet's name underneath it
  for (Pet &p : pets) {
    if (!p.used) continue;
    const Session *s = p.leaving ? nullptr : find(p.key);
    int r = 0;
    for (; r < n && order[r] != &p; r++) {
    }
    Slot sl = slot(p.leaving ? n + 1 : n, r < n ? r : 0);
    Act act = actOf(p, s);
    // What it is doing, else what it works on
    char name[48], fit[48];
    switch (act) {
      case THINK: snprintf(name, sizeof name, "THINK"); break;
      case TOOL: toolName(s->tool, name, sizeof name); break;
      case PACK: snprintf(name, sizeof name, "COMPACT"); break;
      case WAIT: snprintf(name, sizeof name, s->status == Status::WaitingInput ? "ASKING" : "APPROVE"); break;
      case DONE: snprintf(name, sizeof name, "DONE!"); break;
      case FAIL: snprintf(name, sizeof name, "STUCK"); break;
      default: snprintf(name, sizeof name, "%s", p.label); break;
    }
    board::tinyFit(name, sl.chars, fit, sizeof fit);
    uint16_t lc = act == THINK || act == PACK ? white : actColor(act);
    float la = act == NAP || act == DARK || act == GONE ? 0.45f : 0.8f;
    board::tinyCentre(fit, p.x.x, iround(p.y.x + sprites::BASE_ROWS * p.k.x) + 4, lc, la * a * p.shown);
  }

  // What they have used between them
  if (online() && n && n <= 6) {
    uint64_t total = 0;
    for (int i = 0; i < cur.count; i++) total += cur.sessions[i].tokens;
    if (total) {
      char v[24], s[40];
      count(total, v, sizeof v);
      snprintf(s, sizeof s, "%s TOKENS", v);
      board::tinyCentre(s, MID, 97, white, 0.28f * a);
    }
  }
}

// ---------- Alert ----------

void alertText(float a) {
  const Session *s = find(alertKey);
  if (!s) return;
  float cx = MID + slide.x;
  char buf[96];
  text::fit(text::PROP, 1, s->project, 100, buf, sizeof buf);
  board::fontCentre(text::PROP, buf, cx, 65, white, 0.95f * a);
  char tool[48], ask[64];
  if (s->status == Status::WaitingInput) {
    snprintf(ask, sizeof ask, "HAS A QUESTION");
  } else {
    toolName(s->waitTool[0] ? s->waitTool : s->tool, tool, sizeof tool);
    snprintf(ask, sizeof ask, tool[0] ? "APPROVE %s?" : "NEEDS APPROVAL", tool);
  }
  board::tinyCentre(ask, cx, 70, amber, a);
  // What it waits on: the subject or the question
  if (s->waitText[0]) {
    if (ascii(s->waitText)) {
      char lines[3][64];
      int m = wrapTiny(s->waitText, 19, lines, 3);
      for (int k = 0; k < m; k++) board::tinyCentre(lines[k], cx, 78 + k * 7, white, 0.8f * a);
    } else {
      char lines[2][160];
      int m = text::wrap(text::PROP, 1, s->waitText, 84, lines, 2);
      for (int k = 0; k < m; k++) board::fontCentre(text::PROP, lines[k], cx, 85 + k * 11, white, 0.8f * a);
    }
  }
  // Which of the waiting pets this is
  if (tracker.count > 1)
    for (int k = 0; k < tracker.count; k++) {
      bool on = !strcmp(tracker.waits[k].key, alertKey);
      board::light(iround(MID - (tracker.count - 1) * 1.5f) + k * 3, 103, on ? amber : dim, (on ? 0.9f : 0.4f) * a);
    }
}

// ---------- One ----------

// A line round cx in a window maxW wide: still if it fits; else, after two
// seconds' rest, scrolling left a dot at a time and round again, its ends
// fading at the window's edges
void marquee(bool small, const char *s, float cx, int y, int maxW, uint16_t c, float a, int64_t since) {
  int w = small ? board::tinyWidth(s) : text::width(text::PROP, 1, s);
  if (w <= maxW) {
    if (small)
      board::tinyCentre(s, cx, y, c, a);
    else
      board::fontCentre(text::PROP, s, cx, y, c, a);
    return;
  }
  constexpr int PAD = 4, GAP = 18;  // cells in from the window's edge at rest; between the copies
  int x0 = iround(cx - maxW / 2.f), loop = w + GAP;
  float t = (nowMs - since) / 1000.f - 2;
  int off = t <= 0 ? 0 : int(fmodf(t * 9, float(loop)));  // 9 dots a second
  board::clip(x0, x0 + maxW, PAD);
  for (int k = 0; k < 2; k++) {
    float x = float(x0 + PAD - off + k * loop);
    if (small)
      board::tiny(s, x, y, c, a);
    else
      board::font(text::PROP, s, x, y, c, a);
  }
  board::clip(0, N, 0);
}

char titleKey[96];
int64_t titleSince;

void oneText(float a) {
  float t = nowMs / 1000.f;
  const Session *s = find(oneKey);
  if (!s) {
    // Nobody here: a Z drifts where a pet would be; or not connected yet
    static const char *const DOTS[] = {"", ".", "..", "..."};
    char msg[32];
    if (online())
      snprintf(msg, sizeof msg, "NO SESSIONS");
    else
      snprintf(msg, sizeof msg, "%s%s", linkState == LOST ? "OFFLINE" : "CONNECTING", linkState == LOST ? "" : DOTS[int(t * 2.5f) % 4]);
    board::tinyCentre(msg, MID, 70, dim, 0.7f * a);
    float ph = fmodf(t * 0.3f, 1);
    board::tinyCentre("Z", MID + iround(ph * 4), 52 - iround(ph * 10), white, 0.5f * (1 - ph) * a, 2);
    return;
  }
  float cx = MID + slide.x;
  const Pet *p = petOf(oneKey);
  Act act = p ? actOf(*p, s) : NAP;

  // A new pet's lines start scrolling from the start
  if (strcmp(titleKey, oneKey)) snprintf(titleKey, sizeof titleKey, "%s", oneKey), titleSince = nowMs;

  // The title, on one line that scrolls when it is long
  marquee(false, s->title[0] ? s->title : s->project, cx, 71, 100, white, 0.95f * a, titleSince);

  // What it is doing; not connected, that first
  char opts[3][64], state[64];
  int n = stateLines(*s, opts);
  if (!online()) n = 1, snprintf(opts[0], 64, "%s", linkState == LOST ? "OFFLINE" : "CONNECTING...");
  pickTiny(opts, n, roomAt(79), state, sizeof state);
  board::tinyCentre(state, cx, 77, online() ? actColor(act) : dim, 0.9f * a);

  if (online() && events::waiting(*s)) {
    // What it waits on
    char tool[48], ask[64];
    if (s->status == Status::WaitingInput) {
      snprintf(ask, sizeof ask, "HAS A QUESTION");
    } else {
      toolName(s->waitTool[0] ? s->waitTool : s->tool, tool, sizeof tool);
      snprintf(ask, sizeof ask, tool[0] ? "APPROVE %s?" : "NEEDS APPROVAL", tool);
    }
    board::tinyCentre(ask, cx, 86, amber, 0.85f * a);
    if (s->waitText[0] && ascii(s->waitText)) marquee(true, s->waitText, cx, 93, roomAt(95), white, 0.7f * a, titleSince);
  } else {
    // What it has used: tokens, and how full its context is as a row of
    // dots that turns amber at 70% and red at 90%
    bool hasTokens = s->tokens > 0, hasContext = s->context >= 0;
    char tok[24] = "", v[24];
    if (hasTokens) {
      count(s->tokens, v, sizeof v);
      snprintf(tok, sizeof tok, "%s%s", s->tokensExact ? "" : "~", v);
    }
    int tw = board::tinyWidth(tok), bar = hasContext ? 20 : 0, gap = tw && bar ? 3 : 0;
    float x = cx - (tw + gap + bar) / 2.f;
    if (tw) board::tiny(tok, x, 86, white, 0.75f * a);
    if (bar) {
      int filled = iround(s->context / 5);
      uint16_t c = s->context >= 90 ? red : s->context >= 70 ? amber : teal;
      int bx = iround(x) + tw + gap;
      for (int q = 0; q < bar; q++)
        for (int r = 87; r <= 88; r++) board::light(bx + q, r, q < filled ? c : dim, (q < filled ? 0.9f : 0.25f) * a);
    }
    // Its project and agent
    char proj[64], agent[32], line[96], fit[96];
    text::upper(s->project, proj, sizeof proj);
    agentName(s->agent, agent, sizeof agent);
    snprintf(line, sizeof line, "%s · %s", proj, agent);
    if (board::tinyWidth(line) > roomAt(97)) snprintf(line, sizeof line, "%s", proj);
    board::tinyFit(line, roomAt(97) / 4, fit, sizeof fit);
    board::tinyCentre(fit, cx, 95, white, 0.4f * a);
  }

  // A dot for each session in order, this one long, each in its state's colour
  if (cur.count > 1) {
    int w = (cur.count - 1) * 3 + 3, x = iround(MID - w / 2.f);
    for (int i = 0; i < cur.count; i++) {
      const Session &o = cur.sessions[i];
      bool here = !strcmp(o.key, oneKey);
      const Pet *op = petOf(o.key);
      uint16_t c = op ? actColor(actOf(*op, &o)) : dim;
      if (op && actOf(*op, &o) == NAP) c = white;
      for (int q = 0; q < (here ? 3 : 1); q++) board::light(x + q, 104, c, (here ? 0.95f : 0.4f) * a);
      x += here ? 5 : 3;
    }
  }
}

// ---------- The frame ----------

const int8_t STEPS[8][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}};

void compose() {
  board::clear();
  board::level(dimK);
  int k = int(nowMs / SHIFT_MS % 8);
  board::nudge(STEPS[k][0], STEPS[k][1]);

  float one = shown(ONE), all = shown(ALL), al = shown(ALERT);
  if (one > 0) oneText(one);
  if (all > 0) allText(all);
  if (al > 0) alertText(al);

  float t = trans.x;
  for (int i = 0; i < PETS; i++) {
    Pet &p = pets[i];
    if (!p.used) continue;
    Pose a = poseOn(from, p), b = poseOn(to, p);
    Pose at = from == to ? b : Pose{lerp(a.cx, b.cx, t), lerp(a.top, b.top, t), lerp(a.k, b.k, gfx::clamp01(t))};
    float alpha = from == to ? alphaOn(to, p) : lerp(alphaOn(from, p), alphaOn(to, p), gfx::clamp01(t));
    alpha *= p.shown;
    if (alpha < 0.02f) continue;
    drawPet(p, p.leaving ? nullptr : find(p.key), at, alpha, pressed == i && drag == Drag::Undecided);
  }
}

// ---------- Touch ----------

// The pet at home under a panel pixel, or -1
int petAt(int x, int y) {
  int ci = board::cellOf(x), cj = board::cellOf(y);
  for (int i = 0; i < PETS; i++) {
    const Pet &p = pets[i];
    if (!p.used || p.leaving) continue;
    float w = sprites::W * p.k.x, h = (sprites::BASE_ROWS + 2) * p.k.x;
    // A little more than the animal, to be easy to hit
    if (ci >= p.x.x - w / 2 - 3 && ci <= p.x.x + w / 2 + 3 && cj >= p.y.x - 3 && cj <= p.y.x + h + 6) return i;
  }
  return -1;
}

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

// The big pet on ONE under a panel pixel, or -1
int onePetAt(int x, int y) {
  int ci = board::cellOf(x), cj = board::cellOf(y);
  if (ci < MID - 28 || ci > MID + 28 || cj < 11 || cj > 60) return -1;
  for (int i = 0; i < PETS; i++)
    if (pets[i].used && !pets[i].leaving && !strcmp(pets[i].key, oneKey)) return i;
  return -1;
}

// The session before or after key in the summary's order
const char *neighbour(const char *key, int dir) {
  if (!cur.count) return nullptr;
  int at = 0;
  for (int i = 0; i < cur.count; i++)
    if (!strcmp(cur.sessions[i].key, key)) at = i;
  return cur.sessions[(at + dir + cur.count) % cur.count].key;
}

void tap(int x, int y) {
  // Any tap answers the reminders of the waits there are
  tracker.ack();
  switch (to) {
    case ALERT:
      alertUntil = nowMs;
      go(ONE);
      break;
    case ALL: {
      int i = petAt(x, y);
      if (i >= 0) {
        choose(pets[i].key);
        go(ONE);
      }
      break;
    }
    case ONE: {
      int i = onePetAt(x, y);
      if (i >= 0) pets[i].pokeAt = nowMs;
      break;
    }
  }
}

void decide(int dx, int dy) {
  bool across = abs(dx) > abs(dy);
  Drag d = Drag::None;
  Screen target = to;
  switch (to) {
    case ONE:
      if (across)
        d = Drag::Slide;
      else if (dy < 0)
        d = Drag::Move, target = ALL;
      break;
    case ALL:
      if (!across && dy > 0) d = Drag::Move, target = ONE;
      break;
    case ALERT:
      tracker.ack();
      if (across)
        d = Drag::Slide;
      else
        d = Drag::Move, target = ONE;
      break;
  }
  drag = d;
  pressed = -1;
  if (d == Drag::Move) {
    moveFrom = to;
    if (to == ALERT) alertUntil = nowMs;
    go(target, true);
  }
}

float carried(int dx, int dy) {
  switch (moveFrom) {
    case ONE: return -dy / MOVE_PX;
    case ALL: return dy / MOVE_PX;
    default: return sqrtf(float(dx * dx + dy * dy)) / MOVE_PX;
  }
}

}  // namespace

bool init(const uint8_t *cjk, size_t cjkLen) {
  if (!gfx::init() || !text::init(cjk, cjkLen) || !board::init()) return false;
  white = gfx::c565(WHITE), dim = gfx::c565(DIM), amber = gfx::c565(AMBER), green = gfx::c565(GREEN);
  red = gfx::c565(RED), pink = gfx::c565(PINK), teal = gfx::c565(TEAL), smoke = gfx::c565(SMOKE);
  for (int a = 0; a < sprites::COUNT; a++)
    for (const sprites::Ink &k : sprites::ANIMALS[a].inks)
      if (k.key) inks[a][uint8_t(k.key) & 127] = gfx::c565(hex(k.rgb));
  return true;
}

void link(Link state) {
  if (state != ONLINE) {
    // Nothing on screen is current any more
    haveSummary = false;
    tracker.clear();
    drag = Drag::None;
    pressed = -1;
    go(ONE);
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
  syncPets();
  return true;
}

void touch(Touch kind, int x, int y, int64_t ms) {
  lastTouch = ms;
  int dx = x - downX, dy = y - downY;
  switch (kind) {
    case DOWN:
      drag = Drag::Undecided;
      downX = x, downY = y, downAt = ms;
      sampleCount = 0;
      samples[sampleCount++ % 6] = {x, y, ms};
      pressed = to == ALL ? petAt(x, y) : to == ONE ? onePetAt(x, y) : -1;
      break;
    case MOVE:
      if (drag == Drag::None) break;
      samples[sampleCount++ % 6] = {x, y, ms};
      if (drag == Drag::Undecided && dx * dx + dy * dy > 10 * 10) decide(dx, dy);
      if (drag == Drag::Slide) {
        int many = to == ALERT ? tracker.count : cur.count;
        slide.x = dx / float(board::PITCH) * (many > 1 ? 1 : 0.25f);
        slide.v = 0;
      } else if (drag == Drag::Move) {
        float c = carried(dx, dy);
        trans.x = c < 0 ? 0 : c > 1.05f ? 1.05f : c;
        trans.v = 0;
      }
      break;
    case UP: {
      if (drag == Drag::None) break;
      samples[sampleCount++ % 6] = {x, y, ms};
      float vx, vy;
      velocity(vx, vy);
      Drag was = drag;
      drag = Drag::None;
      pressed = -1;
      if (was == Drag::Undecided) {
        if (dx * dx + dy * dy < 14 * 14 && ms - downAt < 600) tap(x, y);
      } else if (was == Drag::Slide) {
        int many = to == ALERT ? tracker.count : cur.count;
        bool next = many > 1 && (dx < -60 || vx < -500), prev = many > 1 && (dx > 60 || vx > 500);
        if (next || prev) {
          int dir = next ? 1 : -1;
          if (to == ALERT) {
            int at = 0;
            for (int k = 0; k < tracker.count; k++)
              if (!strcmp(tracker.waits[k].key, alertKey)) at = k;
            snprintf(alertKey, sizeof alertKey, "%s", tracker.waits[(at + dir + tracker.count) % tracker.count].key);
            alertAt = nowMs;
          } else if (const char *k = neighbour(oneKey, dir)) {
            char key[96];
            snprintf(key, sizeof key, "%s", k);
            choose(key);
          }
          // The new one is already on its way in from the far side
          slide.x += dir * float(N);
        }
        slide.v = vx / board::PITCH;
      } else if (was == Drag::Move) {
        float c = carried(dx, dy), speed = carried(int(vx), int(vy));
        if (c > 0.35f || speed > 2.5f) {
          driving = false;
          trans.v = speed > 0 ? speed : 0;
        } else {
          go(moveFrom);  // not far enough: back it goes
          trans.v = speed < 0 ? -speed : 0;
        }
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
  wallMs = wall ? wall : summaryTs + (ms - summaryAt);
  step(dt);
  uint32_t t0 = gfx::ticks();
  compose();
  composeTicks += gfx::ticks() - t0;
  frames++;
}

void draw(uint16_t *band, int y0, int y1) {
  uint32_t t0 = gfx::ticks();
  board::render(band, y0, y1);
  renderTicks += gfx::ticks() - t0;
}

int cue() {
  if (cueHead == cueTail) return -1;
  int c = cues[cueHead];
  cueHead = (cueHead + 1) % 8;
  return c;
}

const char *screen() {
  static const char *const NAMES[] = {"one", "all", "alert"};
  return NAMES[to];
}

void timings(char *out, size_t len) {
  float k = frames ? 1 / (gfx::ticksPerMs() * frames) : 0;
  snprintf(out, len, "compose %.2f render %.2f", composeTicks * k, renderTicks.exchange(0) * k);
  composeTicks = 0;
  frames = 0;
}

}  // namespace ui
