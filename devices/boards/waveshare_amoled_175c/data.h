#pragma once

#include <stddef.h>
#include <stdint.h>

// The theme's data, kept in the flash's data partition (build.sh --pack writes
// it there), and the JPEG decoder for the photos in it.
namespace data {

// Finds the theme's data in the data partition and says how long it is. It
// must start with a 4-byte tag and its length as a little-endian u32, as a
// photo pack does; false when the partition holds no such thing.
bool open(size_t &len);

// Reads n bytes from `at` in it. The partition runs past the first 16 MB of
// flash, beyond what this core's cache can map, so the data is read rather
// than mapped.
bool read(size_t at, void *out, size_t n);

// jpeg::decode on this board: Espressif's esp_new_jpeg, with the S3's SIMD
bool decode(const uint8_t *jpg, size_t len, uint16_t *out, int w, int h);

}  // namespace data
