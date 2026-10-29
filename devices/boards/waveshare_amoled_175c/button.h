#pragma once

// The 1.75C's two side buttons, either of them a press for the UI: PWR (top
// right), read through the AXP2101 power chip, and BOOT (bottom right) on
// GPIO0.
namespace button {

// After touch::begin, which starts the I2C bus the power chip is on
bool begin();
// Whether a button was pressed and let go since the last call; call once a frame
bool poll();

}  // namespace button
