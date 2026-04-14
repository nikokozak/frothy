#include "frothy_control.h"

#include "froth_slot_table.h"
#include "froth_transport.h"
#include "froth_vm.h"
#include "frothy_base_image.h"
#include "frothy_inspect.h"
#include "frothy_shell.h"
#include "frothy_snapshot.h"
#include "platform.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FROTHY_CONTROL_OUTPUT_CAP 128u

typedef struct {
  uint8_t *buf;
  uint16_t cap;
  uint16_t pos;
} frothy_control_writer_t;

typedef struct {
  const uint8_t *data;
  uint16_t len;
  uint16_t pos;
} frothy_control_reader_t;

typedef struct {
  bool bound;
  uint64_t session_id;
  uint16_t next_seq;
  uint16_t active_seq;
  uint8_t output_buf[FROTHY_CONTROL_OUTPUT_CAP];
  uint16_t output_len;
} frothy_control_session_t;

typedef enum {
  FROTHY_CONTROL_ACTION_CONTINUE = 0,
  FROTHY_CONTROL_ACTION_DETACH = 1,
} frothy_control_action_t;

static froth_error_t frothy_control_writer_u8(frothy_control_writer_t *writer,
                                              uint8_t value) {
  if (writer->pos + 1 > writer->cap) {
    return FROTH_ERROR_LINK_OVERFLOW;
  }
  writer->buf[writer->pos++] = value;
  return FROTH_OK;
}

static froth_error_t frothy_control_writer_u16(frothy_control_writer_t *writer,
                                               uint16_t value) {
  if (writer->pos + 2 > writer->cap) {
    return FROTH_ERROR_LINK_OVERFLOW;
  }
  writer->buf[writer->pos++] = (uint8_t)(value & 0xffu);
  writer->buf[writer->pos++] = (uint8_t)((value >> 8) & 0xffu);
  return FROTH_OK;
}

static froth_error_t frothy_control_writer_u32(frothy_control_writer_t *writer,
                                               uint32_t value) {
  if (writer->pos + 4 > writer->cap) {
    return FROTH_ERROR_LINK_OVERFLOW;
  }
  writer->buf[writer->pos++] = (uint8_t)(value & 0xffu);
  writer->buf[writer->pos++] = (uint8_t)((value >> 8) & 0xffu);
  writer->buf[writer->pos++] = (uint8_t)((value >> 16) & 0xffu);
  writer->buf[writer->pos++] = (uint8_t)((value >> 24) & 0xffu);
  return FROTH_OK;
}

static froth_error_t
frothy_control_writer_bytes(frothy_control_writer_t *writer,
                            const uint8_t *bytes, uint16_t length) {
  if (writer->pos + length > writer->cap) {
    return FROTH_ERROR_LINK_OVERFLOW;
  }
  memcpy(writer->buf + writer->pos, bytes, length);
  writer->pos += length;
  return FROTH_OK;
}

static froth_error_t frothy_control_writer_str(frothy_control_writer_t *writer,
                                               const char *text) {
  size_t length = strlen(text);

  if (length > UINT16_MAX) {
    return FROTH_ERROR_LINK_TOO_LARGE;
  }
  FROTH_TRY(frothy_control_writer_u16(writer, (uint16_t)length));
  return frothy_control_writer_bytes(writer, (const uint8_t *)text,
                                     (uint16_t)length);
}

static froth_error_t frothy_control_reader_u16(frothy_control_reader_t *reader,
                                               uint16_t *out) {
  if (reader->pos + 2 > reader->len) {
    return FROTH_ERROR_LINK_COBS_DECODE;
  }
  *out = (uint16_t)reader->data[reader->pos] |
         ((uint16_t)reader->data[reader->pos + 1] << 8);
  reader->pos += 2;
  return FROTH_OK;
}

