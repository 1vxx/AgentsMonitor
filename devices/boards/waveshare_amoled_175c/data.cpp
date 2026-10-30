#include "data.h"

#include <esp_jpeg_dec.h>
#include <esp_partition.h>
#include <string.h>

namespace data {

namespace {
const esp_partition_t *part;
size_t total;
}  // namespace

bool open(size_t &len) {
  part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, nullptr);
  uint8_t head[8];
  if (!part || esp_partition_read(part, 0, head, sizeof head) != ESP_OK) return false;
  uint32_t n;
  memcpy(&n, head + 4, 4);
  // Erased flash reads as 0xFF; a tag of letters and digits says something was written
  for (int i = 0; i < 4; i++)
    if (!((head[i] >= 'A' && head[i] <= 'Z') || (head[i] >= '0' && head[i] <= '9'))) return false;
  if (n < sizeof head || n > part->size) return false;
  len = total = n;
  return true;
}

bool read(size_t at, void *out, size_t n) {
  if (!part || at > total || n > total - at) return false;
  return esp_partition_read(part, at, out, n) == ESP_OK;
}

bool decode(const uint8_t *jpg, size_t len, uint16_t *out, int w, int h) {
  // One decoder for every photo, opened on the first
  static jpeg_dec_handle_t dec;
  if (!dec) {
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
    if (jpeg_dec_open(&config, &dec) != JPEG_ERR_OK) {
      dec = nullptr;
      return false;
    }
  }
  jpeg_dec_io_t io = {};
  io.inbuf = (uint8_t *)jpg;
  io.inbuf_len = int(len);
  jpeg_dec_header_info_t info;
  if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK || info.width != w || info.height != h) return false;
  io.outbuf = (uint8_t *)out;
  return jpeg_dec_process(dec, &io) == JPEG_ERR_OK;
}

}  // namespace data
