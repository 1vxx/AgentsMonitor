#pragma once

// Which way up the 1.75C stands, from the accelerometer in its QMI8658.
namespace tilt {

// After touch::begin, which starts the I2C bus the chip is on
bool begin();
// Whether the board stands upside down. Lying flat or on its side it stays as
// it was. Call once a frame; it reads the chip a few times a second.
bool upsideDown();
// The last reading in g, x, y and z along the chip's axes, for the logs
const float *gravity();

}  // namespace tilt
