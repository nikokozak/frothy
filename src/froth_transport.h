#pragma once
#include "froth_types.h"
#include <stdint.h>

#ifndef FROTH_LINK_MAX_PAYLOAD
#define FROTH_LINK_MAX_PAYLOAD 256
#endif

#define FROTH_LINK_HEADER_SIZE 20
#define FROTH_LINK_MAGIC_0 'F'
#define FROTH_LINK_MAGIC_1 'L'
#define FROTH_LINK_VERSION 2

/* Raw frame = byte header + payload. COBS adds at most 1 byte per 254. */
#define FROTH_LINK_MAX_FRAME (FROTH_LINK_HEADER_SIZE + FROTH_LINK_MAX_PAYLOAD)
#define FROTH_LINK_COBS_MAX                                                    \
  (FROTH_LINK_MAX_FRAME + (FROTH_LINK_MAX_FRAME / 254) + 1)

/* Message types (V2) */
#define FROTH_LINK_HELLO_REQ 0x01
#define FROTH_LINK_HELLO_RES 0x02
#define FROTH_LINK_ATTACH_REQ 0x03
#define FROTH_LINK_ATTACH_RES 0x04
#define FROTH_LINK_DETACH_REQ 0x05
#define FROTH_LINK_DETACH_RES 0x06
#define FROTH_LINK_INFO_REQ 0x07
#define FROTH_LINK_INFO_RES 0x08
#define FROTH_LINK_RESET_REQ 0x09
#define FROTH_LINK_RESET_RES 0x0A
#define FROTH_LINK_EVAL_REQ 0x0B
#define FROTH_LINK_EVAL_RES 0x0C
#define FROTH_LINK_INTERRUPT_REQ 0x0D
#define FROTH_LINK_KEEPALIVE 0x0E
#define FROTH_LINK_INPUT_DATA 0x0F
#define FROTH_LINK_INPUT_WAIT 0x10
#define FROTH_LINK_OUTPUT_DATA 0x11
#define FROTH_LINK_ERROR 0xFF

typedef struct {
  uint8_t magic[2];
  uint8_t version;
  uint8_t message_type;
  uint64_t session_id;
  uint16_t seq;
  uint16_t payload_length;
  uint32_t crc32;
} froth_link_header_t;

/* ── COBS codec ──────────────────────────────────────────────────────
 * Encode/decode support in-place operation (out == in) for decode only.
 * Encode output is always larger than input; decode output is always
 * smaller or equal.                                                   */

froth_error_t froth_cobs_encode(const uint8_t *in, uint16_t in_len,
                                uint8_t *out, uint16_t out_cap,
                                uint16_t *out_len);

froth_error_t froth_cobs_decode(const uint8_t *in, uint16_t in_len,
                                uint8_t *out, uint16_t out_cap,
                                uint16_t *out_len);

/* ── Frame header ────────────────────────────────────────────────────
 * parse validates magic, version, payload cap, and CRC.
 * On success, *payload points into the frame buffer at offset 20.
 * build serializes header + payload and computes CRC.                 */

froth_error_t froth_link_header_parse(const uint8_t *frame, uint16_t frame_len,
                                      froth_link_header_t *header,
                                      const uint8_t **payload);

froth_error_t froth_link_header_build(uint64_t session_id, uint8_t message_type,
                                      uint16_t seq, const uint8_t *payload,
                                      uint16_t payload_len, uint8_t *out,
                                      uint16_t out_cap, uint16_t *out_len);

/* ── Full frame send ─────────────────────────────────────────────────
 * Builds header, COBS-encodes, emits 0x00 + encoded + 0x00 via
 * platform_emit. Uses a static internal buffer (one frame at a time). */

froth_error_t froth_link_send_frame(uint64_t session_id, uint8_t message_type,
                                    uint16_t seq, const uint8_t *payload,
                                    uint16_t payload_len);

/* ── Inbound frame accumulation ─────────────────────────────────────
 * frame_reset/frame_byte accumulate bytes between 0x00 delimiters.
 * frame_decode does COBS decode + header parse (no dispatch).
 * frame_complete does decode + dispatch (all-in-one convenience).   */

void froth_link_frame_reset(void);
froth_error_t froth_link_frame_byte(uint8_t byte);
froth_error_t froth_link_frame_decode(froth_link_header_t *header,
                                      const uint8_t **payload);
froth_error_t froth_link_frame_complete(froth_vm_t *vm);
