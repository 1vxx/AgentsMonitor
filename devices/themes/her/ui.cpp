#ifndef __clang__
#pragma GCC optimize("O2")
#endif

// The round screen is her face, from one photo of her. Every expression is
// rendered ahead of time on a computer (devices/tools/face_pack.py, with
// LivePortrait, from the table in faces.py) into a photo pack the board keeps
// in its data partition; this plays its clips at the pack's frame rate, and
// puts her words over them, drawn smooth from glyphs baked into the pack, and
// a row of capsules along the bottom.
//
// She is one person, and every session is something she is dealing with for
// you. Each has a capsule, in its colour, in the order they came; the one she
// is telling you about, by default whoever most needs you, is long and lit,
// and the light slides when she moves on. Her face shows that one: moving on,
// she leaves one expression and enters the next. News from a session she is
// not on makes its capsule glow and gets a word from her. Swipe
// sideways to move from one to the next: her words slide over with the
// finger, and her face moves to the next one's; press the side button twice
// and she goes through them all. Taps are for her, not for the sessions.
//
// Waiting, she nags as an unanswered text does: "在吗?", "?" at 2 minutes,
// "are you gaming again" at 5, "fine." at 15, and "I'm fine." at 25 with the
// photo gone cold. Reading her and not answering (the side button, or a tap)
// is "left on read"; answer after the cold war and she huffs until three
// pokes or a few seconds. A finished turn she winks for praise until tapped,
// which tells the daemon you saw it. Tap her head for a pat, her cheek for a
// poke, the middle of her face to say hello; hold the other button for a
// kiss. Without a pack or a decoder, a silhouette stands in.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "gesture.h"
#include "gfx.h"
#include "jpeg.h"
#include "model.h"
#include "spring.h"
#include "text.h"
#include "theme.h"

