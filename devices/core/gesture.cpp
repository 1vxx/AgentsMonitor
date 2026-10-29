#include "gesture.h"

namespace gesture {

void Finger::down(int px, int py, int64_t ms) {
  downX = x = px, downY = y = py, downAt = ms;
  count = 0;
  add(px, py, ms);
}

void Finger::move(int px, int py, int64_t ms) {
  x = px, y = py;
  add(px, py, ms);
}

void Finger::up(int px, int py, int64_t ms) { move(px, py, ms); }

void Finger::add(int px, int py, int64_t ms) { samples[count++ % 6] = {px, py, ms}; }

void Finger::velocity(float &vx, float &vy) const {
  vx = vy = 0;
  if (count < 2) return;
  const Sample &last = samples[(count - 1) % 6];
  int oldest = count > 6 ? count - 6 : 0, k = count - 2;
  while (k > oldest && last.ms - samples[k % 6].ms < 60) k--;
  const Sample &s = samples[k % 6];
  float dt = (last.ms - s.ms) / 1000.f;
  if (dt > 0) vx = (last.x - s.x) / dt, vy = (last.y - s.y) / dt;
}

}  // namespace gesture