static froth_error_t
frothy_control_reader_str_dup(frothy_control_reader_t *reader, char **out) {
  uint16_t length = 0;
  char *copy = NULL;

  *out = NULL;
  FROTH_TRY(frothy_control_reader_u16(reader, &length));
  if (reader->pos + length > reader->len) {
    return FROTH_ERROR_LINK_COBS_DECODE;
  }

  copy = (char *)malloc((size_t)length + 1);
  if (copy == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }
  memcpy(copy, reader->data + reader->pos, length);
  copy[length] = '\0';
  reader->pos += length;
  *out = copy;
  return FROTH_OK;
}

static froth_error_t frothy_control_send_frame(uint64_t session_id,
                                               uint8_t message_type,
                                               uint16_t seq,
                                               const uint8_t *payload,
                                               uint16_t payload_len) {
  return froth_link_send_frame(session_id, message_type, seq, payload,
                               payload_len);
}

static froth_error_t frothy_control_send_idle(uint64_t session_id,
                                              uint16_t seq) {
  return frothy_control_send_frame(session_id, FROTHY_CONTROL_IDLE_EVT, seq,
                                   NULL, 0);
}

static froth_error_t frothy_control_send_interrupted(uint64_t session_id,
                                                     uint16_t seq) {
  return frothy_control_send_frame(session_id, FROTHY_CONTROL_INTERRUPTED_EVT,
                                   seq, NULL, 0);
}

static froth_error_t
frothy_control_send_output(frothy_control_session_t *session) {
  uint8_t payload[2 + FROTHY_CONTROL_OUTPUT_CAP];
  frothy_control_writer_t writer = {payload, sizeof(payload), 0};

  if (session->output_len == 0 || session->active_seq == 0) {
    return FROTH_OK;
  }

  FROTH_TRY(frothy_control_writer_u16(&writer, (uint16_t)session->output_len));
  FROTH_TRY(frothy_control_writer_bytes(&writer, session->output_buf,
                                        session->output_len));
  session->output_len = 0;
  return frothy_control_send_frame(session->session_id,
                                   FROTHY_CONTROL_OUTPUT_EVT,
                                   session->active_seq, payload, writer.pos);
}

static froth_error_t frothy_control_capture_output(void *context,
                                                   uint8_t byte) {
  frothy_control_session_t *session = (frothy_control_session_t *)context;

  if (session->output_len >= FROTHY_CONTROL_OUTPUT_CAP) {
    FROTH_TRY(frothy_control_send_output(session));
  }

  session->output_buf[session->output_len++] = byte;
  if (byte == '\n' || session->output_len >= FROTHY_CONTROL_OUTPUT_CAP) {
    return frothy_control_send_output(session);
  }

  return FROTH_OK;
}

static void frothy_control_start_capture(frothy_control_session_t *session,
                                         uint16_t seq) {
  session->active_seq = seq;
  session->output_len = 0;
  platform_set_emit_hook(frothy_control_capture_output, session);
}

static froth_error_t
frothy_control_stop_capture(frothy_control_session_t *session) {
  froth_error_t err = frothy_control_send_output(session);

  platform_clear_emit_hook();
  session->active_seq = 0;
  session->output_len = 0;
  return err;
}

static froth_error_t frothy_control_send_string_value(uint64_t session_id,
                                                      uint16_t seq,
                                                      const char *text) {
  uint8_t payload[FROTH_LINK_MAX_PAYLOAD];
  frothy_control_writer_t writer = {payload, sizeof(payload), 0};

  FROTH_TRY(frothy_control_writer_str(&writer, text));
  return frothy_control_send_frame(session_id, FROTHY_CONTROL_VALUE_EVT, seq,
                                   payload, writer.pos);
}

static froth_error_t
frothy_control_send_rendered_value(frothy_runtime_t *runtime, uint64_t session_id,
                                   uint16_t seq, frothy_value_t value) {
  char *rendered = NULL;
  froth_error_t err = frothy_value_render(runtime, value, &rendered);

  if (err == FROTH_OK) {
    err = frothy_control_send_string_value(session_id, seq, rendered);
  }
  free(rendered);
  return err;
}

