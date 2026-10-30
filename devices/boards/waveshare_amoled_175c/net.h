#pragma once

#include <stddef.h>
#include <stdint.h>

// Wi-Fi, and the daemon's summary fetched once a second on a task of its own
namespace net {

enum State { CONNECTING, ONLINE, LOST };

void begin();
State state();
// Copies the newest summary into out if one came since the last call
bool take(char *out, size_t len);
// Tells the daemon the person saw this session's turn that ended at endedAt,
// before the next poll; dropped if the daemon can't be reached
void seen(const char *key, int64_t endedAt);

}  // namespace net