namespace her {
namespace {

using gfx::c565;
using gfx::hex;
using gfx::RGB;
using model::Outcome;
using model::Session;
using model::Status;

// ---------- Look ----------

const RGB WHITE = hex(0xfff4f8), DIM = hex(0xb4acbc), AMBER = hex(0xffba46), GREEN = hex(0x8ce1a0), RED = hex(0xff6a5e),
          PINK = hex(0xff7aa6), ICE = hex(0xa8dcff), FROST = hex(0x1d3550), GREY = hex(0x8a8496);

constexpr int W = gfx::W, H = gfx::H;
constexpr int LINE_Y = 370, SUB_Y = 397;  // baselines of her words and the line under them
constexpr float PILL_Y = 431, PILL_H = 5;  // the capsules along the bottom: their top and height,
constexpr float PILL_W = 8, PILL_LIT = 26, PILL_GAP = 7;  // their width, the lit one's, and the gap
constexpr int64_t MIN = 60000;
constexpr int64_t FOCUS_MS = 10000;      // one a finger picks stays this long
constexpr int64_t TOUR_MS = 4000;        // each stop of a tour along the arc
constexpr int64_t SULK_MS = 5 * MIN;     // a finished turn nobody praised
constexpr int64_t UNSEEN_MS = 60 * MIN;  // and how long it waits for praise at all
constexpr int64_t ABORT_MS = 15000;      // cut off mid-sentence, she says so this long
constexpr int64_t HELLO_MS = 4000;       // "在吗?" before what she wants
constexpr int64_t BORED_MS = 3 * MIN, NAP_MS = 10 * MIN;
constexpr int64_t FLASH_MS = 1500;       // a capsule's glow when there is news from it
constexpr int64_t SAY_MS = 2600;         // a reaction's words
constexpr int SWIPE_PX = 60;            // a sideways drag this long moves to the next session
constexpr float SLIDE_PX = 300;         // how far apart neighbouring sessions' words sit

const theme::State &host = theme::state();
const model::Summary &cur = host.summary;

// ---------- The photo pack ----------
//
// Little-endian: "FPK2", u32 total length, u16 width, u16 height, u16 fps,
// u16 clip count, u32 offset and u32 length of the text section; per clip 16
// bytes of name, u16 first frame, u16 frame count; u32 frame count, then per
// frame u32 offset and u32 length of a baseline JPEG of the whole frame; then
// the text section. "FPK1" is the same without the text. See
// devices/tools/face_pack.py.

struct Clip {
  char name[16];
  uint16_t first, count;
};
struct {
  bool ok;
  int fps, clips, frames;
  Clip *clip;       // the clips and the frames' offsets and lengths, read once
  uint32_t *index;
  const uint8_t *base;  // the pack in memory, if it is (the review page)
  uint8_t *jpg;         // else a frame read into here, as big as the biggest
} pack;

// ---------- Smooth text ----------
//
// Glyphs from the pack's text section: a face for her lines and one for the
// line under them, each glyph a box of coverage and, for a face with a
// shadow, a box of the shadow's. Without them, the dot font.

struct Glyph {
  uint32_t cp, at;  // its code point, and where its bitmap is in the section
  uint8_t w, h;
  int8_t x, y;      // the box's left from the pen, and top from the baseline
  uint8_t adv, layers;
  uint16_t pad;
};
struct Face {
  const Glyph *g;
  int n;
};
enum { LINE_FACE, SUB_FACE, FACES };
Face faces[FACES];
const uint8_t *glyphBits;  // the section, which glyphs' offsets count from

bool openText(const theme::Data &d, uint32_t at, uint32_t len) {
  uint8_t *copy = d.p ? nullptr : (uint8_t *)gfx::bigAlloc(len);
  const uint8_t *t = d.p ? d.p + at : copy;
  if (!t || (copy && !d.read(at, copy, len)) || len < 8 || memcmp(t, "FNT1", 4)) return false;
  uint16_t n;
  memcpy(&n, t + 4, 2);
  if (8 + 16 * size_t(n) > len) return false;
  for (int i = 0; i < n && i < FACES; i++) {
    uint32_t count, gat;
    memcpy(&count, t + 8 + 16 * i + 8, 4);
    memcpy(&gat, t + 8 + 16 * i + 12, 4);
    if (gat + 16 * size_t(count) > len) return false;
    faces[i] = {(const Glyph *)(t + gat), int(count)};
  }
  glyphBits = t;
  return true;
}

bool smooth(int face) { return glyphBits && faces[face].n; }

const Glyph *glyphOf(int face, uint32_t cp) {
  const Face &f = faces[face];
  for (int lo = 0, hi = f.n - 1; lo <= hi;) {
    int m = (lo + hi) / 2;
    if (f.g[m].cp < cp) lo = m + 1;
    else if (f.g[m].cp > cp) hi = m - 1;
    else return &f.g[m];
  }
  return cp == '?' ? nullptr : glyphOf(face, '?');
}

int widthOf(int face, const char *s) {
  int w = 0;
  for (uint32_t cp; (cp = text::next(s));)
    if (const Glyph *g = glyphOf(face, cp)) w += g->adv;
  return w;
}

// One glyph onto the band, its box's top left at (x0, y0): its shadow in
// black, the one baked with it or, a glyph without one, its own coverage a
// pixel down and right; then its coverage in c
void drawGlyph(const Glyph *g, int x0, int y0, uint16_t c, float a) {
  if (gfx::recording) return gfx::keep(y0, y0 + g->h + 1, [=] { drawGlyph(g, x0, y0, c, a); });
  const uint8_t *bits = glyphBits + g->at;
  int area = g->w * g->h, from = x0 < 0 ? -x0 : 0, to = x0 + g->w > W ? W - x0 : g->w;
  float k = a * 32 / 255.f, ks = 0.75f * k;
  bool drop = g->layers < 2;
  for (int y = y0 < gfx::clipY0 ? gfx::clipY0 : y0; y <= y0 + g->h && y < gfx::clipY1; y++) {
    const uint8_t *row = bits + (y - y0) * g->w;
    for (int x = from; x < to; x++) {
      int i = y * W + x0 + x;
      if (drop) {
        // Coverage of the pixel up and to the left, as shadow here
        uint8_t up = y > y0 && x > 0 ? row[x - 1 - g->w] : 0;
        if (up) gfx::blend(i, 0, uint32_t(up * ks + 0.5f));
      } else if (y < y0 + g->h && row[area + x]) {
        gfx::blend(i, 0, uint32_t(row[area + x] * ks + 0.5f));
      }
      if (y < y0 + g->h && row[x]) gfx::blend(i, c, uint32_t(row[x] * k + 0.5f));
    }
  }
}

// s centred on cx with its baseline at y, cut short with an ellipsis to fit maxW
void smoothText(int face, const char *s, float cx, int y, uint16_t c, float a, int maxW) {
  char fitted[400];
  snprintf(fitted, sizeof fitted, "%s", s);
  if (widthOf(face, fitted) > maxW) {
    int room = maxW - widthOf(face, "…"), w = 0;
    const char *p = fitted, *cut = fitted;
    for (uint32_t cp; (cp = text::next(p));) {
      const Glyph *g = glyphOf(face, cp);
      if ((w += g ? g->adv : 0) > room) break;
      cut = p;
    }
    snprintf(fitted + (cut - fitted), sizeof fitted - (cut - fitted), "…");
  }
  float x = cx - widthOf(face, fitted) / 2.f;
  for (const char *p = fitted; uint32_t cp = text::next(p);) {
    const Glyph *g = glyphOf(face, cp);
    if (!g) continue;
    if (g->w) drawGlyph(g, int(x + g->x + 0.5f), y + g->y, c, a);
    x += g->adv;
  }
}

bool openPack(const theme::Data &d) {
  uint8_t head[24];
  if (!d.read || d.len < 24 || !d.read(0, head, 24)) return false;
  bool v2 = !memcmp(head, "FPK2", 4);
  if (!v2 && memcmp(head, "FPK1", 4)) return false;
  uint16_t h[4];
  memcpy(h, head + 8, sizeof h);
  size_t top = v2 ? 24 : 16, at = top + size_t(h[3]) * sizeof(Clip);
  uint32_t frames;
  if (h[0] != W || h[1] != H || !h[2] || !h[3] || at + 4 > d.len || !d.read(at, &frames, 4)) return false;
  if (at + 4 + 8 * size_t(frames) > d.len) return false;
  pack.fps = h[2], pack.clips = h[3], pack.frames = int(frames);
  pack.clip = (Clip *)gfx::bigAlloc(pack.clips * sizeof(Clip));
  pack.index = (uint32_t *)gfx::bigAlloc(8 * size_t(frames));
  if (!pack.clip || !pack.index || !d.read(top, pack.clip, pack.clips * sizeof(Clip)) || !d.read(at + 4, pack.index, 8 * frames))
    return false;
  uint32_t biggest = 0;
  for (int f = 0; f < pack.frames; f++) {
    if (pack.index[2 * f] > d.len || pack.index[2 * f + 1] > d.len - pack.index[2 * f]) return false;
    if (pack.index[2 * f + 1] > biggest) biggest = pack.index[2 * f + 1];
  }
  for (int c = 0; c < pack.clips; c++)
    if (!pack.clip[c].count || pack.clip[c].first + pack.clip[c].count > pack.frames) return false;
  pack.base = d.p;
  if (!pack.base && !(pack.jpg = (uint8_t *)gfx::bigAlloc(biggest))) return false;
  uint32_t textAt, textLen;
  memcpy(&textAt, head + 16, 4);
  memcpy(&textLen, head + 20, 4);
  if (v2 && textLen && textAt <= d.len && textLen <= d.len - textAt) openText(d, textAt, textLen);
  return pack.ok = true;
}

int clipNamed(const char *name) {
  for (int c = 0; c < pack.clips; c++)
    if (!strncmp(pack.clip[c].name, name, sizeof pack.clip[c].name)) return c;
  return -1;
}

// ---------- Playing clips ----------
//
// A queue of clips: each plays once, or loops until another follows it.
// Her face rests in an expression (or "idle"): leaving it plays its .out,
// entering one plays its .in, then its .loop; so she always moves from one
// face to the next, never jumps.

struct Seg {
  int clip;
  bool loop;
};
Seg queue[16];
int queued;
int64_t segAt;            // when queue[0] started
char resting[16] = "idle";  // the expression the queue ends in
uint16_t *photo;          // the frame on show, decoded, 16-byte aligned
int shown = -1;           // which of the pack's frames it is
uint32_t decodeTicks, decodes;

void push(const char *name, bool loop) {
  int c = clipNamed(name);
  if (c < 0) return;  // a pack without it: she does without
  // A loop at the end that is not playing would only be cut
  if (queued > 1 && queue[queued - 1].loop) queued--;
  if (queued == int(sizeof queue / sizeof *queue)) return;
  if (!queued) segAt = 0;
  queue[queued++] = {c, loop};
}

void pushPart(const char *pose, const char *part, bool loop) {
  char name[24];
  snprintf(name, sizeof name, "%s.%s", pose, part);
  push(name, loop);
}

void leave() {
  if (strcmp(resting, "idle")) pushPart(resting, "out", false);
  snprintf(resting, sizeof resting, "idle");
}

void enter(const char *pose) {
  if (strcmp(pose, "idle")) {
    pushPart(pose, "in", false);
    pushPart(pose, "loop", true);
  } else {
    push("idle", true);
  }
  snprintf(resting, sizeof resting, "%s", pose);
}

void settle(const char *pose) {
  if (strcmp(pose, resting)) leave(), enter(pose);
}

// A one-off clip, then back to the face she was in, or to `then`
void reaction(const char *clip, const char *then = nullptr) {
  char back[16];
  snprintf(back, sizeof back, "%s", then ? then : resting);
  leave();
  push(clip, false);
  enter(back);
}

// Plays the queue up to now, and decodes the frame on show when it changes
void advance() {
  if (!pack.ok) return;
  if (!queued) push("idle", true), segAt = host.now;
  int frame = -1;
  for (int guard = 0; guard < 32 && queued; guard++) {
    const Seg &s = queue[0];
    const Clip &c = pack.clip[s.clip];
    if (!segAt) segAt = host.now;
    int64_t t = host.now - segAt, step = 1000 / pack.fps, span = int64_t(c.count) * 1000 / pack.fps;
    if (s.loop && queued > 1 && t >= step) {
      // Cut the loop: what follows starts now
      memmove(queue, queue + 1, --queued * sizeof *queue), segAt = host.now;
      continue;
    }
    if (!s.loop && t >= span) {
      memmove(queue, queue + 1, --queued * sizeof *queue), segAt += span;
      if (!queued) push("idle", true), segAt = host.now;
      continue;
    }
    frame = c.first + int(t * pack.fps / 1000 % c.count);
    break;
  }
  if (frame < 0 || frame == shown || !photo || !jpeg::decode) return;
  uint32_t t = gfx::ticks(), at = pack.index[2 * frame], n = pack.index[2 * frame + 1];
  // In memory it decodes where it is; else this one frame is read in first
  const uint8_t *jpg = pack.base ? pack.base + at : theme::data().read(at, pack.jpg, n) ? pack.jpg : nullptr;
  if (jpg && jpeg::decode(jpg, n, photo, W, H)) shown = frame;
  decodeTicks += gfx::ticks() - t, decodes++;
}

// ---------- State ----------

gesture::Finger finger;
char chosenKey[96], talkingKey[96];
int64_t chosenAt = -FOCUS_MS;
int64_t touchedAt;  // wall-clock time of the last touch: she only dozes off when nobody is near
int tourLeft;  // stops left on a tour along the arc
int64_t tourAt;
bool swiping;    // a finger is carrying her words sideways
Spring slide;    // her words' offset while they slide, px
char newsKey[96];  // a session with news, for a word once the frame knows whether she is on it anyway
const char *newsSays;

// The sessions along the arc, in the order they came, each with its flash
struct Mark {
  char key[96];
  int64_t flashAt;
  RGB flash;
} marks[model::MAX];
int marked;
Spring lit;  // where the lit capsule is, in capsules from the left

// A moment over her words
struct {
  char line[96];
  RGB color;
  int64_t until;
} said;

// After a cold war she huffs until poked three times or a while passes
int64_t hmphUntil;
int pokes;

// Each wait she is nagging about, and how far the nagging got
struct Nag {
  char key[96];
  int stage;
} nags[model::MAX];
int nagCount;

// Finished turns a tap already praised, before the daemon says they are seen
struct Praised {
  char key[96];
  int64_t endedAt;
} praised[model::MAX];
int nextPraised;

// What this frame shows, worked out by act()
const char *want;
char line[96], sub[400];
RGB lineColor;
float coldGoal, cold, dimGoal, dim;

// ---------- Sessions ----------

int indexOf(const char *key) {
  if (!host.have) return -1;
  for (int i = 0; i < cur.count; i++)
    if (!strcmp(cur.sessions[i].key, key)) return i;
  return -1;
}

int markOf(const char *key) {
  for (int k = 0; k < marked; k++)
    if (!strcmp(marks[k].key, key)) return k;
  return -1;
}

// Keeps the arc in the order sessions came: those gone leave it, new ones
// join at the end
void placeMarks() {
  int n = 0;
  for (int k = 0; k < marked; k++)
    if (indexOf(marks[k].key) >= 0) marks[n++] = marks[k];
  marked = n;
  for (int i = 0; host.have && i < cur.count && marked < model::MAX; i++) {
    if (markOf(cur.sessions[i].key) >= 0) continue;
    Mark &m = marks[marked++];
    snprintf(m.key, sizeof m.key, "%s", cur.sessions[i].key);
    m.flashAt = -FLASH_MS;
  }
}

bool wasPraised(const Session &s) {
  for (const Praised &p : praised)
    if (p.endedAt == s.endedAt && !strcmp(p.key, s.key)) return true;
  return false;
}

// A finished turn still waiting for praise
bool unseen(const Session &s) {
  if (s.status != Status::Idle || s.seen || wasPraised(s)) return false;
  if (s.outcome != Outcome::Answer && s.outcome != Outcome::Error && s.outcome != Outcome::Refusal) return false;
  return host.wall - s.endedAt < UNSEEN_MS;
}

// Whoever most needs you: waiting, then a finished turn to praise, then
// work, newest turn first as the summary has them
const Session *pressing() {
  if (!host.have || !cur.count) return nullptr;
  for (int i = 0; i < cur.count; i++)
    if (events::waiting(cur.sessions[i])) return &cur.sessions[i];
  for (int i = 0; i < cur.count; i++)
    if (unseen(cur.sessions[i])) return &cur.sessions[i];
  for (int i = 0; i < cur.count; i++)
    if (model::working(cur.sessions[i].status)) return &cur.sessions[i];
  return &cur.sessions[0];
}

// The one she tells you about: a finger's or a tour's pick, else the most pressing
const Session *focused() {
  int i;
  if (chosenKey[0] && host.now - chosenAt < FOCUS_MS && (i = indexOf(chosenKey)) >= 0) return &cur.sessions[i];
  chosenKey[0] = 0;
  return pressing();
}

void choose(const char *key) {
  snprintf(chosenKey, sizeof chosenKey, "%s", key);
  chosenAt = host.now;
}

// The session `dir` along the arc from the one she is on: 1 the next, -1 the one before
const Session *along(const Session *f, int dir) {
  int from = f ? markOf(f->key) : -1;
  if (from < 0) from = dir > 0 ? -1 : 0;
  for (int step = 1; step <= marked; step++) {
    int i = indexOf(marks[((from + dir * step) % marked + marked) % marked].key);
    if (i >= 0) return &cur.sessions[i];
  }
  return nullptr;
}

// How far a wait has got; the steps fall on the host's reminders at 2, 5,
// 15 and 25 minutes
int stageOf(const Session &s) {
  int64_t age = host.wall - events::waitSince(s);
  return age >= 25 * MIN ? 4 : age >= 15 * MIN ? 3 : age >= 5 * MIN ? 2 : age >= 2 * MIN ? 1 : 0;
}

bool has(const char *tool, const char *part) {
  for (const char *t = tool; *t; t++) {
    const char *a = t, *b = part;
    while (*a && *b && (*a | 32) == (*b | 32)) a++, b++;
    if (!*b) return true;
  }
  return false;
}

void say(const char *s) { snprintf(line, sizeof line, "%s", s); }

// What she makes of a tool: her face, and what she says
void busyWith(const char *tool) {
  if (!strncmp(tool, "mcp__", 5)) want = "think", say("出门办事…");
  else if (has(tool, "web")) want = "read", say("逛一下网…");
  else if (has(tool, "grep") || has(tool, "search") || has(tool, "find")) want = "read", say("查岗中…");
  else if (has(tool, "read") || has(tool, "glob") || has(tool, "view") || !strcmp(tool, "LS")) want = "read", say("偷偷翻你文件…");
  else if (has(tool, "edit") || has(tool, "write") || has(tool, "patch")) want = "write", say("在写小作文…");
  else if (has(tool, "todo") || has(tool, "plan")) want = "write", say("列个小清单…");
  else if (!strcmp(tool, "Task") || has(tool, "agent")) want = "think", say("摇闺蜜帮忙…");
  else if (has(tool, "bash") || has(tool, "shell") || has(tool, "exec")) want = "write", say("帮你跑命令…");
  else want = "write", say("帮你忙着呢…");
}

// Her face and words for the session she tells you about
void act(const Session *f) {
  want = "idle";
  line[0] = sub[0] = 0;
  lineColor = WHITE;
  coldGoal = dimGoal = 0;

  if (host.link != ui::ONLINE || !host.have) {
    if (host.link == ui::LOST) want = "shocked", say("你把我拉黑了？！");
    else say("嘟…嘟…");
    return;
  }
  int64_t latest = 0;
  bool quiet = true;
  for (int i = 0; i < cur.count; i++) {
    const Session &s = cur.sessions[i];
    if (s.since > latest) latest = s.since;
    if (model::working(s.status) || events::waiting(s) || unseen(s)) quiet = false;
  }
  if (!f) {
    want = "bored", say("今天不写代码吗？");
    return;
  }
  snprintf(sub, sizeof sub, "%s", f->title[0] ? f->title : f->project);
  if (f->stale) {
    want = "bored", say("人呢？");
    return;
  }

  if (events::waiting(*f)) {
    snprintf(sub, sizeof sub, "%s", f->waitText[0] ? f->waitText : f->waitTool);
    bool input = f->status == Status::WaitingInput;
    switch (stageOf(*f)) {
      case 0:
        want = input ? "ask" : "plead";
        if (host.wall - events::waitSince(*f) < HELLO_MS) say("宝，在吗？");
        else if (input) say("想问你个事儿～");
        else if (has(f->waitTool, "bash")) say("可以让我跑这个吗？");
        else if (has(f->waitTool, "edit") || has(f->waitTool, "write")) say("可以让我改这个吗？");
        else say("这个…可以吗？");
        break;
      case 1: want = "huh", say("？"); break;
      case 2: want = "side_eye", say("你是不是又在打游戏"); break;
      case 3: want = "sulk", say("算了。"), coldGoal = 0.4f; break;
      default: want = "cold", say("没事，我很好。"), coldGoal = 1, lineColor = ICE; break;
    }
    return;
  }

  switch (f->status) {
    case Status::Thinking: want = "think", say("对方正在输入…"); return;
    case Status::Tool: busyWith(f->tool); return;
    case Status::Compacting:
      want = "compact", say("选择性失忆中…");
      if (f->context >= 0) snprintf(sub, sizeof sub, "记仇小本本 %d%% 满了，撕掉几页", int(f->context + 0.5f));
      return;
    default: break;
  }

  // At rest: what the last turn left
  int64_t ago = host.wall - f->endedAt;
  if (unseen(*f)) {
    if (f->outcome == Outcome::Answer && ago < SULK_MS) want = "praise_me", say("做好啦，快夸我！");
    else if (f->outcome == Outcome::Answer) want = "unpraised", say("你都不夸我…");
    else if (f->outcome == Outcome::Error) want = "sad", say("呜…搞砸了");
    else want = "refuse", say("不要。");
    return;
  }
  if (f->outcome == Outcome::Aborted && ago < ABORT_MS) {
    want = "interrupt", say("我话还没说完！");
    return;
  }
  // Picked to hear about, with nothing to tell: she says so, and which it is
  if (chosenKey[0] && !strcmp(chosenKey, f->key)) {
    say("这边没什么事～");
    return;
  }
  sub[0] = 0;
  int64_t still = host.wall - latest;
  if (quiet && still > NAP_MS && host.wall - touchedAt > NAP_MS) want = "sleep", say("Zz"), dimGoal = 0.7f;
  else if (quiet && still > BORED_MS) want = "bored", say("好无聊…");
}

void tell(const char *s, RGB color = WHITE) {
  snprintf(said.line, sizeof said.line, "%s", s);
  said.color = color;
  said.until = host.now + SAY_MS;
}

// Remembers how far each wait's nagging got, and when one is answered,
// reacts as it deserves
void followNags(bool answered) {
  for (int i = 0; i < nagCount;) {
    int at = indexOf(nags[i].key);
    if (at >= 0 && events::waiting(cur.sessions[at])) {
      int st = stageOf(cur.sessions[at]);
      if (st > nags[i].stage) nags[i].stage = st;
      i++;
      continue;
    }
    if (answered && at >= 0) {
      if (nags[i].stage >= 3) hmphUntil = host.now + 6000, pokes = 0;
      else if (nags[i].stage) reaction("makeup"), tell("终于理我了！");
      else reaction("pat"), tell("嘻嘻，谢谢宝～");
    }
    nags[i] = nags[--nagCount];
  }
  for (int k = 0; host.have && k < cur.count && nagCount < model::MAX; k++) {
    const Session &s = cur.sessions[k];
    bool known = false;
    for (int i = 0; i < nagCount; i++) known = known || !strcmp(nags[i].key, s.key);
    if (known || !events::waiting(s)) continue;
    snprintf(nags[nagCount].key, sizeof nags[0].key, "%s", s.key);
    nags[nagCount++].stage = stageOf(s);
  }
}

// ---------- Drawing ----------

// The photo, a row at a time into the band
void photoRows() {
  if (gfx::recording) return gfx::keep(0, H, [] { photoRows(); });
  for (int y = gfx::clipY0; y < gfx::clipY1; y++) memcpy(gfx::fb + y * W, photo + y * W, W * 2);
}

// Colour c over rows [y0, y1) of the round, at a0 at the top to a1 at the bottom
void wash(int y0, int y1, uint16_t c, float a0, float a1) {
  if (gfx::recording) return gfx::keep(y0, y1, [=] { wash(y0, y1, c, a0, a1); });
  gfx::rows(y0, y1, [&](int row, int x0, int x1) {
    float t = float(row / W - y0) / float(y1 - y0);
    gfx::blendRun(row + x0, row + x1, c, uint32_t((a0 + (a1 - a0) * t) * 32));
  });
}

RGB stateColor(const Session &s) {
  if (s.stale) return GREY;
  if (events::waiting(s)) return stageOf(s) >= 4 ? ICE : AMBER;
  if (model::working(s.status)) return PINK;
  if (unseen(s)) return s.outcome == Outcome::Answer ? GREEN : RED;
  return GREY;
}

void compose(const Session *f) {
  bool anyWaiting = false;
  for (int i = 0; host.have && i < cur.count; i++) anyWaiting = anyWaiting || events::waiting(cur.sessions[i]);

  if (shown >= 0) {
    // The pack's frames come with her edge melted into black and the bottom
    // darkened under her words (faces.py's LOOK), smoother than a blend here
    photoRows();
    if (cold > 0.01f) wash(0, H, c565(FROST), 0.45f * cold, 0.45f * cold);
    if (dim > 0.01f) wash(0, H, 0, dim, dim);
  } else if (pack.ok) {
    // The first frame on its way
    gfx::clear();
  } else {
    // No photo: a silhouette stands in, and says how to make one
    gfx::clear();
    gfx::discAt(gfx::CX, 175, 68, c565(GREY), 0.4f);
    gfx::roundRect(gfx::CX - 125, 262, gfx::CX + 125, 480, 100, c565(GREY), 0.28f);
    wash(270, H, 0, 0, 0.8f);
  }

  // The rim: whether anyone needs you, at a glance
  float breath = 0.55f + 0.45f * sinf(host.now * 0.0026f);
  if (anyWaiting) gfx::vignette(186, gfx::CX + 3, c565(cold > 0.5f ? ICE : AMBER), 0.3f * breath);

  // A capsule per session along the bottom, in its colour: the one she is on
  // long and lit, the others short and dim, one with news glowing a moment
  float widths[model::MAX], total = PILL_GAP * (marked - 1);
  for (int k = 0; k < marked; k++) total += widths[k] = PILL_W + (PILL_LIT - PILL_W) * gfx::clamp01(1 - fabsf(k - lit.x));
  float x = gfx::CX - total / 2;
  for (int k = 0; k < marked; k++) {
    int i = indexOf(marks[k].key);
    if (i >= 0) {
      const Session &s = cur.sessions[i];
      float on = gfx::clamp01(1 - fabsf(k - lit.x)), a = (0.4f + 0.6f * on) * (events::waiting(s) ? 0.6f + 0.4f * breath : 1);
      float tf = float(host.now - marks[k].flashAt) / FLASH_MS;
      if (tf < 1) gfx::glowAt(x + widths[k] / 2, PILL_Y + PILL_H / 2, 18, c565(marks[k].flash), 0.7f * (1 - tf));
      gfx::roundRect(x, PILL_Y, x + widths[k], PILL_Y + PILL_H, PILL_H / 2, c565(stateColor(s)), a);
    }
    x += widths[k] + PILL_GAP;
  }

  // Her words: a reaction's for a moment, else what she says about it,
  // fading as they slide away
  float away = 1 - gfx::clamp01(fabsf(slide.x) / (SLIDE_PX * 0.8f));
  const char *words = host.now < said.until ? said.line : line;
  RGB color = host.now < said.until ? said.color : lineColor;
  if (!words[0] && !pack.ok) words = "先生成我的表情包";
  uint16_t subColor = c565(f ? gfx::mix(stateColor(*f), DIM, 0.45f) : DIM);
  if (words[0] && smooth(LINE_FACE)) {
    smoothText(LINE_FACE, words, gfx::CX + slide.x, LINE_Y, c565(color), away, 340);
  } else if (words[0]) {
    int sc = text::width(text::PROP, 3, words) <= 300 ? 3 : 2;
    char fitted[96];
    text::fit(text::PROP, sc, words, 330, fitted, sizeof fitted);
    text::centre(text::PROP, sc, fitted, gfx::CX + slide.x, LINE_Y, c565(color), away);
  }
  if (sub[0] && host.now >= said.until && smooth(SUB_FACE)) {
    smoothText(SUB_FACE, sub, gfx::CX + slide.x, SUB_Y, subColor, 0.9f * away, 270);
  } else if (sub[0] && host.now >= said.until) {
    char fitted[160];
    text::fit(text::PROP, 2, sub, 250, fitted, sizeof fitted);
    text::centre(text::PROP, 2, fitted, gfx::CX + slide.x, SUB_Y, subColor, 0.9f * away);
  }
}

// ---------- The theme ----------

class Her final : public theme::Theme {
 public:
  bool init() override {
    if (openPack(theme::data())) {
      // 16 bytes over, to start the frame on a 16-byte boundary as decoders want
      uint8_t *p = (uint8_t *)gfx::bigAlloc(W * H * 2 + 16);
      if (p) photo = (uint16_t *)(uintptr_t(p + 15) & ~uintptr_t(15));
    }
    return true;
  }

