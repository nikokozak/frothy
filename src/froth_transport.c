#include "froth_transport.h"
#include "froth_crc32.h"
#include "froth_link.h"
#include "platform.h"
#include <stdbool.h>

/* ── COBS encode ─────────────────────────────────────────────────────
 * Walks input, grouping runs of non-zero bytes. Each group is prefixed
 * with a code byte = (run_length + 1). A zero byte in the input ends
 * the current group. Runs longer than 254 bytes are split (code 0xFF
 * means 254 data bytes follow with no implicit trailing zero).        */

froth_error_t froth_cobs_encode(const uint8_t *in, uint16_t in_len,
                                uint8_t *out, uint16_t out_cap,
                                uint16_t *out_len) {
  uint16_t rp = 0;  /* read position in input */
  uint16_t wp = 1;  /* write position in output (0 reserved for first code) */
  uint16_t cp = 0;  /* position of current code byte in output */
  uint8_t code = 1; /* distance to next zero (1 = zero immediately follows) */

  while (rp < in_len) {
    if (in[rp] == 0) {
      out[cp] = code;
      cp = wp;
      if (wp >= out_cap)
        return FROTH_ERROR_LINK_OVERFLOW;
      wp++;
      code = 1;
      rp++;
    } else {
      if (wp >= out_cap)
        return FROTH_ERROR_LINK_OVERFLOW;
      out[wp] = in[rp];
      wp++;
      code++;
      rp++;

      if (code == 0xFF) {
        out[cp] = code;
        cp = wp;
        if (wp >= out_cap)
          return FROTH_ERROR_LINK_OVERFLOW;
        wp++;
        code = 1;
      }
    }
  }

  out[cp] = code;
  *out_len = wp;
  return FROTH_OK;
}

/* ── COBS decode ─────────────────────────────────────────────────────
 * Reads code bytes to determine group lengths. Each group contributes
 * (code - 1) data bytes. If code < 0xFF and more data remains, an
 * implicit zero byte is appended. Safe for in-place use (out == in)
 * because the write pointer never passes the read pointer.            */

froth_error_t froth_cobs_decode(const uint8_t *in, uint16_t in_len,
                                uint8_t *out, uint16_t out_cap,
                                uint16_t *out_len) {
  uint16_t rp = 0;
  uint16_t wp = 0;

  while (rp < in_len) {
    uint8_t code = in[rp++];

    if (code == 0)
      return FROTH_ERROR_LINK_COBS_DECODE;

    uint8_t count = code - 1;

    if (rp + count > in_len)
      return FROTH_ERROR_LINK_COBS_DECODE;

    for (uint8_t i = 0; i < count; i++) {
      if (wp >= out_cap)
        return FROTH_ERROR_LINK_OVERFLOW;
      out[wp++] = in[rp++];
    }

    if (code < 0xFF && rp < in_len) {
      if (wp >= out_cap)
        return FROTH_ERROR_LINK_OVERFLOW;
      out[wp++] = 0x00;
    }
  }

  *out_len = wp;
  return FROTH_OK;
}

/* ── LE byte helpers ─────────────────────────────────────────────────*/

static uint16_t read_u16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}

static void write_u32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

static uint64_t read_u64(const uint8_t *p) {
  return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
         ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) |
         ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) |
         ((uint64_t)p[7] << 56);
}

static void write_u64(uint8_t *p, uint64_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
  p[4] = (v >> 32) & 0xFF;
  p[5] = (v >> 40) & 0xFF;
  p[6] = (v >> 48) & 0xFF;
  p[7] = (v >> 56) & 0xFF;
}

/* ── CRC over header[0..15] + payload ────────────────────────────── */

static uint32_t link_crc(const uint8_t *header, const uint8_t *payload,
                         uint16_t payload_len) {
  uint32_t crc = 0xFFFFFFFF;
  crc = froth_crc32_update(crc, header, 16);
  crc = froth_crc32_update(crc, payload, payload_len);
  return crc ^ 0xFFFFFFFF;
}

/* ── Header parse ────────────────────────────────────────────────── */

froth_error_t froth_link_header_parse(const uint8_t *frame, uint16_t frame_len,
                                      froth_link_header_t *header,
                                      const uint8_t **payload) {
  if (frame_len < FROTH_LINK_HEADER_SIZE)
    return FROTH_ERROR_LINK_COBS_DECODE;

  header->magic[0] = frame[0];
  header->magic[1] = frame[1];
  if (header->magic[0] != FROTH_LINK_MAGIC_0 ||
      header->magic[1] != FROTH_LINK_MAGIC_1)
    return FROTH_ERROR_LINK_BAD_MAGIC;

  header->version = frame[2];
  if (header->version != FROTH_LINK_VERSION)
    return FROTH_ERROR_LINK_BAD_VERSION;

  header->message_type = frame[3];
  header->session_id = read_u64(frame + 4);
  header->seq = read_u16(frame + 12);
  header->payload_length = read_u16(frame + 14);
  header->crc32 = read_u32(frame + 16);

  if (header->payload_length > FROTH_LINK_MAX_PAYLOAD)
    return FROTH_ERROR_LINK_TOO_LARGE;

  if ((uint16_t)(FROTH_LINK_HEADER_SIZE + header->payload_length) != frame_len)
    return FROTH_ERROR_LINK_COBS_DECODE;

  const uint8_t *pl = frame + FROTH_LINK_HEADER_SIZE;
  uint32_t expected = link_crc(frame, pl, header->payload_length);

  if (expected != header->crc32)
    return FROTH_ERROR_LINK_BAD_CRC;

  *payload = pl;
  return FROTH_OK;
}

