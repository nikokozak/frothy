#pragma once
#include <stddef.h>
#include <stdint.h>

/* IEEE 802.3 CRC32, bitwise (no lookup table). */
uint32_t froth_crc32(const uint8_t *data, size_t len);

/* Incremental CRC32. Start with crc=0xFFFFFFFF, feed chunks,
 * then XOR final result with 0xFFFFFFFF. */
uint32_t froth_crc32_update(uint32_t crc, const uint8_t *data, size_t len);
