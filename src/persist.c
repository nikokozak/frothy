#include "persist.h"

#if !FR_FEATURE_PERSISTENCE
#error "persist.c should only be compiled when FR_FEATURE_PERSISTENCE is enabled"
#endif

#include "base_defs.h"
#include "base_image.h"
#include "code.h"
#include "crc.h"
#include "event.h"
#include "handle.h"
#include "object.h"
#include "persist_payload.h"
#include "platform.h"
#include "profile.h"
#include "slot.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint16_t fr_persist_last_payload_bytes = 0;
static const uint8_t *fr_persist_boot_payload_bytes = NULL;
static uint16_t fr_persist_boot_payload_length = 0;
static bool fr_persist_boot_image_pinned;

static void fr_persist_forget_boot_image(void) {
  fr_persist_boot_payload_bytes = NULL;
  fr_persist_boot_payload_length = 0;
  fr_persist_boot_image_pinned = false;
}

typedef struct fr_persist_stream_ctx_t {
  uint32_t crc;
  uint32_t length;
} fr_persist_stream_ctx_t;

static fr_err_t fr_persist_stream_write_payload(void *ctx,
                                                const uint8_t *bytes,
                                                uint16_t length) {
  fr_persist_stream_ctx_t *stream = (fr_persist_stream_ctx_t *)ctx;

  if (stream == NULL || (bytes == NULL && length > 0)) {
    return FR_ERR_INVALID;
  }
  if (stream->length + length > FR_PERSIST_PAYLOAD_BYTES) {
    return FR_ERR_CAPACITY;
  }
  if (length > 0) {
    FR_TRY(fr_platform_persist_stream_write(bytes, length));
    stream->crc = fr_crc32_update(stream->crc, bytes, length);
    stream->length += length;
  }
  return FR_OK;
}

static fr_err_t fr_persist_stream_commit(
    const fr_runtime_t *runtime, const uint8_t *old_payload,
    uint16_t old_payload_length, bool preserve_library) {
  fr_persist_stream_ctx_t stream = {0xffffffffu, 0};
  uint16_t payload_length = 0;
  uint8_t header[FR_PERSIST_HEADER_BYTES];
  fr_err_t err = FR_OK;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  err = fr_platform_persist_stream_begin();
  if (err != FR_OK) {
    return err;
  }
  if (preserve_library) {
    err = fr_persist_payload_save_stream(
        runtime, old_payload, old_payload_length, fr_persist_stream_write_payload,
        &stream, &payload_length);
  } else {
    err = fr_persist_payload_encode_stream(
        runtime, fr_persist_stream_write_payload, &stream, &payload_length);
  }
  if (err == FR_OK && payload_length != stream.length) {
    err = FR_ERR_CORRUPT;
  }
  if (err == FR_OK) {
    err = fr_persist_format_build_header(header, stream.length, ~stream.crc);
  }
  if (err == FR_OK) {
    err = fr_platform_persist_stream_finalize(header);
  }
  if (err != FR_OK) {
    fr_platform_persist_stream_abort();
    return err;
  }
  fr_persist_last_payload_bytes = payload_length;
  return FR_OK;
}

/* Defined in persist_payload.c — boot two-call restore. fr_persist_restore_library
 * resets the runtime, installs resources, and applies only L1 binds (per-bind
 * failures log + skip per SPEC D6). fr_persist_restore_user must follow
 * within the same boot sequence; it applies L2 binds plus names and events
 * onto the runtime the L1 pass left behind. fr_persist_payload_restore_user_only
 * is the SPEC D5 user-`restore` path: applies L2 binds plus names and events
 * to the runtime without resetting, so the L1 already in place from boot
 * survives unchanged. */
extern fr_err_t fr_persist_payload_restore_library(fr_runtime_t *runtime,
                                                   const uint8_t *bytes,
                                                   uint16_t length);
extern fr_err_t fr_persist_payload_restore_user_after_library(
    fr_runtime_t *runtime, const uint8_t *bytes, uint16_t length);
extern fr_err_t fr_persist_payload_restore_user_only(fr_runtime_t *runtime,
                                                     const uint8_t *bytes,
                                                     uint16_t length);
extern fr_err_t fr_persist_payload_restore(fr_runtime_t *runtime,
                                           const uint8_t *bytes,
                                           uint16_t length);

typedef fr_err_t (*fr_persist_payload_apply_fn_t)(fr_runtime_t *runtime,
                                                  const uint8_t *bytes,
                                                  uint16_t length);

