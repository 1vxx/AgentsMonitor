#pragma once

// The 1.75C's two side buttons: PWR (top right), read through the AXP2101
// power chip, presses for the UI; BOOT (bottom right) on GPIO0, held down,
// summons.
namespace button {

enum Event { NONE, PRESS, HOLD };

// After touch::begin, which starts the I2C bus the power chip is on
bool begin();
// PRESS when PWR was pressed and let go since the last call; HOLD once BOOT
// has been down 3 s, while it still is. Call once a frame.
Event poll();

}  // namespace button