  void reset() override {
    chosenKey[0] = talkingKey[0] = 0;
    said.until = hmphUntil = 0;
    tourLeft = nagCount = marked = 0;
    newsKey[0] = 0;
  }

  void event(const events::Event &e, const model::Summary &) override {
    int k = markOf(e.key);
    if (k >= 0) {
      marks[k].flashAt = host.now;
      marks[k].flash = e.kind == events::WAIT ? AMBER : e.outcome == Outcome::Answer ? GREEN : e.outcome == Outcome::Aborted ? GREY : RED;
    }
    // News from a session she is not on: a word about it, unless she turns to it
    if (strcmp(e.key, talkingKey)) {
      snprintf(newsKey, sizeof newsKey, "%s", e.key);
      if (e.kind == events::WAIT) newsSays = "那边在叫你～";
      else if (e.outcome == Outcome::Answer) newsSays = "那边做好啦～";
      else if (e.outcome == Outcome::Aborted) newsSays = "我话还没说完！";
      else newsSays = "那边出问题了…";
    }
  }

  void summary() override {
    placeMarks();
    followNags(true);
  }

  void touch(ui::Touch kind, int x, int y, int64_t ms) override {
    touchedAt = host.wall;
    if (kind == ui::DOWN) {
      finger.down(x, y, ms);
      return;
    }
    // Sideways, the finger carries her words; let go far or fast enough and
    // the next session's (or the one before's) come in from that side
    if (kind == ui::MOVE) {
      finger.move(x, y, ms);
      if (!swiping && finger.dragging() && abs(finger.dx()) > abs(finger.dy())) swiping = true, tourLeft = 0;
      if (swiping) slide.x = float(finger.dx()), slide.v = 0;
      return;
    }
    finger.up(x, y, ms);
    if (swiping) {
      swiping = false;
      float vx, vy;
      finger.velocity(vx, vy);
      int dir = finger.dx() < -SWIPE_PX || vx < -500 ? 1 : finger.dx() > SWIPE_PX || vx > 500 ? -1 : 0;
      const Session *f = focused(), *n = dir ? along(f, dir) : nullptr;
      if (n && n != f) choose(n->key), slide.x += dir * SLIDE_PX;
      slide.v = vx;
      return;
    }
    if (!finger.tapped(ms)) return;
    tourLeft = 0;

    const Session *f = focused();
    int dx = abs(x - int(gfx::CX));
    if (host.now < hmphUntil) {
      // Three pokes and she can't keep it up
      if (++pokes >= 3) hmphUntil = host.now, reaction("makeup", "idle"), tell("这还差不多。");
      else tell("哼！");
    } else if (y < 150 && dx < 130) {
      reaction("pat"), tell("嘿嘿～");
    } else if (dx > 70 && dx < 175 && y > 190 && y < 320) {
      reaction("poke"), tell("干嘛～");
    } else if (f && unseen(*f)) {
      theme::seen(f->key, f->endedAt);
      Praised &p = praised[nextPraised++ % model::MAX];
      snprintf(p.key, sizeof p.key, "%s", f->key);
      p.endedAt = f->endedAt;
      reaction("pat"), tell("被夸了，嘿嘿～");
    } else if (f && events::waiting(*f)) {
      theme::ack();
      reaction("left_on_read"), tell("已读不回？", PINK);
    } else {
      reaction("makeup"), tell("嘿嘿，在呢～");
    }
  }

