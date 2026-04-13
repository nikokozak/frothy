#include "frothy_base_image.h"

#include "froth_slot_table.h"
#include "froth_vm.h"
#include "frothy_ffi.h"
#include "frothy_inspect.h"
#include "frothy_snapshot.h"
#include "frothy_value.h"

#include <string.h>

typedef struct {
  const char *name;
  frothy_native_fn_t fn;
  size_t arity;
  uint8_t flags;
} frothy_base_slot_t;

enum {
  FROTHY_BASE_SLOT_EMITS_OUTPUT = 1u << 0,
  FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT = 1u << 1,
};

static const frothy_base_slot_t frothy_base_slots[] = {
    {"save", frothy_builtin_save, 0,
     FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT},
    {"restore", frothy_builtin_restore, 0,
     FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT},
    {"wipe", frothy_builtin_wipe, 0,
     FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT},
    {"words", frothy_builtin_words, 0,
     FROTHY_BASE_SLOT_EMITS_OUTPUT |
         FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT},
    {"see", frothy_builtin_see, 1,
     FROTHY_BASE_SLOT_EMITS_OUTPUT |
         FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT},
    {"core", frothy_builtin_core, 1,
     FROTHY_BASE_SLOT_EMITS_OUTPUT |
         FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT},
    {"slotInfo", frothy_builtin_slot_info, 1,
     FROTHY_BASE_SLOT_EMITS_OUTPUT |
         FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT},
    {"boot", NULL, 0, 0},
};

static frothy_runtime_t *frothy_base_runtime(void) {
  return &froth_vm.frothy_runtime;
}

static const frothy_base_slot_t *
frothy_base_image_find_slot(const char *name) {
  size_t i;

  for (i = 0; i < sizeof(frothy_base_slots) / sizeof(frothy_base_slots[0]);
       i++) {
    if (strcmp(frothy_base_slots[i].name, name) == 0) {
      return &frothy_base_slots[i];
    }
  }

  return NULL;
}

bool frothy_base_image_has_slot(const char *name) {
  if (frothy_ffi_is_base_slot_name(name)) {
    return true;
  }

  if (frothy_base_image_find_slot(name) != NULL) {
    return true;
  }

  return false;
}

bool frothy_base_image_builtin_emits_output(const char *name) {
  const frothy_base_slot_t *slot = frothy_base_image_find_slot(name);

  return slot != NULL && (slot->flags & FROTHY_BASE_SLOT_EMITS_OUTPUT) != 0;
}

bool frothy_base_image_shell_suppresses_raw_output(const char *name) {
  const frothy_base_slot_t *slot = frothy_base_image_find_slot(name);

  return slot != NULL &&
         (slot->flags & FROTHY_BASE_SLOT_SHELL_SUPPRESSES_RAW_OUTPUT) != 0;
}

froth_error_t frothy_base_image_install(void) {
  size_t i;

  for (i = 0; i < sizeof(frothy_base_slots) / sizeof(frothy_base_slots[0]);
       i++) {
    froth_cell_u_t slot_index;

    FROTH_TRY(froth_slot_find_name_or_create(&froth_vm.heap,
                                             frothy_base_slots[i].name,
                                             &slot_index));
    FROTH_TRY(froth_slot_set_overlay(slot_index, 0));
    FROTH_TRY(froth_slot_clear_binding(slot_index));
    if (frothy_base_slots[i].fn != NULL) {
      frothy_value_t value = frothy_value_make_nil();

      FROTH_TRY(frothy_runtime_alloc_native(frothy_base_runtime(),
                                            frothy_base_slots[i].fn,
                                            frothy_base_slots[i].name,
                                            frothy_base_slots[i].arity, NULL,
                                            &value));
      FROTH_TRY(froth_slot_set_impl(slot_index, frothy_value_to_cell(value)));
      FROTH_TRY(
          froth_slot_set_arity(slot_index, (uint8_t)frothy_base_slots[i].arity, 1));
    }
  }

  return frothy_ffi_install_board_base_slots();
}

froth_error_t frothy_base_image_reset(void) {
  froth_cell_u_t slot_count = froth_slot_count();
  froth_cell_u_t slot_index;

  FROTH_TRY(frothy_runtime_clear_overlay_state(frothy_base_runtime()));

  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    const char *name = NULL;

    if (froth_slot_get_name(slot_index, &name) != FROTH_OK) {
      continue;
    }
    if (!frothy_base_image_has_slot(name)) {
      continue;
    }

    FROTH_TRY(froth_slot_set_overlay(slot_index, 0));
    FROTH_TRY(froth_slot_clear_binding(slot_index));
  }

  FROTH_TRY(froth_slot_reset_overlay());
  froth_vm.heap.pointer = froth_vm.watermark_heap_offset;
  return frothy_base_image_install();
}
