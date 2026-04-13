#include "froth_snapshot.h"
#include "froth_crc32.h"
#include "froth_types.h"
#include "platform.h"
#include <stdint.h>
#include <string.h>

static void write_le16(uint8_t *buf, uint16_t val) {
  buf[0] = val & 0xFF;
  buf[1] = (val >> 8) & 0xFF;
}

static void write_le32(uint8_t *buf, uint32_t val) {
  buf[0] = val & 0xFF;
  buf[1] = (val >> 8) & 0xFF;
  buf[2] = (val >> 16) & 0xFF;
  buf[3] = (val >> 24) & 0xFF;
}

static uint16_t read_le16(const uint8_t *buf) {
  return ((uint16_t)buf[0]) | ((uint16_t)buf[1] << 8);
}

static uint32_t read_le32(const uint8_t *buf) {
  return ((uint32_t)buf[0]) | ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

uint32_t froth_snapshot_abi_hash(void) {
  uint8_t arr[4];
  arr[0] = FROTH_CELL_SIZE_BITS;
  arr[1] = 0; /* endian: LE */
  arr[2] = FROTH_SNAPSHOT_VERSION & 0xFF;
  arr[3] = (FROTH_SNAPSHOT_VERSION >> 8) & 0xFF;
  return froth_crc32(arr, 4);
}

froth_error_t froth_snapshot_build_header(uint8_t *header, uint32_t payload_len,
                                          const uint8_t *payload,
                                          uint32_t generation) {
  memset(header, 0, FROTH_SNAPSHOT_HEADER_SIZE);
  memcpy(&header[FROTH_SNAPSHOT_MAGIC_OFFSET], FROTH_SNAPSHOT_MAGIC, 8);
  write_le16(&header[FROTH_SNAPSHOT_VERSION_OFFSET], FROTH_SNAPSHOT_VERSION);
  write_le16(&header[FROTH_SNAPSHOT_FLAGS_OFFSET], 0);
  header[FROTH_SNAPSHOT_CELL_BITS_OFFSET] = FROTH_CELL_SIZE_BITS;
  header[FROTH_SNAPSHOT_ENDIAN_OFFSET] = 0;
  write_le32(&header[FROTH_SNAPSHOT_ABI_HASH_OFFSET],
             froth_snapshot_abi_hash());
  write_le32(&header[FROTH_SNAPSHOT_GENERATION_OFFSET], generation);
  write_le32(&header[FROTH_SNAPSHOT_PAYLOAD_LEN_OFFSET], payload_len);
  write_le32(&header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET],
             froth_crc32(payload, payload_len));
  /* header_crc32 field is zero during its own computation (already zeroed by memset) */
  write_le32(&header[FROTH_SNAPSHOT_HEADER_CRC32_OFFSET],
             froth_crc32(header, FROTH_SNAPSHOT_HEADER_SIZE));
  return FROTH_OK;
}

froth_error_t
froth_snapshot_parse_header(const uint8_t *header,
                            froth_snapshot_header_info_t *parse_out) {
  /* 1. Magic check */
  if (memcmp(&header[FROTH_SNAPSHOT_MAGIC_OFFSET], FROTH_SNAPSHOT_MAGIC, 8) !=
      0) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  /* 2. Header CRC — must zero the crc field for computation, so copy */
  uint8_t tmp[FROTH_SNAPSHOT_HEADER_SIZE];
  memcpy(tmp, header, FROTH_SNAPSHOT_HEADER_SIZE);
  uint32_t stored_hdr_crc = read_le32(&tmp[FROTH_SNAPSHOT_HEADER_CRC32_OFFSET]);
  memset(&tmp[FROTH_SNAPSHOT_HEADER_CRC32_OFFSET], 0, 4);
  if (froth_crc32(tmp, FROTH_SNAPSHOT_HEADER_SIZE) != stored_hdr_crc) {
    return FROTH_ERROR_SNAPSHOT_BAD_CRC;
  }

  /* 3. Format version */
  if (read_le16(&header[FROTH_SNAPSHOT_VERSION_OFFSET]) !=
      FROTH_SNAPSHOT_VERSION) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  /* 4. ABI compatibility */
  if (read_le32(&header[FROTH_SNAPSHOT_ABI_HASH_OFFSET]) !=
      froth_snapshot_abi_hash()) {
    return FROTH_ERROR_SNAPSHOT_INCOMPAT;
  }

  /* 5. Extract fields */
  parse_out->payload_len =
      read_le32(&header[FROTH_SNAPSHOT_PAYLOAD_LEN_OFFSET]);
  parse_out->generation =
      read_le32(&header[FROTH_SNAPSHOT_GENERATION_OFFSET]);
  parse_out->flags = read_le16(&header[FROTH_SNAPSHOT_FLAGS_OFFSET]);

  return FROTH_OK;
}

#ifdef FROTH_HAS_SNAPSHOTS

/* Read and validate one slot's header. Returns FROTH_OK + fills info,
 * or an error if the slot is empty/corrupt/incompatible. */
static froth_error_t read_slot_header(uint8_t slot,
                                      froth_snapshot_header_info_t *info) {
  uint8_t hdr[FROTH_SNAPSHOT_HEADER_SIZE];
  froth_error_t err = platform_snapshot_read(slot, 0, hdr,
                                             FROTH_SNAPSHOT_HEADER_SIZE);
  if (err != FROTH_OK) return err;
  return froth_snapshot_parse_header(hdr, info);
}

froth_error_t froth_snapshot_pick_active(uint8_t *slot_out,
                                         uint32_t *generation_out) {
  froth_snapshot_header_info_t info_a, info_b;
  froth_error_t err_a = read_slot_header(0, &info_a);
  froth_error_t err_b = read_slot_header(1, &info_b);

  int a_valid = (err_a == FROTH_OK);
  int b_valid = (err_b == FROTH_OK);

  if (!a_valid && !b_valid) {
    return FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT;
  }

  if (a_valid && !b_valid) {
    *slot_out = 0;
    *generation_out = info_a.generation;
  } else if (!a_valid && b_valid) {
    *slot_out = 1;
    *generation_out = info_b.generation;
  } else {
    /* both valid — higher generation wins */
    if (info_a.generation >= info_b.generation) {
      *slot_out = 0;
      *generation_out = info_a.generation;
    } else {
      *slot_out = 1;
      *generation_out = info_b.generation;
    }
  }

  return FROTH_OK;
}

froth_error_t froth_snapshot_pick_inactive(uint8_t *slot_out,
                                           uint32_t *next_generation_out) {
  froth_snapshot_header_info_t info_a, info_b;
  froth_error_t err_a = read_slot_header(0, &info_a);
  froth_error_t err_b = read_slot_header(1, &info_b);

  int a_valid = (err_a == FROTH_OK);
  int b_valid = (err_b == FROTH_OK);

  if (!a_valid && !b_valid) {
    /* first save ever */
    *slot_out = 0;
    *next_generation_out = 1;
  } else if (a_valid && !b_valid) {
    /* A is active, write to B */
    *slot_out = 1;
    *next_generation_out = info_a.generation + 1;
  } else if (!a_valid && b_valid) {
    /* B is active, write to A */
    *slot_out = 0;
    *next_generation_out = info_b.generation + 1;
  } else {
    /* both valid — write to the one with lower generation (the stale one) */
    if (info_a.generation <= info_b.generation) {
      *slot_out = 0;
      *next_generation_out = info_b.generation + 1;
    } else {
      *slot_out = 1;
      *next_generation_out = info_a.generation + 1;
    }
  }

  return FROTH_OK;
}

#endif /* FROTH_HAS_SNAPSHOTS */