static froth_error_t frothy_control_send_value_payload(uint64_t session_id,
                                                       uint16_t seq,
                                                       const uint8_t *payload,
                                                       uint16_t payload_len) {
  return frothy_control_send_frame(session_id, FROTHY_CONTROL_VALUE_EVT, seq,
                                   payload, payload_len);
}

static froth_error_t frothy_control_send_error(uint64_t session_id,
                                               uint16_t seq,
                                               frothy_control_phase_t phase,
                                               froth_error_t err,
                                               const char *detail) {
  uint8_t payload[FROTH_LINK_MAX_PAYLOAD];
  frothy_control_writer_t writer = {payload, sizeof(payload), 0};

  FROTH_TRY(frothy_control_writer_u8(&writer, (uint8_t)phase));
  FROTH_TRY(frothy_control_writer_u16(&writer, (uint16_t)err));
  FROTH_TRY(frothy_control_writer_str(&writer, detail));
  return frothy_control_send_frame(session_id, FROTHY_CONTROL_ERROR_EVT, seq,
                                   payload, writer.pos);
}

static froth_error_t frothy_control_send_hello(uint64_t session_id,
                                               uint16_t seq) {
  uint8_t payload[FROTH_LINK_MAX_PAYLOAD];
  frothy_control_writer_t writer = {payload, sizeof(payload), 0};

  FROTH_TRY(frothy_control_writer_u8(&writer, FROTH_CELL_SIZE_BITS));
  FROTH_TRY(frothy_control_writer_u16(&writer, FROTH_LINK_MAX_PAYLOAD));
  FROTH_TRY(frothy_control_writer_u32(&writer, FROTH_HEAP_SIZE));
  FROTH_TRY(frothy_control_writer_u32(&writer, froth_vm.heap.pointer));
  FROTH_TRY(frothy_control_writer_u16(&writer, froth_slot_count()));
  FROTH_TRY(frothy_control_writer_u8(&writer, 0));
  FROTH_TRY(frothy_control_writer_str(&writer, FROTH_VERSION));
  FROTH_TRY(frothy_control_writer_str(&writer, FROTH_BOARD_NAME));
  FROTH_TRY(frothy_control_writer_u8(&writer, 0));
  return frothy_control_send_frame(session_id, FROTHY_CONTROL_HELLO_EVT, seq,
                                   payload, writer.pos);
}

static uint16_t frothy_control_overlay_slot_count(void) {
  froth_cell_u_t slot_count = froth_slot_count();
  froth_cell_u_t slot_index;
  uint16_t count = 0;

  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    if (froth_slot_is_overlay(slot_index)) {
      count++;
    }
  }

  return count;
}

static froth_error_t frothy_control_send_reset_value(uint64_t session_id,
                                                     uint16_t seq,
                                                     froth_error_t status) {
  uint32_t overlay_used = 0;
  uint8_t payload[FROTH_LINK_MAX_PAYLOAD];
  frothy_control_writer_t writer = {payload, sizeof(payload), 0};

  if (froth_vm.heap.pointer > froth_vm.watermark_heap_offset) {
    overlay_used = froth_vm.heap.pointer - froth_vm.watermark_heap_offset;
  }

  FROTH_TRY(frothy_control_writer_u32(&writer, (uint32_t)status));
  FROTH_TRY(frothy_control_writer_u32(&writer, FROTH_HEAP_SIZE));
  FROTH_TRY(frothy_control_writer_u32(&writer, froth_vm.heap.pointer));
  FROTH_TRY(frothy_control_writer_u32(&writer, overlay_used));
  FROTH_TRY(frothy_control_writer_u16(&writer, froth_slot_count()));
  FROTH_TRY(
      frothy_control_writer_u16(&writer, frothy_control_overlay_slot_count()));
  FROTH_TRY(frothy_control_writer_u8(&writer, 0));
  FROTH_TRY(frothy_control_writer_str(&writer, FROTH_VERSION));
  return frothy_control_send_value_payload(session_id, seq, payload,
                                           writer.pos);
}

static bool frothy_control_words_item_fits(const char *name) {
  size_t item_size = 2u + strlen(name);

  return item_size <= (size_t)FROTH_LINK_MAX_PAYLOAD - 2u;
}

