#include "frothy_ffi.h"

#include "ffi.h"
#include "froth_slot_table.h"
#include "froth_stack.h"
#include "froth_tbuf.h"
#include "froth_vm.h"
#include "frothy_value.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef FROTHY_HAS_BOARD_PINS
#include "frothy_board_pins.h"
#endif

static const char *const frothy_board_binding_names[] = {
    "gpio.mode", "gpio.write", "ms",
    "adc.read",  "uart.init",  "uart.write",
    "uart.read", NULL,
};

static const char *const frothy_board_pin_names[] = {
    "LED_BUILTIN",
    "UART_TX",
    "UART_RX",
    "A0",
    NULL,
};

static bool frothy_name_in_list(const char *name, const char *const *names) {
  size_t i;

  if (name == NULL) {
    return false;
  }

  for (i = 0; names[i] != NULL; i++) {
    if (strcmp(name, names[i]) == 0) {
      return true;
    }
  }

  return false;
}

static froth_error_t frothy_ffi_push_value(frothy_runtime_t *runtime,
                                           frothy_value_t value) {
  frothy_value_class_t value_class;
  const char *text = NULL;
  size_t length = 0;

  FROTH_TRY(frothy_value_class(runtime, value, &value_class));
  switch (value_class) {
  case FROTHY_VALUE_CLASS_INT:
    return froth_push(&froth_vm, frothy_value_as_int(value));
  case FROTHY_VALUE_CLASS_BOOL:
    return froth_push(&froth_vm, frothy_value_as_bool(value) ? -1 : 0);
  case FROTHY_VALUE_CLASS_NIL:
    return froth_push(&froth_vm, 0);
  case FROTHY_VALUE_CLASS_TEXT:
    FROTH_TRY(frothy_runtime_get_text(runtime, value, &text, &length));
    return froth_push_bstring(&froth_vm, (const uint8_t *)text,
                              (froth_cell_t)length);
  case FROTHY_VALUE_CLASS_CELLS:
  case FROTHY_VALUE_CLASS_CODE:
  case FROTHY_VALUE_CLASS_NATIVE:
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  return FROTH_ERROR_TYPE_MISMATCH;
}

static froth_error_t frothy_ffi_make_output_value(frothy_runtime_t *runtime,
                                                  froth_cell_t cell,
                                                  frothy_value_t *out) {
  froth_bstring_view_t text;

  if (FROTH_CELL_IS_NUMBER(cell)) {
    return frothy_value_make_int((int32_t)FROTH_CELL_STRIP_TAG(cell), out);
  }
  if (FROTH_CELL_IS_BSTRING(cell)) {
    FROTH_TRY(froth_bstring_resolve(&froth_vm, cell, &text));
    return frothy_runtime_alloc_text(runtime, (const char *)text.data,
                                     (size_t)text.len, out);
  }

  return FROTH_ERROR_TYPE_MISMATCH;
}

static froth_error_t frothy_ffi_dispatch(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  const froth_ffi_entry_t *entry = (const froth_ffi_entry_t *)context;
  froth_cell_u_t start_depth = froth_vm.ds.pointer;
  froth_cell_t start_thrown = froth_vm.thrown;
  froth_cell_t result_cell = 0;
  froth_error_t err = FROTH_OK;
  size_t i;

  if (entry == NULL || entry->word == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  if (entry->in_arity == FROTH_FFI_ARITY_UNKNOWN ||
      entry->out_arity == FROTH_FFI_ARITY_UNKNOWN) {
    return FROTH_ERROR_SIGNATURE;
  }
  if (arg_count != entry->in_arity) {
    return FROTH_ERROR_SIGNATURE;
  }

  for (i = 0; i < arg_count; i++) {
    err = frothy_ffi_push_value(runtime, args[i]);
    if (err != FROTH_OK) {
      goto cleanup;
    }
  }

  err = entry->word(&froth_vm);
  if (err == FROTH_ERROR_THROW) {
    err = (froth_error_t)froth_vm.thrown;
  }
  if (err != FROTH_OK) {
    goto cleanup;
  }

  if (entry->out_arity == 0) {
    *out = frothy_value_make_nil();
    goto cleanup;
  }
  if (entry->out_arity != 1) {
    err = FROTH_ERROR_SIGNATURE;
    goto cleanup;
  }

  err = froth_stack_pop(&froth_vm.ds, &result_cell);
  if (err != FROTH_OK) {
    goto cleanup;
  }
  err = frothy_ffi_make_output_value(runtime, result_cell, out);

cleanup:
  froth_vm.ds.pointer = start_depth;
  froth_vm.thrown = start_thrown;
  return err;
}

static froth_error_t frothy_ffi_bind_native(const froth_ffi_entry_t *entry) {
  frothy_value_t value = frothy_value_make_nil();
  froth_cell_u_t slot_index = 0;

  FROTH_TRY(
      froth_slot_find_name_or_create(&froth_vm.heap, entry->name, &slot_index));
  FROTH_TRY(froth_slot_set_overlay(slot_index, 0));
  FROTH_TRY(froth_slot_clear_binding(slot_index));
  FROTH_TRY(frothy_runtime_alloc_native(&froth_vm.frothy_runtime,
                                        frothy_ffi_dispatch, entry->name,
                                        entry->in_arity, entry, &value));
  FROTH_TRY(froth_slot_set_impl(slot_index, frothy_value_to_cell(value)));
  return froth_slot_set_arity(slot_index, entry->in_arity, 1);
}

static froth_error_t frothy_ffi_bind_pin(const frothy_board_pin_t *pin) {
  frothy_value_t value = frothy_value_make_nil();
  froth_cell_u_t slot_index = 0;

  FROTH_TRY(frothy_value_make_int((int32_t)pin->value, &value));
  FROTH_TRY(
      froth_slot_find_name_or_create(&froth_vm.heap, pin->name, &slot_index));
  FROTH_TRY(froth_slot_set_overlay(slot_index, 0));
  FROTH_TRY(froth_slot_clear_binding(slot_index));
  FROTH_TRY(froth_slot_set_impl(slot_index, frothy_value_to_cell(value)));
  return froth_slot_clear_arity(slot_index);
}

static froth_error_t
frothy_ffi_install_binding_table_filtered(const froth_ffi_entry_t *table,
                                          bool filter_board_surface) {
  size_t i;

  if (table == NULL) {
    return FROTH_OK;
  }

  for (i = 0; table[i].name != NULL; i++) {
    if (table[i].word == NULL) {
      continue;
    }
    if (filter_board_surface &&
        !frothy_name_in_list(table[i].name, frothy_board_binding_names)) {
      continue;
    }
    if (table[i].in_arity == FROTH_FFI_ARITY_UNKNOWN ||
        table[i].out_arity == FROTH_FFI_ARITY_UNKNOWN) {
      continue;
    }
    if (table[i].out_arity > 1) {
      continue;
    }
    FROTH_TRY(frothy_ffi_bind_native(&table[i]));
  }

  return FROTH_OK;
}

static froth_error_t
frothy_ffi_install_pin_table_filtered(const frothy_board_pin_t *pins,
                                      bool filter_board_surface) {
  size_t i;

  if (pins == NULL) {
    return FROTH_OK;
  }

  for (i = 0; pins[i].name != NULL; i++) {
    if (filter_board_surface &&
        !frothy_name_in_list(pins[i].name, frothy_board_pin_names)) {
      continue;
    }
    FROTH_TRY(frothy_ffi_bind_pin(&pins[i]));
  }

  return FROTH_OK;
}

froth_error_t frothy_ffi_install_binding_table(const froth_ffi_entry_t *table) {
  return frothy_ffi_install_binding_table_filtered(table, false);
}

froth_error_t frothy_ffi_install_pin_table(const frothy_board_pin_t *pins) {
  return frothy_ffi_install_pin_table_filtered(pins, false);
}

froth_error_t frothy_ffi_install_board_base_slots(void) {
#ifdef FROTHY_HAS_BOARD_PINS
  FROTH_TRY(
      frothy_ffi_install_pin_table_filtered(frothy_generated_board_pins, true));
#endif
  return frothy_ffi_install_binding_table_filtered(froth_board_bindings, true);
}

bool frothy_ffi_is_base_slot_name(const char *name) {
  return frothy_name_in_list(name, frothy_board_binding_names) ||
         frothy_name_in_list(name, frothy_board_pin_names);
}
