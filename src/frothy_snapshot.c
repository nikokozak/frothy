#include "frothy_snapshot.h"

#include "froth_crc32.h"
#include "froth_snapshot.h"
#include "froth_vm.h"
#include "frothy_base_image.h"
#include "frothy_snapshot_codec.h"
#include "platform.h"

#include <stdint.h>
#include <stdlib.h>

static frothy_runtime_t *frothy_runtime(void) {
  return &froth_vm.frothy_runtime;
}

static froth_error_t frothy_snapshot_reset_with_error(froth_error_t err) {
  froth_error_t reset_err = frothy_base_image_reset();
  return reset_err != FROTH_OK ? reset_err : err;
}

static uint32_t frothy_snapshot_stored_crc32(const uint8_t *header) {
  return ((uint32_t)header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET]) |
         ((uint32_t)header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET + 1] << 8) |
         ((uint32_t)header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET + 2] << 16) |
         ((uint32_t)header[FROTH_SNAPSHOT_PAYLOAD_CRC32_OFFSET + 3] << 24);
}

froth_error_t frothy_snapshot_save(void) {
#ifndef FROTH_HAS_SNAPSHOTS
  return FROTH_ERROR_IO;
#else
  uint8_t *payload = NULL;
  uint8_t header[FROTH_SNAPSHOT_HEADER_SIZE];
  uint8_t slot = 0;
  uint32_t generation = 0;
  uint32_t payload_length = 0;
  froth_error_t err;

  err = frothy_snapshot_codec_write_payload(frothy_runtime(), &payload,
                                            &payload_length);
  if (err == FROTH_OK) {
    err = froth_snapshot_pick_inactive(&slot, &generation);
  }
  if (err == FROTH_OK) {
    err = platform_snapshot_write(slot, FROTH_SNAPSHOT_HEADER_SIZE, payload,
                                  payload_length);
  }
  if (err == FROTH_OK) {
    err = froth_snapshot_build_header(header, payload_length, payload,
                                      generation);
  }
  if (err == FROTH_OK) {
    err = platform_snapshot_write(slot, 0, header, sizeof(header));
  }

  free(payload);
  return err;
#endif
}

froth_error_t frothy_snapshot_restore(void) {
#ifndef FROTH_HAS_SNAPSHOTS
  return FROTH_ERROR_IO;
#else
  uint8_t slot = 0;
  uint32_t generation = 0;
  uint8_t header[FROTH_SNAPSHOT_HEADER_SIZE];
  froth_snapshot_header_info_t info;
  uint8_t *payload = NULL;
  froth_error_t err;

  err = froth_snapshot_pick_active(&slot, &generation);
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }

  err = platform_snapshot_read(slot, 0, header, sizeof(header));
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }

  err = froth_snapshot_parse_header(header, &info);
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }
  if (info.payload_len > FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES) {
    return frothy_snapshot_reset_with_error(FROTH_ERROR_SNAPSHOT_OVERFLOW);
  }

  payload = (uint8_t *)malloc(info.payload_len);
  if (info.payload_len > 0 && payload == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  err = platform_snapshot_read(slot, FROTH_SNAPSHOT_HEADER_SIZE, payload,
                               info.payload_len);
  if (err == FROTH_OK &&
      froth_crc32(payload, info.payload_len) !=
          frothy_snapshot_stored_crc32(header)) {
    err = FROTH_ERROR_SNAPSHOT_BAD_CRC;
  }
  if (err == FROTH_OK) {
    err = frothy_snapshot_codec_validate_payload(payload, info.payload_len);
  }
  if (err != FROTH_OK) {
    free(payload);
    return frothy_snapshot_reset_with_error(err);
  }

  err = frothy_base_image_reset();
  if (err != FROTH_OK) {
    free(payload);
    return err;
  }

  err = frothy_snapshot_codec_restore_payload(payload, info.payload_len);
  free(payload);
  if (err != FROTH_OK) {
    return frothy_snapshot_reset_with_error(err);
  }

  return FROTH_OK;
#endif
}

froth_error_t frothy_snapshot_wipe(void) {
#ifndef FROTH_HAS_SNAPSHOTS
  return frothy_base_image_reset();
#else
  FROTH_TRY(platform_snapshot_erase(0));
  FROTH_TRY(platform_snapshot_erase(1));
  return frothy_base_image_reset();
#endif
}

froth_error_t frothy_builtin_save(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  if (arg_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_snapshot_save());
  *out = frothy_value_make_nil();
  return FROTH_OK;
}

froth_error_t frothy_builtin_restore(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  if (arg_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_snapshot_restore());
  *out = frothy_value_make_nil();
  return FROTH_OK;
}

froth_error_t frothy_builtin_wipe(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  if (arg_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_snapshot_wipe());
  *out = frothy_value_make_nil();
  return FROTH_OK;
}
