#include "frothy_base_image.h"

#include "froth_slot_table.h"
#include "froth_vm.h"
#include "frothy_eval.h"
#include "frothy_ffi.h"
#include "frothy_ir.h"
#include "frothy_inspect.h"
#include "frothy_parser.h"
#include "frothy_snapshot.h"
#include "frothy_value.h"
#include "platform.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#ifdef FROTHY_HAS_BOARD_BASE_LIB
#include "frothy_board_base_lib.h"
#endif

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
    {"dangerous.wipe", frothy_builtin_wipe, 0,
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

static const char *frothy_base_registry[FROTH_SLOT_TABLE_SIZE];
static size_t frothy_base_registry_count = 0;

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

static void frothy_base_image_clear_registry(void) {
  frothy_base_registry_count = 0;
}

static bool frothy_base_image_registry_has(const char *name) {
  size_t i;

  if (name == NULL) {
    return false;
  }

  for (i = 0; i < frothy_base_registry_count; i++) {
    if (strcmp(frothy_base_registry[i], name) == 0) {
      return true;
    }
  }

  return false;
}

static froth_error_t frothy_base_image_register_name(const char *name) {
  if (name == NULL || frothy_base_image_registry_has(name)) {
    return FROTH_OK;
  }
  if (frothy_base_registry_count >=
      sizeof(frothy_base_registry) / sizeof(frothy_base_registry[0])) {
    return FROTH_ERROR_BOUNDS;
  }

  frothy_base_registry[frothy_base_registry_count++] = name;
  return FROTH_OK;
}

static froth_error_t frothy_base_image_capture_bound_slots(void) {
  froth_cell_u_t slot_count = froth_slot_count();
  froth_cell_u_t slot_index;

  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    const char *name = NULL;
    froth_cell_t impl = 0;
    froth_native_word_t prim = NULL;

    if (froth_slot_is_overlay(slot_index)) {
      continue;
    }
    if (froth_slot_get_name(slot_index, &name) != FROTH_OK || name == NULL) {
      continue;
    }
    if (froth_slot_get_impl(slot_index, &impl) != FROTH_OK &&
        froth_slot_get_prim(slot_index, &prim) != FROTH_OK) {
      continue;
    }
    FROTH_TRY(frothy_base_image_register_name(name));
  }

  return FROTH_OK;
}

bool frothy_base_image_has_slot(const char *name) {
  return frothy_base_image_registry_has(name);
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

static froth_error_t frothy_base_image_eval_form_as_base(const char *source,
                                                         size_t *consumed_out) {
  frothy_ir_program_t program;
  frothy_value_t value = frothy_value_make_nil();
  froth_error_t err = FROTH_OK;

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level_prefix(source, consumed_out, &program);
  if (err == FROTH_OK) {
    err = frothy_eval_program(&program, &value);
  }
  if (err == FROTH_OK) {
    err = frothy_value_release(frothy_base_runtime(), value);
  }
  frothy_ir_program_free(&program);
  return err;
}

static froth_error_t frothy_base_image_eval_source_as_base(const char *source) {
  const char *cursor = source;
  uint8_t saved_boot_complete = froth_vm.boot_complete;
  froth_error_t err = FROTH_OK;

  froth_vm.boot_complete = 0;
  while (cursor != NULL && *cursor != '\0') {
    size_t consumed = 0;

    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
      cursor++;
    }
    if (*cursor == '\0') {
      break;
    }

    err = frothy_base_image_eval_form_as_base(cursor, &consumed);
    if (err != FROTH_OK) {
      break;
    }
    if (consumed == 0) {
      err = FROTH_ERROR_SIGNATURE;
      break;
    }
    cursor += consumed;
  }

  froth_vm.boot_complete = saved_boot_complete;
  return err;
}

froth_error_t frothy_base_image_install(void) {
  size_t i;

  frothy_base_image_clear_registry();
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
    FROTH_TRY(frothy_base_image_register_name(frothy_base_slots[i].name));
  }

  FROTH_TRY(frothy_ffi_install_board_base_slots());

#ifdef FROTHY_HAS_BOARD_BASE_LIB
  FROTH_TRY(frothy_base_image_eval_source_as_base(frothy_board_base_lib));
#endif

  FROTH_TRY(frothy_base_image_capture_bound_slots());

  return FROTH_OK;
}

froth_error_t frothy_base_image_reset(void) {
  size_t i;

  platform_reset_runtime_state();
  FROTH_TRY(frothy_runtime_clear_overlay_state(frothy_base_runtime()));

  for (i = 0; i < frothy_base_registry_count; i++) {
    froth_cell_u_t slot_index = 0;
    const char *name = frothy_base_registry[i];

    if (name == NULL || froth_slot_find_name(name, &slot_index) != FROTH_OK) {
      continue;
    }

    FROTH_TRY(froth_slot_set_overlay(slot_index, 0));
    FROTH_TRY(froth_slot_clear_binding(slot_index));
  }

  FROTH_TRY(froth_slot_reset_overlay());
  froth_vm.heap.pointer = froth_vm.watermark_heap_offset;
  return frothy_base_image_install();
}
