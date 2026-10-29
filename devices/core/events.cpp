#include "events.h"

#include <stdio.h>
#include <string.h>

namespace events {

using model::Session;

const Session *find(const model::Summary &sum, const char *key) {
  for (int i = 0; i < sum.count; i++)
    if (!strcmp(sum.sessions[i].key, key)) return &sum.sessions[i];
  return nullptr;
}

int Tracker::follow(const model::Summary *prev, const model::Summary &next, Event *out, int max) {
  int n = 0;
  auto emit = [&](Kind kind, int index, const Session &s) {
    if (n >= max) return;
    Event &e = out[n++];
    e.kind = kind;
    e.index = index;
    snprintf(e.key, sizeof e.key, "%s", s.key);
    e.outcome = s.outcome;
  };
  for (int i = 0; i < next.count; i++) {
    const Session &s = next.sessions[i];
    const Session *p = prev ? find(*prev, s.key) : nullptr;
    // A new wait, or a new question while still waiting
    if (waiting(s) && !(p && waiting(*p) && waitSince(*p) == waitSince(s))) {
      Wait *w = nullptr;
      for (int k = 0; k < count; k++)
        if (!strcmp(waits[k].key, s.key)) w = &waits[k];
      if (!w && count < model::MAX) w = &waits[count++];
      if (w) {
        snprintf(w->key, sizeof w->key, "%s", s.key);
        w->since = waitSince(s);
        w->reminded = 0;
        w->acked = !prev;
      }
      if (prev) emit(WAIT, i, s);
    }
    // A turn that ended
    if (p && s.endedAt && s.endedAt != p->endedAt && !waiting(s)) emit(END, i, s);
  }
  return n;
}

void Tracker::prune(const model::Summary &next) {
  int keep = 0;
  for (int k = 0; k < count; k++) {
    const Session *s = find(next, waits[k].key);
    if (s && waiting(*s)) waits[keep++] = waits[k];
  }
  count = keep;
}

int Tracker::remind(int64_t wall, Event *out, int max) {
  int n = 0;
  for (int k = 0; k < count; k++) {
    Wait &w = waits[k];
    if (w.acked) continue;
    int64_t due = w.reminded == 0 ? 120000 : 300000 + int64_t(w.reminded - 1) * 600000;
    if (wall - w.since >= due) {
      w.reminded++;
      if (n < max) {
        Event &e = out[n++];
        e.kind = REMIND;
        e.index = -1;
        snprintf(e.key, sizeof e.key, "%s", w.key);
        e.outcome = model::Outcome::None;
      }
    }
  }
  return n;
}

void Tracker::ack() {
  for (int k = 0; k < count; k++) waits[k].acked = true;
}

}  // namespace events
