#pragma once

#include <stddef.h>
#include <stdint.h>

// Photos for themes that show them, decoded to the panel's pixels. The core
// has no decoder of its own: a board that can decode sets one at start-up,
// and without it decoding fails and a theme draws without its photos.
namespace jpeg {

// Decodes a baseline JPEG of exactly w x h pixels into out: w x h big-endian
// RGB565 pixels, starting on a 16-byte boundary. False if it can't.
extern bool (*decode)(const uint8_t *jpg, size_t len, uint16_t *out, int w, int h);

}  // namespace jpeg
