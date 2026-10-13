#pragma once

#include "image.h"
#include "runtime.h"

#if FR_FEATURE_PERSISTENCE

/*
 * Current payload version carries 32-bit int value fields. Independent from
 * the overlay update version in config.h. Project policy is no backwards
 * compatibility (D7 in T12L-7); the decoder only accepts the current version.
 * An older payload does not load; a later save or a wipe replaces it.
 */
enum {
  FR_PERSIST_PAYLOAD_VERSION = 6,
};

typedef fr_err_t (*fr_persist_payload_write_fn_t)(void *ctx,
                                                  const uint8_t *bytes,
                                                  uint16_t length);

fr_err_t fr_persist_payload_encode(const fr_runtime_t *runtime, uint8_t *bytes,
                                   uint16_t cap, uint16_t *out_length);
fr_err_t fr_persist_payload_encode_stream(
    const fr_runtime_t *runtime, fr_persist_payload_write_fn_t write, void *ctx,
    uint16_t *out_length);
fr_err_t fr_persist_payload_restore(fr_runtime_t *runtime, const uint8_t *bytes,
                                    uint16_t length);

/* D5: save encodes only L2 records from the runtime, with the L1 prefix
 * (BIND + NAME records pulled from the existing NVS payload) pasted in
 * verbatim. See the comments on the impl for the literal-value-only
 * limitation on extraction. */
fr_err_t fr_persist_payload_extract_library_records(const uint8_t *src,
                                                    uint16_t src_length,
                                                    uint8_t *dst,
                                                    uint16_t dst_cap,
                                                    uint16_t *out_length);
fr_err_t fr_persist_payload_save_encode(const fr_runtime_t *runtime,
                                        const uint8_t *library_prefix,
                                        uint16_t library_prefix_length,
                                        uint8_t *bytes, uint16_t cap,
                                        uint16_t *out_length);
fr_err_t fr_persist_payload_save_stream(
    const fr_runtime_t *runtime, const uint8_t *old_payload,
    uint16_t old_payload_length, fr_persist_payload_write_fn_t write,
    void *ctx, uint16_t *out_length);

/* The values that a save must store hold no handle and no Bytes value. A
 * command that saves at once checks this before its save (ADR 0090
 * amendment). A refusal then leaves its target binding, the tier and the
 * saved image as they were. The encoder checks it too. On a refusal, the
 * note names the slot. */
fr_err_t fr_persist_payload_check_savable(const fr_runtime_t *runtime);

/* ADR 0070 tolerant save. hold_volatile_slots collects the overlay slots
 * whose live handle value can be stored as nil, or returns FR_ERR_VOLATILE
 * when any of them must keep the old strict rejection. note_held_slots
 * records the advisory the REPL renders after the save succeeds. */
fr_err_t fr_persist_payload_hold_volatile_slots(const fr_runtime_t *runtime,
                                                fr_handle_hold_t *out,
                                                uint8_t cap,
                                                uint8_t *out_count);
void fr_persist_payload_note_held_slots(const fr_runtime_t *runtime,
                                        fr_slot_id_t first_slot_id,
                                        uint8_t count);

/* Wipe the module-global per-slot tier stamps. Called when a fresh base image
 * is installed so stamps from a prior runtime cannot leak across. */
void fr_persist_session_reset(void);

/* Stamp the slot(s) changed by the current install tier. The overlay helper
 * mirrors the REPL apply path; the slot helper covers value bindings that write
 * one slot without producing an overlay update. */
void fr_persist_session_install_tier_stamp_slot(const fr_runtime_t *runtime,
                                                fr_slot_id_t slot_id);
void fr_persist_session_install_tier_stamp_overlay(
    const fr_runtime_t *runtime, const fr_overlay_update_t *update);

#endif
