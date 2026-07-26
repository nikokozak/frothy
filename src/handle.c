#include "handle.h"

#include "platform.h"
#include "runtime.h"

#if FR_FEATURE_HANDLES
static const char *const fr_handle_kind_names[FR_HANDLE_KIND_COUNT] = {
    "none", "uart", "pwm", "i2c-bus", "i2c-device",
    "spi",  "tcp",  "trace", "pulse", "ble-connection",
};
typedef char
    fr_handle_kind_names_match_count[(sizeof(fr_handle_kind_names) /
                                      sizeof(fr_handle_kind_names[0])) ==
                                             FR_HANDLE_KIND_COUNT
                                         ? 1
                                         : -1];

static void fr_handle_clear_entry(fr_handle_entry_t *entry) {
  entry->platform_index = FR_HANDLE_PLATFORM_NONE;
  entry->kind = FR_HANDLE_KIND_NONE;
}

static bool fr_handle_kind_is_known(fr_handle_kind_t kind) {
  return kind > FR_HANDLE_KIND_NONE && kind < FR_HANDLE_KIND_COUNT;
}

/* A hold names the live entry by id and generation, so a stale ref left in
 * a slot after its handle was closed matches nothing and holds nothing. */
static bool fr_handle_entry_is_held(const fr_handle_entry_t *entry,
                                    fr_handle_id_t id,
                                    const fr_handle_hold_t *held,
                                    uint8_t held_count) {
  if (held == NULL || entry->kind == FR_HANDLE_KIND_NONE ||
      entry->platform_index == FR_HANDLE_PLATFORM_NONE) {
    return false;
  }
  for (uint8_t i = 0; i < held_count; i++) {
    fr_handle_ref_t ref = {0};

    if (fr_tagged_decode_handle_ref(held[i].value, &ref) == FR_OK &&
        ref.id == id && ref.generation == entry->generation) {
      return true;
    }
  }
  return false;
}
#endif

const char *fr_handle_kind_name(fr_handle_kind_t kind) {
#if FR_FEATURE_HANDLES
  if (kind < FR_HANDLE_KIND_COUNT) {
    return fr_handle_kind_names[kind];
  }
#else
  (void)kind;
#endif
  return "unknown";
}

void fr_handle_reset(fr_runtime_t *runtime) {
  if (runtime == NULL) {
    return;
  }

#if FR_FEATURE_HANDLES
  runtime->held_handles = NULL;
  runtime->held_handle_count = 0;
  for (fr_handle_id_t i = 0; i < FR_PROFILE_MAX_HANDLES; i++) {
    fr_handle_clear_entry(&runtime->handles.entries[i]);
    runtime->handles.entries[i].generation = 0;
    runtime->handles.entries[i].retired = false;
  }
#else
  (void)runtime;
#endif
}

void fr_handle_close_all(fr_runtime_t *runtime) {
  if (runtime == NULL) {
    return;
  }

#if FR_FEATURE_HANDLES
  const fr_handle_hold_t *held = runtime->held_handles;
  uint8_t held_count = runtime->held_handle_count;

  /* Take the hold before walking: it covers the one reset a tolerant save
   * remounts through, so the recovery reset after a failed apply finds no
   * hold and closes everything (ADR 0070). */
  runtime->held_handles = NULL;
  runtime->held_handle_count = 0;

  for (fr_handle_id_t i = 0; i < FR_PROFILE_MAX_HANDLES; i++) {
    fr_handle_entry_t *entry = &runtime->handles.entries[i];

    if (fr_handle_entry_is_held(entry, i, held, held_count)) {
      continue;
    }
    if (entry->kind != FR_HANDLE_KIND_NONE &&
        entry->platform_index != FR_HANDLE_PLATFORM_NONE &&
        fr_platform_handle_close(entry->kind, entry->platform_index) !=
            FR_OK) {
      /* The platform kept ownership (ADR 0068): keep the entry so the
       * next bulk cleanup retries and open paths can find it. */
      continue;
    }
    fr_handle_clear_entry(entry);
  }
#endif
}