static fr_err_t fr_persist_apply_payload(fr_runtime_t *runtime,
                                         const uint8_t *payload,
                                         uint16_t payload_length,
                                         fr_persist_payload_apply_fn_t apply,
                                         bool reset_on_error) {
  fr_err_t err = FR_OK;

  if (runtime == NULL || payload == NULL || apply == NULL) {
    return FR_ERR_INVALID;
  }

  err = apply(runtime, payload, payload_length);
  if (err != FR_OK && reset_on_error) {
    FR_TRY(fr_runtime_reset(runtime));
  }
  return err;
}

static fr_err_t fr_persist_cleanup_failed_apply(fr_runtime_t *runtime) {
  fr_err_t err = fr_runtime_clear_project(runtime);

  fr_code_restore_base(runtime);
  fr_object_restore_base(runtime);
  return err;
}

/* latest_only stops after the newest image instead of falling back to the
 * older one. The save tail needs that for its *result*: a fallback mount
 * would quietly put the runtime back on the previous program while save
 * reported success (ADR 0070). It is not a recovery policy -- the tolerant
 * save runs the ordinary loop afterwards when the newest image fails.
 * Boot and public restore keep the fallback -- there, an older good image
 * is the recovery. */
static fr_err_t
fr_persist_restore_read_and_apply(fr_runtime_t *runtime,
                                  fr_persist_payload_apply_fn_t apply,
                                  bool reset_on_miss, bool latest_only,
                                  uint16_t *out_applied_payload_length) {
  fr_err_t err = FR_OK;
  fr_err_t last_err = FR_ERR_NOT_FOUND;
  const uint8_t *image = NULL;
  const uint8_t *payload = NULL;
  uint16_t image_length = 0;
  uint16_t payload_length = 0;
  fr_persist_format_info_t info = {0};

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }

  for (uint8_t image_index = 0;; image_index++) {
    bool apply_started = false;

    err = fr_platform_persist_mount(image_index, &image, &image_length);
    if (err == FR_ERR_NOT_FOUND) {
      break;
    }
    if (err != FR_OK) {
      if (reset_on_miss) {
        FR_TRY(fr_runtime_reset(runtime));
      }
      return err;
    }

    err = fr_persist_format_validate(image, image_length, &info);
    if (err == FR_OK) {
      if (info.payload_length > UINT16_MAX) {
        err = FR_ERR_CAPACITY;
      } else {
        payload_length = (uint16_t)info.payload_length;
        payload = &image[FR_PERSIST_HEADER_BYTES];
        apply_started = true;
        err = fr_persist_apply_payload(runtime, payload, payload_length, apply,
                                       reset_on_miss);
      }
    }
    if (err == FR_OK) {
      err = fr_platform_persist_mount_commit();
      if (err != FR_OK) {
        fr_err_t cleanup_err = fr_persist_cleanup_failed_apply(runtime);

        fr_platform_persist_mount_discard();
        if (cleanup_err != FR_OK) {
          return cleanup_err;
        }
        return err;
      }
      if (out_applied_payload_length != NULL) {
        *out_applied_payload_length = payload_length;
      }
      fr_code_mark_persist_image(runtime);
      fr_object_mark_persist_image(runtime);
      fr_slot_mark_persist_image(runtime);
      if (apply == fr_persist_payload_restore_library) {
        fr_persist_boot_payload_bytes = payload;
        fr_persist_boot_payload_length = payload_length;
      }
      return FR_OK;
    }
    if (apply_started) {
      fr_err_t cleanup_err = fr_persist_cleanup_failed_apply(runtime);

      fr_platform_persist_mount_discard();
      if (cleanup_err != FR_OK) {
        return cleanup_err;
      }
    } else {
      fr_platform_persist_mount_discard();
    }
    last_err = err;
    if (latest_only) {
      break;
    }
  }

  if (last_err == FR_ERR_NOT_FOUND && reset_on_miss) {
    FR_TRY(fr_runtime_reset(runtime));
  }
  return last_err;
}

static fr_err_t fr_persist_remount(fr_runtime_t *runtime, bool latest_only) {
  return fr_persist_restore_read_and_apply(runtime, fr_persist_payload_restore,
                                           true, latest_only, NULL);
}

/* D5: save persists the user tier only and preserves the library tier in
 * durable storage byte-for-byte. The existing committed payload (if any) is
 * scanned for L1 record spans, those source bytes are copied into the new
 * stream in source order, and freshly encoded L2 records follow them. A first
 * save against empty storage produces an L2-only payload. */
