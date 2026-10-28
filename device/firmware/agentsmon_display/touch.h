#pragma once

#include <stdint.h>

// The 1.75C's CST9217 touch controller, read as one finger: down, moves, up.
namespace touch {

enum Kind { NONE, DOWN, MOVE, UP };

struct Event {
  Kind kind;
  int x, y;
};

bool begin();
// Reads the controller when it has something new; call once a frame
Event poll();

}  // namespace touch