fr_err_t fr_handle_close_kind(fr_runtime_t *runtime, fr_handle_kind_t kind) {
#if FR_FEATURE_HANDLES
  fr_err_t first_error = FR_OK;

  if (runtime == NULL || !fr_handle_kind_is_known(kind)) {
    return FR_ERR_INVALID;
  }
  for (fr_handle_id_t i = 0; i < FR_PROFILE_MAX_HANDLES; i++) {
    fr_handle_entry_t *entry = &runtime->handles.entries[i];

    if (entry->kind != kind) {
      continue;
    }
    if (entry->platform_index != FR_HANDLE_PLATFORM_NONE) {
      fr_err_t err = fr_platform_handle_close(kind, entry->platform_index);
      if (err != FR_OK) {
        /* Same preservation rule as fr_handle_close_all. */
        if (first_error == FR_OK) {
          first_error = err;
        }
        continue;
      }
    }
    fr_handle_clear_entry(entry);
  }
  return first_error;
#else
  (void)runtime;
  (void)kind;
  return FR_ERR_UNSUPPORTED;
#endif
}

void fr_handle_forget_kind(fr_runtime_t *runtime, fr_handle_kind_t kind) {
#if FR_FEATURE_HANDLES
  if (runtime == NULL || !fr_handle_kind_is_known(kind)) {
    return;
  }
  for (fr_handle_id_t i = 0; i < FR_PROFILE_MAX_HANDLES; i++) {
    fr_handle_entry_t *entry = &runtime->handles.entries[i];

    if (entry->kind == kind) {
      fr_handle_clear_entry(entry);
    }
  }
#else
  (void)runtime;
  (void)kind;
#endif
}

fr_err_t fr_handle_reserve(fr_runtime_t *runtime, fr_handle_kind_t kind,
                           fr_handle_ref_t *out_ref,
                           fr_tagged_t *out_tagged) {
#if FR_FEATURE_HANDLES
  if (runtime == NULL || out_ref == NULL || out_tagged == NULL ||
      !fr_handle_kind_is_known(kind)) {
    return FR_ERR_INVALID;
  }

  for (fr_handle_id_t i = 0; i < FR_PROFILE_MAX_HANDLES; i++) {
    fr_handle_entry_t *entry = &runtime->handles.entries[i];

    if (entry->kind != FR_HANDLE_KIND_NONE || entry->retired) {
      continue;
    }
    if (entry->generation == 0x0fu) {
      entry->retired = true;
      continue;
    }

    entry->generation = (fr_handle_generation_t)(entry->generation + 1u);
    entry->kind = kind;
    entry->platform_index = FR_HANDLE_PLATFORM_NONE;
    *out_ref = (fr_handle_ref_t){.id = i, .generation = entry->generation};
    FR_TRY(fr_tagged_encode_handle_ref(*out_ref, out_tagged));
    return FR_OK;
  }
  return FR_ERR_CAPACITY;
#else
  (void)runtime;
  (void)kind;
  (void)out_ref;
  (void)out_tagged;
  return FR_ERR_UNSUPPORTED;
#endif
}

fr_err_t fr_handle_activate(fr_runtime_t *runtime, fr_handle_ref_t ref,
                            uint16_t platform_index) {
#if FR_FEATURE_HANDLES
  fr_handle_entry_t *entry = NULL;

  if (runtime == NULL || platform_index == FR_HANDLE_PLATFORM_NONE) {
    return FR_ERR_INVALID;
  }
  if (ref.id >= FR_PROFILE_MAX_HANDLES) {
    return FR_ERR_HANDLE;
  }

  entry = &runtime->handles.entries[ref.id];
  if (entry->kind == FR_HANDLE_KIND_NONE ||
      entry->generation != ref.generation) {
    return FR_ERR_HANDLE;
  }
  if (entry->platform_index != FR_HANDLE_PLATFORM_NONE) {
    return FR_ERR_INVALID;
  }
  /* One live entry per (kind, platform_index): reverse lookup (ADR 0067)
   * must never have to guess between two owners of one platform slot. */
  for (fr_handle_id_t i = 0; i < FR_PROFILE_MAX_HANDLES; i++) {
    const fr_handle_entry_t *other = &runtime->handles.entries[i];

    if (i != ref.id && other->kind == entry->kind &&
        other->platform_index == platform_index) {
      return FR_ERR_INVALID;
    }
  }

  entry->platform_index = platform_index;
  return FR_OK;
#else
  (void)runtime;
  (void)ref;
  (void)platform_index;
  return FR_ERR_UNSUPPORTED;
#endif
}