/* Everything up to and including the header write, which is the commit:
 * the payload goes to the inactive slot first and the stamped header
 * last. On any error nothing is committed -- the inactive slot may be
 * dirty, but the newest valid image and the live runtime are both
 * untouched. Mounting the new image is the caller's next step, and the
 * boundary between the two is what tells a failed save from a failed
 * remount. */
static fr_err_t fr_persist_commit_image(fr_runtime_t *runtime) {
  fr_err_t read_err = FR_OK;
  fr_err_t save_err = FR_OK;
  const uint8_t *image = NULL;
  const uint8_t *old_payload = NULL;
  uint16_t image_length = 0;
  uint16_t old_payload_length = 0;
  fr_persist_format_info_t info = {0};

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  fr_persist_forget_boot_image();

  read_err = fr_platform_persist_mount(0, &image, &image_length);
  if (read_err == FR_OK) {
    save_err = fr_persist_format_validate(image, image_length, &info);
    if (save_err == FR_OK) {
      if (info.payload_length > UINT16_MAX) {
        save_err = FR_ERR_CAPACITY;
      } else {
        old_payload = &image[FR_PERSIST_HEADER_BYTES];
        old_payload_length = (uint16_t)info.payload_length;
      }
    }
  } else if (read_err != FR_ERR_NOT_FOUND && read_err != FR_ERR_CORRUPT &&
             read_err != FR_ERR_OTHER_RELEASE) {
    return read_err;
  }
  if (save_err == FR_OK) {
    save_err = fr_persist_stream_commit(runtime, old_payload,
                                        old_payload_length, true);
  }
  if (save_err != FR_OK) {
    fr_platform_persist_mount_discard();
  }
  return save_err;
}

/* ADR 0071. These entries replace the running program: they commit a new
 * image, remount one, or reset the runtime. None of that can happen while
 * the VM is executing, because every live frame is reading instructions
 * out of the image and holding runtime-owned values that a replacement
 * destroys. The check comes first in each entry, before any commit --
 * stream begin erases the slot it picked, so a refusal that arrived later
 * would already have destroyed what the frames are reading.
 *
 * The guard sits here rather than on the save/restore/wipe natives so a
 * linked native calling these directly meets it too. C that goes around
 * these entries stays trusted, as with every kernel API. */
static fr_err_t fr_persist_check_prompt_only(const fr_runtime_t *runtime) {
  if (fr_runtime_is_executing(runtime)) {
    return FR_ERR_PROMPT_ONLY;
  }
  return FR_OK;
}

fr_err_t fr_persist_save(fr_runtime_t *runtime) {
  FR_TRY(fr_persist_check_prompt_only(runtime));
  FR_TRY(fr_persist_commit_image(runtime));
  return fr_persist_remount(runtime, false);
}

void fr_persist_note_save_rejection(fr_runtime_t *runtime, fr_err_t err) {
  if (err != FR_ERR_VOLATILE || runtime == NULL || runtime->diag == NULL) {
    return;
  }
  if (runtime->diag->kind == FR_DIAG_NONE) {
    runtime->diag->kind = FR_DIAG_NOTE;
    runtime->diag->message_id = FR_DIAG_MSG_RUNTIME_SLOT_UNPERSISTABLE;
    runtime->diag->got = FR_DIAG_UNPERSISTABLE_UNSUPPORTED_VALUE;
  }
  runtime->diag->presentation = FR_DIAG_PRESENT_NOTICE;
}

/* ADR 0070. The prompt's own `save` stores slots holding handle values as
 * nil instead of refusing, and keeps the live ones running: the LED stays
 * lit, the program keeps its bindings, and the response says which slots
 * will come back empty after a reboot. A stale ref -- a handle value whose
 * resource is already closed -- is an inert value and rides along.
 *
 * Every other route to save -- an expression, a saved function, wipe-user's
 * internal save -- calls fr_persist_save and keeps refusing. That is the
 * point of splitting them: this path runs outside the VM, where nothing is
 * mid-execution when the save tail remounts the image.
 *
 * Whatever cannot be held (see fr_persist_payload_hold_volatile_slots)
 * falls back to the strict save, so those responses stay exactly as they
 * were. */
