#include "jpeg.h"

namespace jpeg {

bool (*decode)(const uint8_t *jpg, size_t len, uint16_t *out, int w, int h);

}  // namespace jpeg