  void press(int times, int64_t) override {
    const Session *f = focused();
    if (times == 2) {
      // Through them all, along the arc
      tourLeft = marked;
      tourAt = host.now - TOUR_MS;
      return;
    }
    tourLeft = 0;
    if (f && events::waiting(*f)) reaction("left_on_read"), tell("已读不回？", PINK);
    chosenKey[0] = 0;
  }

  void summon(int64_t) override { reaction("kiss"), tell("啵～"); }

  void frame() override {
    if (tourLeft && host.now - tourAt >= TOUR_MS) {
      const Session *n = along(focused(), 1);
      if (n) choose(n->key);
      tourAt = host.now;
      tourLeft--;
    }
    followNags(false);
    const Session *f = focused();
    act(f);
    if (host.now < hmphUntil) want = "hmph", say("哼。"), coldGoal = 0.2f;

    // Her face for the one she is on; news from another, a word about it
    const char *key = f ? f->key : "";
    snprintf(talkingKey, sizeof talkingKey, "%s", key);
    settle(want);
    if (newsKey[0] && strcmp(newsKey, key)) tell(newsSays);
    newsKey[0] = 0;
    int k = f ? markOf(f->key) : -1;
    if (k >= 0) lit.step(float(k), host.dt, 120, 19);
    if (!swiping) slide.step(0, host.dt, 200, 24);
    advance();
    float dt = gfx::clamp01(host.dt * 0.8f);
    cold += (coldGoal - cold) * dt, dim += (dimGoal - dim) * dt;

    text::newFrame();
    gfx::record(true);
    compose(f);
    gfx::record(false);
  }

  void draw(uint16_t *band, int y0, int y1) override { gfx::play(band, y0, y1); }

  // The clip playing, for the logs and the review page
  const char *screen() const override {
    if (shown < 0) return "silhouette";
    return queued ? pack.clip[queue[0].clip].name : resting;
  }

  void timings(char *out, size_t len) override {
    snprintf(out, len, "decode %.1f ms x%u", decodes ? decodeTicks / gfx::ticksPerMs() / decodes : 0.f, unsigned(decodes));
    decodeTicks = decodes = 0;
  }
};

}  // namespace
}  // namespace her

theme::Theme &theme::instance() {
  static her::Her her;
  return her;
}
