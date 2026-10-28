#pragma once

#include <math.h>

// A damped spring, the one motion everything on the display shares: it eases
// in and out, carries the speed it had when its target moves, and with less
// damping than critical (2 * sqrt(stiffness)) settles with a small overshoot.
struct Spring {
  float x = 0, v = 0;

  void step(float target, float dt, float stiffness = 170, float damping = 24) {
    // Small fixed steps keep a stiff spring stable at any frame rate
    while (dt > 0) {
      float h = dt > 1 / 240.f ? 1 / 240.f : dt;
      v += (stiffness * (target - x) - damping * v) * h;
      x += v * h;
      dt -= h;
    }
  }

  bool settled(float target) const { return fabsf(x - target) < 0.002f && fabsf(v) < 0.02f; }
};