static froth_error_t frothy_control_send_words(uint64_t session_id,
                                               uint16_t seq) {
  const char **names = NULL;
  size_t count = 0;
  size_t i = 0;
  uint8_t payload[FROTH_LINK_MAX_PAYLOAD];
  frothy_control_writer_t writer = {payload, sizeof(payload), 0};
  froth_error_t err;

  err = frothy_inspect_collect_words(&names, &count);
  if (err != FROTH_OK) {
    return err;
  }

  for (i = 0; i < count; i++) {
    if (!frothy_control_words_item_fits(names[i])) {
      frothy_inspect_free_words(names);
      return FROTH_ERROR_LINK_OVERFLOW;
    }
  }

  i = 0;
  while (err == FROTH_OK) {
    uint16_t chunk_count = 0;

    writer.pos = 0;
    err = frothy_control_writer_u16(&writer, 0);
    if (err != FROTH_OK) {
      break;
    }

    while (i < count) {
      size_t name_length = strlen(names[i]);
      uint16_t needed = (uint16_t)(2u + name_length);

      if ((size_t)writer.pos + (size_t)needed > sizeof(payload)) {
        if (chunk_count == 0) {
          err = FROTH_ERROR_LINK_OVERFLOW;
        }
        break;
      }
      err = frothy_control_writer_str(&writer, names[i]);
      if (err != FROTH_OK) {
        break;
      }
      chunk_count++;
      i++;
    }

    if (err != FROTH_OK) {
      break;
    }

    payload[0] = (uint8_t)(chunk_count & 0xffu);
    payload[1] = (uint8_t)((chunk_count >> 8) & 0xffu);
    err =
        frothy_control_send_value_payload(session_id, seq, payload, writer.pos);
    if (err != FROTH_OK) {
      break;
    }

    if (count == 0 || i == count) {
      break;
    }
  }
  frothy_inspect_free_words(names);
  if (err != FROTH_OK) {
    return err;
  }

  return FROTH_OK;
}

static froth_error_t frothy_control_send_see(uint64_t session_id, uint16_t seq,
                                             const char *name) {
  frothy_inspect_binding_view_t view;
  uint8_t payload[FROTH_LINK_MAX_PAYLOAD];
  frothy_control_writer_t writer = {payload, sizeof(payload), 0};
  froth_error_t err;

  err =
      frothy_inspect_render_binding_view(&froth_vm.frothy_runtime, name, &view);
  if (err != FROTH_OK) {
    return err;
  }

  {
    size_t name_length = strlen(name);
    size_t rendered_length = strlen(view.rendered);
    size_t offset = 0;
    size_t header_size = 2u + name_length + 1u + 1u + 2u;
    size_t available = 0;

    if (header_size > sizeof(payload)) {
      err = FROTH_ERROR_LINK_OVERFLOW;
    }
    if (err == FROTH_OK) {
      available = sizeof(payload) - header_size;
      if (rendered_length > 0 && available == 0) {
        err = FROTH_ERROR_LINK_OVERFLOW;
      }
    }

    while (err == FROTH_OK) {
      size_t chunk_length = rendered_length - offset;

      if (chunk_length > available) {
        chunk_length = available;
      }

      writer.pos = 0;
      err = frothy_control_writer_str(&writer, name);
      if (err == FROTH_OK) {
        err = frothy_control_writer_u8(&writer, view.is_overlay ? 1u : 0u);
      }
      if (err == FROTH_OK) {
        err = frothy_control_writer_u8(&writer, (uint8_t)view.value_class);
      }
      if (err == FROTH_OK) {
        err = frothy_control_writer_u16(&writer, (uint16_t)chunk_length);
      }
      if (err == FROTH_OK) {
        err = frothy_control_writer_bytes(
            &writer, (const uint8_t *)view.rendered + offset,
            (uint16_t)chunk_length);
      }
      if (err != FROTH_OK) {
        break;
      }

      err = frothy_control_send_value_payload(session_id, seq, payload,
                                              writer.pos);
      if (err != FROTH_OK) {
        break;
      }

      offset += chunk_length;
      if (offset == rendered_length) {
        break;
      }
    }
  }
  frothy_inspect_binding_view_free(&view);
  if (err != FROTH_OK) {
    return err;
  }

  return FROTH_OK;
}

