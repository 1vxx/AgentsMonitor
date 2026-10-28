// Set up as Waveshare's 1.75C examples do: reset on GPIO2, interrupt on GPIO11,
// both axes mirrored to match the panel.

#include "touch.h"

#include <Arduino.h>
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>

namespace touch {
namespace {

constexpr int PIN_SDA = 15, PIN_SCL = 14, PIN_RST = 2, PIN_INT = 11;

TouchDrvCST92xx chip;
bool ready, down;
int lastX, lastY, misses;
volatile bool signalled;

void IRAM_ATTR onInterrupt() { signalled = true; }

}  // namespace

bool begin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  chip.setPins(PIN_RST, PIN_INT);
  if (!chip.begin(Wire, 0x5A, PIN_SDA, PIN_SCL)) return false;
  chip.setMaxCoordinates(466, 466);
  chip.setMirrorXY(true, true);
  attachInterrupt(PIN_INT, onInterrupt, FALLING);
  ready = true;
  return true;
}

Event poll() {
  // While a finger is down, read every frame to see it move and lift
  if (!ready || (!signalled && !down)) return {NONE, 0, 0};
  signalled = false;
  TouchPoints points = chip.getTouchPoints();
  if (points.hasPoints()) {
    const TouchPoint &p = points.getPoint(0);
    Kind kind = down ? MOVE : DOWN;
    misses = 0;
    if (kind == MOVE && p.x == lastX && p.y == lastY) return {NONE, 0, 0};
    down = true, lastX = p.x, lastY = p.y;
    return {kind, lastX, lastY};
  }
  // One empty read can be a glitch mid-touch; two in a row is a lift
  if (!down || ++misses < 2) return {NONE, 0, 0};
  down = false, misses = 0;
  return {UP, lastX, lastY};
}

}  // namespace touch