fr_err_t fr_persist_save_tolerant(fr_runtime_t *runtime) {
  FR_TRY(fr_persist_check_prompt_only(runtime));
#if FR_FEATURE_HANDLES
  fr_handle_hold_t held[FR_HANDLE_TABLE_CAPACITY];
  uint8_t held_count = 0;
  fr_err_t err = FR_OK;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  if (fr_persist_payload_hold_volatile_slots(
          runtime, held, (uint8_t)FR_HANDLE_TABLE_CAPACITY, &held_count) !=
          FR_OK ||
      held_count == 0) {
    err = fr_persist_save(runtime);
    fr_persist_note_save_rejection(runtime, err);
    return err;
  }

  runtime->held_handles = held;
  runtime->held_handle_count = held_count;
  err = fr_persist_commit_image(runtime);
  if (err == FR_OK) {
    err = fr_persist_remount(runtime, true);
    if (err != FR_OK) {
      /* Committed, but the new image did not mount. Latest-only kept that
       * from passing as a success on the older program; recovery is still
       * the ordinary candidate loop, which every other save gets. The hold
       * is spent by now, so its handles close with everything else and the
       * runtime comes back on a mounted program instead of a cleared one.
       * The save's own error is what the user hears either way. */
      runtime->held_handles = NULL;
      runtime->held_handle_count = 0;
      (void)fr_persist_remount(runtime, false);
    }
  }
  runtime->held_handles = NULL;
  runtime->held_handle_count = 0;
  if (err != FR_OK) {
    /* Two shapes end here. A commit that never happened leaves the runtime
     * exactly as it was, handles included, with the previous image still
     * the newest -- nothing to undo. A commit that would not mount has just
     * been recovered above. Either way the save's own error is the answer. */
    fr_persist_note_save_rejection(runtime, err);
    return err;
  }

  /* The image has the slots as nil; the runtime gets its handles back. */
  for (uint8_t i = 0; i < held_count; i++) {
    FR_TRY(fr_slot_write(runtime, held[i].slot_id, held[i].value));
  }
  fr_persist_payload_note_held_slots(runtime, held[0].slot_id, held_count);
  return FR_OK;
#else
  fr_err_t err = fr_persist_save(runtime);

  fr_persist_note_save_rejection(runtime, err);
  return err;
#endif
}

/* D5: public `restore` brings L2 back from durable storage; L1 binds already
 * in place from boot stay untouched. The payload layer rewinds code to the
 * base-image boundary and remounts saved code so code ids do not drift. The
 * no-payload path still skips reset so an empty restore cannot collapse L1. */
fr_err_t fr_persist_restore(fr_runtime_t *runtime) {
  fr_err_t err = FR_OK;

  FR_TRY(fr_persist_check_prompt_only(runtime));
  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  fr_persist_forget_boot_image();
#if FR_FEATURE_BLE
  /* Public restore preserves library-tier slots, so it cannot clear the whole
   * runtime. BLE is still volatile project state and must stop before saved
   * user code replaces the current user tier. The clear is authoritative for
   * connections even when it errors (they drop before later cleanup steps
   * can fail), so the runtime entries are forgotten either way -- otherwise
   * close_all would preserve them as zombies (ADR 0068). */
  {
    fr_err_t ble_err = fr_platform_ble_project_clear();

    fr_handle_forget_kind(runtime, FR_HANDLE_KIND_BLE_CONNECTION);
    FR_TRY(ble_err);
  }
#endif
  /* Handles are volatile project state for the same reason: replacing the
   * user tier drops every binding that could close them, leaving the pin
   * stuck busy until reset (same failure wipe-user had). A failed close
   * keeps its entry (ADR 0068) for the next cleanup to retry; the result
   * is not reported -- restore errors must mean restore. */
  fr_handle_close_all(runtime);
  err = fr_persist_restore_read_and_apply(
      runtime, fr_persist_payload_restore_user_only, false, false, NULL);
  if (err == FR_ERR_OTHER_RELEASE && runtime->diag != NULL &&
      runtime->diag->kind == FR_DIAG_NONE) {
    runtime->diag->kind = FR_DIAG_NOTE;
    runtime->diag->note =
        "the image comes from another release -- save writes a new one";
  }
  return err;
}

fr_err_t fr_persist_restore_library(fr_runtime_t *runtime) {
  FR_TRY(fr_persist_check_prompt_only(runtime));
  fr_err_t err = FR_OK;
  uint16_t payload_length = 0;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  fr_persist_forget_boot_image();

  err = fr_persist_restore_read_and_apply(
      runtime, fr_persist_payload_restore_library, true, false,
      &payload_length);
  if (err == FR_OK) {
    fr_persist_boot_payload_length = payload_length;
    fr_persist_boot_image_pinned = true;
  }
  return err;
}

