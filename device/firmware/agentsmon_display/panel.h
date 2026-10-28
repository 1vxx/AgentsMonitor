#pragma once

#include <stddef.h>
#include <stdint.h>

// The 1.75C's CO5300 AMOLED over QSPI, driven straight through ESP-IDF's SPI
// master. Frames arrive a band of rows at a time, each sent by DMA from the
// internal RAM it was drawn in while the next band is drawn.
namespace panel {

// Resets and initialises the panel, the whole width as the column window
bool begin();
void brightness(uint8_t level);
// Starts sending `rows` rows of big-endian RGB565 from band (internal,
// DMA-capable memory) to rows y0 on; returns at once. `k` names the band's
// buffer. y0 and rows must be even.
void send(int k, const uint16_t *band, int y0, int rows);
// Returns once band buffer k is no longer being sent
void wait(int k);

}  // namespace panel
