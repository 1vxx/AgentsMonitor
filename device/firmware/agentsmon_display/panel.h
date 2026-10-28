#pragma once

#include <stddef.h>
#include <stdint.h>

// The 1.75C's CO5300 AMOLED over QSPI, driven straight through ESP-IDF's SPI
// master. Frames arrive a band of rows at a time, each queued and sent by DMA
// from the internal RAM it was drawn in while more bands are drawn.
namespace panel {

// Resets and initialises the panel, the whole width as the column window
bool begin();
void brightness(uint8_t level);
// Queues `rows` rows of big-endian RGB565 from band (internal, DMA-capable
// memory) for rows y0 on, and returns at once; the band must hold still
// until sent() counts it. y0 and rows must be even, the band at most 32 KB.
// One task at a time.
void send(const uint16_t *band, int y0, int rows);
// How many bands have gone out since begin()
uint32_t sent();

}  // namespace panel
