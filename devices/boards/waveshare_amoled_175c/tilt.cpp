// Gravity along the screen's up-down axis says which way up the board stands.
// Ten reads a second are plenty, each under a millisecond on the bus, so the
// gyroscope stays off and the accelerometer runs in its low-power mode.
//
// The chip's registers by hand, as button.cpp does the power chip's: SensorLib
// 0.5.1 has two classes named SensorQMI8658, and the older header's crashes
// inside begin().

#include "tilt.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

namespace tilt {
namespace {

constexpr uint8_t IMU = 0x6B, WHO_AM_I = 0x00, CTRL1 = 0x02, CTRL2 = 0x03, CTRL7 = 0x08, AX_L = 0x35, RESET = 0x60;
constexpr int UP_AXIS = 0;  // the chip's +x points up the screen
constexpr uint32_t READ_MS = 100;
constexpr uint32_t HOLD_MS = 600;  // long enough not to turn over at a bump, or on the way past
constexpr float CLEAR_G = 0.4f;    // less along the axis is lying flat or on its side
constexpr float G_PER_COUNT = 2.f / 32768;

bool ready, flipped;
uint32_t readAt, agreedAt;
float g[3];

int readReg(uint8_t reg) {
  Wire.beginTransmission(IMU);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(IMU, uint8_t(1)) != 1) return -1;
  return Wire.read();
}

void writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(IMU);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

bool readGravity() {
  Wire.beginTransmission(IMU);
  Wire.write(AX_L);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(IMU, uint8_t(6)) != 6) return false;
  for (float &axis : g) {
    int low = Wire.read();
    axis = int16_t(Wire.read() << 8 | low) * G_PER_COUNT;
  }
  return true;
}

}  // namespace

bool begin() {
  writeReg(RESET, 0xB0);
  delay(20);  // 15 ms at most, and it ignores the bus meanwhile
  if (readReg(WHO_AM_I) != 0x05) return false;
  writeReg(CTRL1, 0x40);  // little-endian, and a read steps through the registers
  writeReg(CTRL2, 0x0D);  // +-2 g, low power at 21 Hz
  writeReg(CTRL7, 0x01);  // accelerometer on, gyroscope off
  ready = true;
  return true;
}

bool upsideDown() {
  uint32_t ms = millis();
  if (!ready || ms - readAt < READ_MS) return flipped;
  readAt = ms;
  if (!readGravity()) return flipped;
  float up = g[UP_AXIS];
  // Only clearly the other way up counts, and it must last
  if (fabsf(up) < CLEAR_G || (up < 0) == flipped) {
    agreedAt = ms;
  } else if (ms - agreedAt >= HOLD_MS) {
    flipped = !flipped;
    agreedAt = ms;
  }
  return flipped;
}

const float *gravity() { return g; }

}  // namespace tilt
