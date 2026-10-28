#pragma once

#include <stddef.h>

// Wi-Fi, and the daemon's summary fetched once a second on a task of its own
namespace net {

enum State { CONNECTING, ONLINE, LOST };

void begin();
State state();
// Copies the newest summary into out if one came since the last call
bool take(char *out, size_t len);

}  // namespace net
