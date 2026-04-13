#include "froth_crc32.h"

uint32_t froth_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xEDB88320;
      else
        crc >>= 1;
    }
  }
  return crc;
}

uint32_t froth_crc32(const uint8_t *data, size_t len) {
  return froth_crc32_update(0xFFFFFFFF, data, len) ^ 0xFFFFFFFF;
}