static froth_error_t
frothy_control_expect_empty_payload(const froth_link_header_t *header) {
  if (header->payload_length != 0) {
    return FROTH_ERROR_SIGNATURE;
  }
  return FROTH_OK;
}

static froth_error_t
frothy_control_parse_string_payload(const froth_link_header_t *header,
                                    const uint8_t *payload, char **out) {
  frothy_control_reader_t reader = {payload, header->payload_length, 0};

  FROTH_TRY(frothy_control_reader_str_dup(&reader, out));
  if (reader.pos != reader.len) {
    free(*out);
    *out = NULL;
    return FROTH_ERROR_SIGNATURE;
  }
  return FROTH_OK;
}

static froth_error_t
frothy_control_handle_builtin_no_args(frothy_control_session_t *session,
                                      const froth_link_header_t *header,
                                      frothy_native_fn_t builtin,
                                      frothy_control_phase_t phase,
                                      const char *bad_detail,
                                      const char *fail_detail) {
  frothy_value_t result = frothy_value_make_nil();
  froth_error_t err = frothy_control_expect_empty_payload(header);
  froth_error_t stop_err = FROTH_OK;
  froth_error_t out_err = FROTH_OK;

  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_CONTROL, err,
                                        bad_detail));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  frothy_control_start_capture(session, header->seq);
  err = builtin(&froth_vm.frothy_runtime, NULL, NULL, 0, &result);
  stop_err = frothy_control_stop_capture(session);
  if (err == FROTH_ERROR_PROGRAM_INTERRUPTED) {
    if (stop_err != FROTH_OK) {
      return stop_err;
    }
    FROTH_TRY(
        frothy_control_send_interrupted(session->session_id, header->seq));
    return frothy_control_send_idle(session->session_id, header->seq);
  }
  if (err != FROTH_OK) {
    if (stop_err != FROTH_OK) {
      return stop_err;
    }
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq, phase,
                                        err, fail_detail));
    return frothy_control_send_idle(session->session_id, header->seq);
  }
  if (stop_err != FROTH_OK) {
    return stop_err;
  }

  out_err = frothy_control_send_rendered_value(&froth_vm.frothy_runtime,
                                               session->session_id, header->seq,
                                               result);
  if (out_err == FROTH_OK) {
    out_err = frothy_control_send_idle(session->session_id, header->seq);
  }
  return out_err;
}

static froth_error_t
frothy_control_handle_builtin_string_arg(frothy_control_session_t *session,
                                         const froth_link_header_t *header,
                                         const uint8_t *payload,
                                         frothy_native_fn_t builtin,
                                         frothy_control_phase_t phase,
                                         const char *bad_detail,
                                         const char *fail_detail) {
  char *text = NULL;
  frothy_value_t arg = frothy_value_make_nil();
  frothy_value_t result = frothy_value_make_nil();
  frothy_value_t args[1];
  bool arg_ready = false;
  froth_error_t err;
  froth_error_t stop_err = FROTH_OK;
  froth_error_t out_err = FROTH_OK;

  err = frothy_control_parse_string_payload(header, payload, &text);
  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_CONTROL, err,
                                        bad_detail));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  err = frothy_runtime_alloc_text(&froth_vm.frothy_runtime, text, strlen(text),
                                  &arg);
  free(text);
  text = NULL;
  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq, phase,
                                        err, fail_detail));
    return frothy_control_send_idle(session->session_id, header->seq);
  }
  arg_ready = true;
  args[0] = arg;

  frothy_control_start_capture(session, header->seq);
  err = builtin(&froth_vm.frothy_runtime, NULL, args, 1, &result);
  stop_err = frothy_control_stop_capture(session);
  if (arg_ready) {
    frothy_value_release(&froth_vm.frothy_runtime, arg);
    arg_ready = false;
  }
  if (err == FROTH_ERROR_PROGRAM_INTERRUPTED) {
    if (stop_err != FROTH_OK) {
      return stop_err;
    }
    FROTH_TRY(
        frothy_control_send_interrupted(session->session_id, header->seq));
    return frothy_control_send_idle(session->session_id, header->seq);
  }
  if (err != FROTH_OK) {
    if (stop_err != FROTH_OK) {
      return stop_err;
    }
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq, phase,
                                        err, fail_detail));
    return frothy_control_send_idle(session->session_id, header->seq);
  }
  if (stop_err != FROTH_OK) {
    return stop_err;
  }

  out_err = frothy_control_send_rendered_value(&froth_vm.frothy_runtime,
                                               session->session_id, header->seq,
                                               result);
  if (out_err == FROTH_OK) {
    out_err = frothy_control_send_idle(session->session_id, header->seq);
  }
  return out_err;
}