/* ── Header build ────────────────────────────────────────────────── */

froth_error_t froth_link_header_build(uint64_t session_id, uint8_t message_type,
                                      uint16_t seq, const uint8_t *payload,
                                      uint16_t payload_len, uint8_t *out,
                                      uint16_t out_cap, uint16_t *out_len) {
  uint16_t total = FROTH_LINK_HEADER_SIZE + payload_len;
  if (total > out_cap)
    return FROTH_ERROR_LINK_OVERFLOW;

  out[0] = FROTH_LINK_MAGIC_0;
  out[1] = FROTH_LINK_MAGIC_1;
  out[2] = FROTH_LINK_VERSION;
  out[3] = message_type;
  write_u64(out + 4, session_id);
  write_u16(out + 12, seq);
  write_u16(out + 14, payload_len);

  for (uint16_t i = 0; i < payload_len; i++)
    out[FROTH_LINK_HEADER_SIZE + i] = payload[i];

  uint32_t crc = link_crc(out, out + FROTH_LINK_HEADER_SIZE, payload_len);
  write_u32(out + 16, crc);

  *out_len = total;
  return FROTH_OK;
}

/* ── Full frame send ─────────────────────────────────────────────── */

static uint8_t raw_buf[FROTH_LINK_MAX_FRAME];
static uint8_t cobs_buf[FROTH_LINK_COBS_MAX];

froth_error_t froth_link_send_frame(uint64_t session_id, uint8_t message_type,
                                    uint16_t seq, const uint8_t *payload,
                                    uint16_t payload_len) {
  uint16_t raw_len, enc_len;

  FROTH_TRY(froth_link_header_build(session_id, message_type, seq, payload,
                                    payload_len, raw_buf, sizeof(raw_buf),
                                    &raw_len));

  FROTH_TRY(froth_cobs_encode(raw_buf, raw_len, cobs_buf, sizeof(cobs_buf),
                              &enc_len));

  FROTH_TRY(platform_emit_raw(0x00));
  for (uint16_t i = 0; i < enc_len; i++)
    FROTH_TRY(platform_emit_raw(cobs_buf[i]));
  FROTH_TRY(platform_emit_raw(0x00));

  return FROTH_OK;
}

/* ── Inbound frame accumulation ──────────────────────────────────── */

static uint8_t rx_buf[FROTH_LINK_COBS_MAX];
static uint16_t rx_pos = 0;
static bool rx_overflow = false;

void froth_link_frame_reset(void) {
  rx_pos = 0;
  rx_overflow = false;
}

froth_error_t froth_link_frame_byte(uint8_t byte) {
  if (rx_pos >= FROTH_LINK_COBS_MAX) {
    rx_overflow = true;
    return FROTH_OK;
  }
  rx_buf[rx_pos++] = byte;
  return FROTH_OK;
}

/* Decode + parse the accumulated frame. Returns FROTH_OK on success,
 * in which case header and payload are valid. Resets and returns
 * an error code (silently) on junk frames. */
froth_error_t froth_link_frame_decode(froth_link_header_t *header,
                                      const uint8_t **payload) {
  if (rx_overflow || rx_pos == 0) {
    froth_link_frame_reset();
    return FROTH_ERROR_LINK_COBS_DECODE;
  }

  uint16_t decoded_len;
  froth_error_t err = froth_cobs_decode(rx_buf, rx_pos, rx_buf,
                                        FROTH_LINK_COBS_MAX, &decoded_len);
  if (err != FROTH_OK) {
    froth_link_frame_reset();
    return err;
  }

  err = froth_link_header_parse(rx_buf, decoded_len, header, payload);
  if (err != FROTH_OK) {
    froth_link_frame_reset();
    return err;
  }

  return FROTH_OK;
}

/* Decode, parse, and dispatch in one shot. Old all-in-one path. */
froth_error_t froth_link_frame_complete(froth_vm_t *vm) {
  froth_link_header_t header;
  const uint8_t *payload;
  froth_error_t err = froth_link_frame_decode(&header, &payload);
  if (err != FROTH_OK)
    return FROTH_OK; /* junk frame, silently dropped */

  err = froth_link_dispatch(vm, &header, payload);
  froth_link_frame_reset();
  return err;
}