fr_err_t fr_persist_restore_user(fr_runtime_t *runtime) {
  FR_TRY(fr_persist_check_prompt_only(runtime));
  fr_err_t err = FR_OK;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  if (!fr_persist_boot_image_pinned) {
    return FR_ERR_NOT_FOUND;
  }

  err = fr_persist_apply_payload(runtime, fr_persist_boot_payload_bytes,
                                 fr_persist_boot_payload_length,
                                 fr_persist_payload_restore_user_after_library,
                                 false);
  if (err == FR_OK) {
    fr_slot_mark_persist_image(runtime);
  }
  if (err == FR_OK || err == FR_ERR_NOT_FOUND) {
    fr_persist_forget_boot_image();
  }
  return err;
}

fr_err_t fr_persist_wipe(fr_runtime_t *runtime) {
  FR_TRY(fr_persist_check_prompt_only(runtime));
  fr_err_t clear_err;
  fr_err_t restart_err;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }

  /* A full wipe is recovery. Project clear leaves the runtime consistent even
   * when platform cleanup fails, and the restart releases platform state. */
  clear_err = fr_runtime_clear_project(runtime);
  fr_persist_forget_boot_image();
  fr_platform_persist_unmount();
  FR_TRY(fr_platform_persist_clear());

  restart_err = fr_platform_restart();
  FR_TRY(fr_base_image_install(runtime));
  FR_TRY(clear_err);
  return restart_err == FR_ERR_UNSUPPORTED ? FR_OK : restart_err;
}

/* Defined in persist_payload.c. Drops every user-tier overlay binding from
 * the runtime; library-tier slots stay. The encoder reads runtime->slots so
 * the fr_persist_save below writes only the surviving library binds. */
extern void fr_persist_session_wipe_user_tier(fr_runtime_t *runtime);

fr_err_t fr_persist_wipe_user(fr_runtime_t *runtime) {
  FR_TRY(fr_persist_check_prompt_only(runtime));
  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  /* Events and handles are user runtime state: they are only ever created by
   * user code. Stop events here (a full wipe does so via fr_runtime_reset) so
   * a timer cannot keep firing into a slot the tier wipe just cleared, which
   * spams 'wrong type' errors. Close handles for the same reason with a worse
   * failure mode: the tier wipe drops every binding that could close them, so
   * a surviving platform channel (an open PWM pin, an I2C bus) would be
   * unreachable and its pin stuck busy until reset. A failed close keeps
   * its entry (ADR 0068); the save's remount re-enters clear_project and
   * retries it, so no error is reported from here. */
  FR_TRY(fr_event_clear_table(runtime));
  fr_handle_close_all(runtime);
  fr_persist_session_wipe_user_tier(runtime);
  return fr_persist_save(runtime);
}

/* Defined in persist_payload.c — drops L1 overlay binds from the runtime
 * and clears their tier stamps so the next encode walks only L2. */
extern void fr_persist_session_wipe_library_tier(fr_runtime_t *runtime);

/* Encode the runtime in full (both tiers) and commit it as the active
 * payload. Used by install-library at receipt (after wiping runtime L1, so
 * the encode covers only the surviving L2 state) and by the REPL compile
 * path after every L1 definition (so the new library word lands in durable
 * storage as it is typed — D3's "subsequent definitions are compiled,
 * installed, and persisted to NVS with tier tag L1"). */
fr_err_t fr_persist_save_full(fr_runtime_t *runtime) {
  FR_TRY(fr_persist_check_prompt_only(runtime));
  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  fr_persist_forget_boot_image();
  FR_TRY(fr_persist_stream_commit(runtime, NULL, 0, false));
  return fr_persist_remount(runtime, false);
}

/* SPEC D10: receiving install-library drops L1 from the device — runtime
 * binds and the L1 closure in durable storage — before accepting the next
 * definitions. The runtime wipe restores L1-stamped overlay slots and
 * compacts their names; the full save then commits a payload that reflects
 * the post-wipe runtime (L2 only at this point — definitions arriving after
 * receipt land in storage via the REPL compile path's own save_full call). */
fr_err_t fr_persist_install_library(fr_runtime_t *runtime) {
  FR_TRY(fr_persist_check_prompt_only(runtime));
  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  fr_persist_session_wipe_library_tier(runtime);
  return fr_persist_save_full(runtime);
}

uint16_t fr_persist_debug_last_payload_bytes(void) {
  return fr_persist_last_payload_bytes;
}

uint32_t fr_persist_debug_profile_hash(void) {
  return fr_profile_hash();
}