static froth_error_t
frothy_control_handle_eval(frothy_control_session_t *session,
                           const froth_link_header_t *header,
                           const uint8_t *payload) {
  char *source = NULL;
  frothy_shell_eval_result_t result;
  froth_error_t err;
  froth_error_t stop_err = FROTH_OK;
  froth_error_t out_err = FROTH_OK;
  frothy_control_phase_t phase = FROTHY_CONTROL_PHASE_EVAL;
  const char *detail = "evaluation failed";

  memset(&result, 0, sizeof(result));
  result.value = frothy_value_make_nil();

  err = frothy_control_parse_string_payload(header, payload, &source);
  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_CONTROL, err,
                                        "bad EVAL payload"));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  frothy_control_start_capture(session, header->seq);
  err = frothy_shell_eval_source(source, &result);
  stop_err = frothy_control_stop_capture(session);
  free(source);
  source = NULL;
  if (err == FROTH_ERROR_PROGRAM_INTERRUPTED) {
    if (stop_err != FROTH_OK) {
      out_err = stop_err;
    } else {
      out_err =
          frothy_control_send_interrupted(session->session_id, header->seq);
      if (out_err == FROTH_OK) {
        out_err = frothy_control_send_idle(session->session_id, header->seq);
      }
    }
    goto cleanup;
  }
  if (err != FROTH_OK) {
    if (stop_err != FROTH_OK) {
      out_err = stop_err;
      goto cleanup;
    }
    if (result.phase == FROTHY_SHELL_EVAL_PHASE_PARSE) {
      phase = FROTHY_CONTROL_PHASE_PARSE;
      detail = "parse failed";
    }
    out_err = frothy_control_send_error(session->session_id, header->seq, phase,
                                        err, detail);
    if (out_err == FROTH_OK) {
      out_err = frothy_control_send_idle(session->session_id, header->seq);
    }
    goto cleanup;
  }
  if (stop_err != FROTH_OK) {
    out_err = stop_err;
    goto cleanup;
  }
  out_err = frothy_control_send_string_value(session->session_id, header->seq,
                                             result.rendered);
  if (out_err == FROTH_OK) {
    out_err = frothy_control_send_idle(session->session_id, header->seq);
  }

cleanup:
  frothy_shell_eval_result_free(&result);
  free(source);
  return out_err;
}

static froth_error_t
frothy_control_handle_words(frothy_control_session_t *session,
                            const froth_link_header_t *header) {
  froth_error_t err = frothy_control_expect_empty_payload(header);

  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_CONTROL, err,
                                        "bad WORDS payload"));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  err = frothy_control_send_words(session->session_id, header->seq);
  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_INSPECT, err,
                                        "words failed"));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  return frothy_control_send_idle(session->session_id, header->seq);
}

