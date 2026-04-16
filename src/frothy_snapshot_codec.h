#pragma once

#include "froth_types.h"
#include "frothy_value.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
  size_t payload_length_high_water;
  size_t symbol_count_high_water;
  size_t object_count_high_water;
  size_t binding_count_high_water;
} frothy_snapshot_codec_usage_t;

uint8_t *frothy_snapshot_codec_payload_buffer(size_t *capacity_out);
froth_error_t frothy_snapshot_codec_write_payload(
    const frothy_runtime_t *runtime, const uint8_t **payload_out,
    uint32_t *payload_length_out);
froth_error_t frothy_snapshot_codec_validate_payload(const uint8_t *payload,
                                                     size_t payload_length);
froth_error_t frothy_snapshot_codec_restore_payload(const uint8_t *payload,
                                                    uint32_t payload_length);
void frothy_snapshot_codec_get_usage(frothy_snapshot_codec_usage_t *out);
void frothy_snapshot_codec_debug_reset_usage(void);

void frothy_snapshot_test_set_error_after_objects(froth_error_t err);
