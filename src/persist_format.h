#pragma once

#include "types.h"

#include <stdbool.h>

#if FR_FEATURE_PERSISTENCE

enum {
  FR_PERSIST_HEADER_BYTES = 32,
  FR_PERSIST_PROFILE_HASH_OFFSET = 8,
  FR_PERSIST_BACKEND_GENERATION_OFFSET = 12,
  FR_PERSIST_PAYLOAD_LENGTH_OFFSET = 16,
  FR_PERSIST_PAYLOAD_CRC_OFFSET = 20,
  FR_PERSIST_HEADER_CRC_OFFSET = 24,
};

#ifndef FR_PROFILE_PERSIST_STORAGE_BYTES
#define FR_PROFILE_PERSIST_STORAGE_BYTES FR_PROFILE_PERSISTENCE_BYTES
#endif

enum {
  FR_PERSIST_STORAGE_BYTES = FR_PROFILE_PERSIST_STORAGE_BYTES,
  FR_PERSIST_PAYLOAD_BYTES =
      FR_PROFILE_PERSIST_STORAGE_BYTES - FR_PERSIST_HEADER_BYTES,
};

typedef struct fr_persist_format_info_t {
  uint32_t backend_generation;
  uint32_t payload_length;
  uint32_t total_length;
  uint32_t payload_crc;
} fr_persist_format_info_t;

fr_err_t fr_persist_format_build_header(uint8_t *bytes,
                                        uint32_t payload_length,
                                        uint32_t payload_crc);
fr_err_t fr_persist_format_read_header(const uint8_t *bytes,
                                       fr_persist_format_info_t *out);
fr_err_t fr_persist_format_validate_header_payload_crc(
    const uint8_t *bytes, uint32_t payload_crc,
    fr_persist_format_info_t *out);
fr_err_t fr_persist_format_validate(const uint8_t *bytes,
                                    uint16_t stored_length,
                                    fr_persist_format_info_t *out);
fr_err_t fr_persist_format_stamp_generation(uint8_t *bytes,
                                            uint32_t backend_generation);

enum { FR_PERSIST_NO_MOUNTED_SLOT = 0xff };

/* The slot of two that a mount or a read uses (ADR 0076). err holds the
 * check of each slot: FR_OK, FR_ERR_NOT_FOUND for an empty slot, or the
 * error of the check. Image 0 is the valid slot with the higher generation,
 * or slot 0 when the generations are equal. Image 1 is the other valid
 * slot. With no valid slot, a damaged slot gives FR_ERR_CORRUPT.
 * Else a slot of another release gives FR_ERR_OTHER_RELEASE, else the
 * answer is FR_ERR_NOT_FOUND. */
fr_err_t
fr_persist_format_pick_read_slot(const fr_err_t err[2],
                                 const fr_persist_format_info_t info[2],
                                 uint8_t image_index, uint8_t *out_slot);

/* The slot of two that a save writes, and its generation. The save never
 * writes the slot that the runtime is mounted on, because it reads the kept
 * records from it. With no image mounted, an empty or invalid slot comes
 * first, then the older one. The generation is above every valid envelope. */
void fr_persist_format_pick_commit_slot(const bool valid[2],
                                        const fr_persist_format_info_t info[2],
                                        uint8_t mounted_slot, uint8_t *out_slot,
                                        uint32_t *out_generation);

#endif