static froth_error_t
frothy_control_handle_reset(frothy_control_session_t *session,
                            const froth_link_header_t *header) {
  froth_error_t err = frothy_control_expect_empty_payload(header);
  froth_error_t reset_status = FROTH_OK;

  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_CONTROL, err,
                                        "bad RESET payload"));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  reset_status = frothy_base_image_reset();
  err = frothy_control_send_reset_value(session->session_id, header->seq,
                                        reset_status);
  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_CONTROL, err,
                                        "reset failed"));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  return frothy_control_send_idle(session->session_id, header->seq);
}

static froth_error_t
frothy_control_handle_see(frothy_control_session_t *session,
                          const froth_link_header_t *header,
                          const uint8_t *payload) {
  char *name = NULL;
  froth_error_t err;

  err = frothy_control_parse_string_payload(header, payload, &name);
  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_CONTROL, err,
                                        "bad SEE payload"));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  err = frothy_control_send_see(session->session_id, header->seq, name);
  free(name);
  if (err != FROTH_OK) {
    FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                        FROTHY_CONTROL_PHASE_INSPECT, err,
                                        "see failed"));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  return frothy_control_send_idle(session->session_id, header->seq);
}

static froth_error_t frothy_control_dispatch(
    frothy_control_session_t *session, const froth_link_header_t *header,
    const uint8_t *payload, frothy_control_action_t *action_out) {
  froth_error_t err;

  *action_out = FROTHY_CONTROL_ACTION_CONTINUE;

  if (!session->bound) {
    if (header->message_type != FROTHY_CONTROL_HELLO_REQ ||
        header->session_id == 0 || header->seq != 0) {
      return FROTH_OK;
    }
    if (header->payload_length != 0) {
      FROTH_TRY(frothy_control_send_error(
          header->session_id, header->seq, FROTHY_CONTROL_PHASE_CONTROL,
          FROTH_ERROR_SIGNATURE, "bad HELLO payload"));
      return frothy_control_send_idle(header->session_id, header->seq);
    }

    session->bound = true;
    session->session_id = header->session_id;
    session->next_seq = 1;
    FROTH_TRY(frothy_control_send_hello(session->session_id, header->seq));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  if (header->session_id != session->session_id) {
    return FROTH_OK;
  }
  if (header->seq != session->next_seq) {
    FROTH_TRY(frothy_control_send_error(
        session->session_id, header->seq, FROTHY_CONTROL_PHASE_CONTROL,
        FROTH_ERROR_SIGNATURE, "unexpected sequence"));
    return frothy_control_send_idle(session->session_id, header->seq);
  }

  switch (header->message_type) {
  case FROTHY_CONTROL_EVAL_REQ:
    FROTH_TRY(frothy_control_handle_eval(session, header, payload));
    break;
  case FROTHY_CONTROL_WORDS_REQ:
    FROTH_TRY(frothy_control_handle_words(session, header));
    break;
  case FROTHY_CONTROL_SEE_REQ:
    FROTH_TRY(frothy_control_handle_see(session, header, payload));
    break;
  case FROTHY_CONTROL_RESET_REQ:
    FROTH_TRY(frothy_control_handle_reset(session, header));
    break;
  case FROTHY_CONTROL_SAVE_REQ:
    FROTH_TRY(frothy_control_handle_builtin_no_args(
        session, header, frothy_builtin_save, FROTHY_CONTROL_PHASE_EVAL,
        "bad SAVE payload", "save failed"));
    break;
  case FROTHY_CONTROL_RESTORE_REQ:
    FROTH_TRY(frothy_control_handle_builtin_no_args(
        session, header, frothy_builtin_restore, FROTHY_CONTROL_PHASE_EVAL,
        "bad RESTORE payload", "restore failed"));
    break;
  case FROTHY_CONTROL_WIPE_REQ:
    FROTH_TRY(frothy_control_handle_builtin_no_args(
        session, header, frothy_builtin_wipe, FROTHY_CONTROL_PHASE_EVAL,
        "bad WIPE payload", "wipe failed"));
    break;
  case FROTHY_CONTROL_CORE_REQ:
    FROTH_TRY(frothy_control_handle_builtin_string_arg(
        session, header, payload, frothy_builtin_core,
        FROTHY_CONTROL_PHASE_INSPECT, "bad CORE payload", "core failed"));
    break;
  case FROTHY_CONTROL_SLOT_INFO_REQ:
    FROTH_TRY(frothy_control_handle_builtin_string_arg(
        session, header, payload, frothy_builtin_slot_info,
        FROTHY_CONTROL_PHASE_INSPECT, "bad SLOT_INFO payload",
        "slot info failed"));
    break;
  case FROTHY_CONTROL_DETACH_REQ:
    err = frothy_control_expect_empty_payload(header);
    if (err != FROTH_OK) {
      FROTH_TRY(frothy_control_send_error(session->session_id, header->seq,
                                          FROTHY_CONTROL_PHASE_CONTROL, err,
                                          "bad DETACH payload"));
      FROTH_TRY(frothy_control_send_idle(session->session_id, header->seq));
      break;
    }
    FROTH_TRY(frothy_control_send_idle(session->session_id, header->seq));
    *action_out = FROTHY_CONTROL_ACTION_DETACH;
    break;
  default:
    FROTH_TRY(frothy_control_send_error(
        session->session_id, header->seq, FROTHY_CONTROL_PHASE_CONTROL,
        FROTH_ERROR_LINK_UNKNOWN_TYPE, "unknown request"));
    FROTH_TRY(frothy_control_send_idle(session->session_id, header->seq));
    break;
  }

  if (*action_out == FROTHY_CONTROL_ACTION_CONTINUE) {
    session->next_seq = (session->next_seq == 0xffffu)
                            ? 1u
                            : (uint16_t)(session->next_seq + 1u);
  }
  return FROTH_OK;
}

static froth_error_t frothy_control_read_frame(froth_link_header_t *header,
                                               const uint8_t **payload) {
  bool in_frame = false;

  froth_link_frame_reset();
  while (1) {
    uint8_t byte = 0;
    froth_error_t err = platform_key(&byte);

    if (err != FROTH_OK) {
      if (froth_vm.interrupted) {
        froth_vm.interrupted = 0;
        froth_link_frame_reset();
        return FROTH_ERROR_PROGRAM_INTERRUPTED;
      }
      if (platform_input_closed()) {
        froth_link_frame_reset();
        return FROTH_ERROR_IO;
      }
      continue;
    }

    if (!in_frame && byte == 0x03) {
      froth_link_frame_reset();
      return FROTH_ERROR_PROGRAM_INTERRUPTED;
    }

    if (!in_frame) {
      if (byte == 0x00) {
        froth_link_frame_reset();
        in_frame = true;
      }
      continue;
    }

    if (byte == 0x00) {
      err = froth_link_frame_decode(header, payload);
      froth_link_frame_reset();
      if (err != FROTH_OK) {
        in_frame = false;
        continue;
      }
      return FROTH_OK;
    }

    err = froth_link_frame_byte(byte);
    if (err != FROTH_OK) {
      froth_link_frame_reset();
      in_frame = false;
    }
  }
}

froth_error_t frothy_control_run(void) {
  frothy_control_session_t session;

  memset(&session, 0, sizeof(session));
  platform_clear_emit_hook();

  while (1) {
    froth_link_header_t header;
    const uint8_t *payload = NULL;
    frothy_control_action_t action = FROTHY_CONTROL_ACTION_CONTINUE;
    froth_error_t err = frothy_control_read_frame(&header, &payload);

    if (err == FROTH_ERROR_PROGRAM_INTERRUPTED) {
      platform_clear_emit_hook();
      return FROTH_OK;
    }
    if (err == FROTH_ERROR_IO && platform_input_closed()) {
      platform_clear_emit_hook();
      return FROTH_OK;
    }
    if (err != FROTH_OK) {
      platform_clear_emit_hook();
      return err;
    }

    err = frothy_control_dispatch(&session, &header, payload, &action);
    if (err != FROTH_OK) {
      platform_clear_emit_hook();
      return err;
    }
    if (action == FROTHY_CONTROL_ACTION_DETACH) {
      platform_clear_emit_hook();
      return FROTH_OK;
    }
  }
}
