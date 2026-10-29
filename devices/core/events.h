#pragma once

#include <stdint.h>

#include "model.h"

// What happened, read off the difference between two summaries: a session
// that starts waiting for you, a turn that ends, and the reminders for a wait
// nobody answers (at 2 and 5 minutes, then every 10). Display programs turn
// these into chimes and motion of their own.
namespace events {

enum Kind : uint8_t { WAIT, END, REMIND };

struct Event {
  Kind kind;
  int index;  // the session's place in the summary just followed; -1 for REMIND
  char key[96];
  model::Outcome outcome;  // for END
};

struct Wait {
  char key[96];
  int64_t since;
  int reminded;
  bool acked;
};

// Waiting, unless the session has gone quiet
inline bool waiting(const model::Session &s) { return model::waiting(s.status) && !s.stale; }
inline int64_t waitSince(const model::Session &s) { return s.waitSince ? s.waitSince : s.since; }
const model::Session *find(const model::Summary &sum, const char *key);

struct Tracker {
  Wait waits[model::MAX];
  int count = 0;

  // The events between prev (null for the first summary) and next, in the
  // order of next's sessions; returns how many. Waits found in the first
  // summary are kept, but neither call out nor remind.
  int follow(const model::Summary *prev, const model::Summary &next, Event *out, int max);
  // Drops the waits that next no longer has; call once next is current
  void prune(const model::Summary &next);
  // The reminders due by `wall` (epoch ms); returns how many
  int remind(int64_t wall, Event *out, int max);
  // Someone looked: no more reminders for the waits there are now
  void ack();
  void clear() { count = 0; }
};

}  // namespace events
