// PWR goes to the AXP2101's PWRON pin, not to the ESP32: the chip raises a
// short-press interrupt when it is let go, which is read here off the I2C bus
// rather than its IRQ line. Held for seconds, PWR still powers the board off.

#include "button.h"

#include <Arduino.h>
#include <Wire.h>

namespace button {
namespace {

constexpr uint8_t AXP = 0x34, CHIP_ID = 0x03, INTEN2 = 0x41, INTSTS2 = 0x49;
constexpr uint8_t SHORT_PRESS = 1 << 3;  // in INTEN2 and INTSTS2
constexpr int PIN_BOOT = 0;
constexpr uint32_t READ_MS = 50, BOUNCE_MS = 30;

bool pmu;
bool bootDown;
uint32_t readAt, bootAt;

int readReg(uint8_t reg) {
  Wire.beginTransmission(AXP);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(AXP, uint8_t(1)) != 1) return -1;
  return Wire.read();
}

void writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(AXP);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

}  // namespace

bool begin() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  int inten = readReg(CHIP_ID) == 0x4A ? readReg(INTEN2) : -1;
  if (inten < 0) return false;
  writeReg(INTEN2, uint8_t(inten) | SHORT_PRESS);
  writeReg(INTSTS2, SHORT_PRESS);  // forget one from before start-up
  pmu = true;
  return true;
}

bool poll() {
  uint32_t ms = millis();
  bool pressed = false;
  // BOOT: let go after it has been down past the bounce
  bool down = digitalRead(PIN_BOOT) == LOW;
  if (down && !bootDown) bootAt = ms;
  if (!down && bootDown && ms - bootAt >= BOUNCE_MS) pressed = true;
  bootDown = down;
  if (pmu && ms - readAt >= READ_MS) {
    readAt = ms;
    int sts = readReg(INTSTS2);
    if (sts > 0 && (sts & SHORT_PRESS)) {
      writeReg(INTSTS2, SHORT_PRESS);
      pressed = true;
    }
  }
  return pressed;
}

}  // namespace button