fr_err_t fr_handle_release_reserved(fr_runtime_t *runtime,
                                    fr_handle_ref_t ref) {
#if FR_FEATURE_HANDLES
  fr_handle_entry_t *entry = NULL;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  if (ref.id >= FR_PROFILE_MAX_HANDLES) {
    return FR_ERR_HANDLE;
  }

  entry = &runtime->handles.entries[ref.id];
  if (entry->kind == FR_HANDLE_KIND_NONE ||
      entry->generation != ref.generation) {
    return FR_ERR_HANDLE;
  }
  if (entry->platform_index != FR_HANDLE_PLATFORM_NONE) {
    return FR_ERR_INVALID;
  }

  /* A reservation released before activation never handed its tagged ref
   * to user code (the open failed inside the native), so the generation
   * rolls back instead of burning. Without this, repeatedly reopening a
   * busy resource retires table entries one failed attempt at a time
   * until the whole table is dead until reboot. */
  entry->generation = (fr_handle_generation_t)(entry->generation - 1u);
  fr_handle_clear_entry(entry);
  return FR_OK;
#else
  (void)runtime;
  (void)ref;
  return FR_ERR_UNSUPPORTED;
#endif
}

fr_err_t fr_handle_lookup(const fr_runtime_t *runtime, fr_handle_ref_t ref,
                          fr_handle_kind_t expected_kind,
                          fr_handle_kind_t *out_kind,
                          uint16_t *out_platform_index) {
#if FR_FEATURE_HANDLES
  const fr_handle_entry_t *entry = NULL;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  if (ref.id >= FR_PROFILE_MAX_HANDLES) {
    return FR_ERR_HANDLE;
  }

  entry = &runtime->handles.entries[ref.id];
  if (entry->kind == FR_HANDLE_KIND_NONE ||
      entry->generation != ref.generation ||
      entry->platform_index == FR_HANDLE_PLATFORM_NONE) {
    return FR_ERR_HANDLE;
  }
  if (expected_kind != FR_HANDLE_KIND_NONE && entry->kind != expected_kind) {
    return FR_ERR_TYPE;
  }

  if (out_kind != NULL) {
    *out_kind = entry->kind;
  }
  if (out_platform_index != NULL) {
    *out_platform_index = entry->platform_index;
  }
  return FR_OK;
#else
  (void)runtime;
  (void)ref;
  (void)expected_kind;
  (void)out_kind;
  (void)out_platform_index;
  return FR_ERR_UNSUPPORTED;
#endif
}

fr_err_t fr_handle_find_active(const fr_runtime_t *runtime,
                               fr_handle_kind_t kind, uint16_t platform_index,
                               fr_tagged_t *out_tagged) {
#if FR_FEATURE_HANDLES
  if (runtime == NULL || out_tagged == NULL ||
      !fr_handle_kind_is_known(kind) ||
      platform_index == FR_HANDLE_PLATFORM_NONE) {
    return FR_ERR_INVALID;
  }
  for (fr_handle_id_t i = 0; i < FR_PROFILE_MAX_HANDLES; i++) {
    const fr_handle_entry_t *entry = &runtime->handles.entries[i];
    fr_handle_ref_t ref;

    if (entry->kind != kind || entry->retired ||
        entry->platform_index != platform_index) {
      continue;
    }
    ref = (fr_handle_ref_t){.id = i, .generation = entry->generation};
    return fr_tagged_encode_handle_ref(ref, out_tagged);
  }
  return FR_ERR_NOT_FOUND;
#else
  (void)runtime;
  (void)kind;
  (void)platform_index;
  (void)out_tagged;
  return FR_ERR_UNSUPPORTED;
#endif
}

fr_err_t fr_handle_close(fr_runtime_t *runtime, fr_handle_ref_t ref) {
#if FR_FEATURE_HANDLES
  fr_handle_entry_t *entry = NULL;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  if (ref.id >= FR_PROFILE_MAX_HANDLES) {
    return FR_ERR_HANDLE;
  }

  entry = &runtime->handles.entries[ref.id];
  if (entry->kind == FR_HANDLE_KIND_NONE ||
      entry->generation != ref.generation ||
      entry->platform_index == FR_HANDLE_PLATFORM_NONE) {
    return FR_ERR_HANDLE;
  }

  FR_TRY(fr_platform_handle_close(entry->kind, entry->platform_index));
  fr_handle_clear_entry(entry);
  return FR_OK;
#else
  (void)runtime;
  (void)ref;
  return FR_ERR_UNSUPPORTED;
#endif
}
