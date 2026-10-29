#pragma once

#include <stdint.h>

// One finger on the screen, read the same way by every theme: where it went
// down, where it is, how fast it moves, and whether it tapped or is dragging.
// What a drag does is the theme's own business.
namespace gesture {

// A finger that has moved this far from where it went down is dragging
constexpr int DRAG_PX = 10;
// A touch that lifts less than this far from where it went down, and this
// soon, is a tap
constexpr int TAP_PX = 14;
constexpr int64_t TAP_MS = 600;

struct Finger {
  int downX = 0, downY = 0;  // where it went down
  int x = 0, y = 0;          // where it is
  int64_t downAt = 0;

  void down(int x, int y, int64_t ms);
  void move(int x, int y, int64_t ms);
  void up(int x, int y, int64_t ms);

  int dx() const { return x - downX; }
  int dy() const { return y - downY; }
  bool dragging() const { return dx() * dx() + dy() * dy() > DRAG_PX * DRAG_PX; }
  // Lifted at `ms`: close to where it went down, and soon enough
  bool tapped(int64_t ms) const { return dx() * dx() + dy() * dy() < TAP_PX * TAP_PX && ms - downAt < TAP_MS; }
  // Pixels per second over about the last 60 ms; 0 with too little to go on
  void velocity(float &vx, float &vy) const;

 private:
  struct Sample {
    int x, y;
    int64_t ms;
  } samples[6] = {};
  int count = 0;
  void add(int x, int y, int64_t ms);
};

}  // namespace gesture
