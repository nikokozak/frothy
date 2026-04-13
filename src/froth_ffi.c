#include "froth_ffi.h"
#include "froth_slot_table.h"
#include "froth_stack.h"
#include "froth_tbuf.h"
#include "froth_types.h"
#include <stddef.h>
#include <stdint.h>

#ifndef FROTH_FFI_MAX_TABLES
#define FROTH_FFI_MAX_TABLES 8
#endif
static const froth_ffi_entry_t *registered_tables[FROTH_FFI_MAX_TABLES];
static froth_cell_u_t registered_table_count = 0;

/* Pop a number from DS. Returns stripped payload. Type-checked: non-numbers are
 * rejected. */
froth_error_t froth_pop(froth_vm_t *vm, froth_cell_t *value) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&vm->ds, &cell));
  if (!FROTH_CELL_IS_NUMBER(cell)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  *value = FROTH_CELL_STRIP_TAG(cell);
  return FROTH_OK;
}

froth_error_t froth_pop_bstring(froth_vm_t *vm, const uint8_t **data,
                                froth_cell_t *len) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&vm->ds, &cell));
  if (!FROTH_CELL_IS_BSTRING(cell))
    return FROTH_ERROR_TYPE_MISMATCH;

  froth_bstring_view_t bstring;
  FROTH_TRY(froth_bstring_resolve(vm, cell, &bstring));

  *data = bstring.data;
  *len = bstring.len;
  return FROTH_OK;
}

froth_error_t froth_push_bstring(froth_vm_t *vm, const uint8_t *data,
                                 froth_cell_t len) {
  if (len < 0 || len > FROTH_STRING_MAX_LEN)
    return FROTH_ERROR_BSTRING_TOO_LONG;
  froth_cell_t cell;
  FROTH_TRY(froth_tbuf_alloc(vm, data, len, &cell));
  return froth_stack_push(&vm->ds, cell);
}

/* Pop any cell from DS. Returns stripped payload and tag enum separately. */
froth_error_t froth_pop_tagged(froth_vm_t *vm, froth_cell_t *payload,
                               froth_cell_tag_t *tag) {
  froth_cell_t cell;
  FROTH_TRY(froth_stack_pop(&vm->ds, &cell));
  *payload = FROTH_CELL_STRIP_TAG(cell);
  *tag = FROTH_CELL_GET_TAG(cell);
  return FROTH_OK;
}

/* Push a number onto DS. Tags as FROTH_NUMBER internally.
 * Returns FROTH_ERROR_VALUE_OVERFLOW if value exceeds payload range. */
froth_error_t froth_push(froth_vm_t *vm, froth_cell_t value) {
  froth_cell_t cell;
  FROTH_TRY(froth_make_cell(value, FROTH_NUMBER, &cell));
  FROTH_TRY(froth_stack_push(&vm->ds, cell));
  return FROTH_OK;
}

/* Signal an error from an FFI binding. Sets vm->thrown and returns the
 * FROTH_ERROR_THROW sentinel for propagation via FROTH_TRY / catch. */
froth_error_t froth_throw(froth_vm_t *vm, froth_cell_t error_code) {
  vm->thrown = error_code;
  return FROTH_ERROR_THROW;
}

/* Register a null-terminated table of FFI bindings into the slot table. */
froth_error_t froth_ffi_register(froth_vm_t *vm,
                                 const froth_ffi_entry_t *table) {
  if (registered_table_count >= FROTH_FFI_MAX_TABLES) {
    return FROTH_ERROR_FFI_TABLE_FULL;
  }
  for (froth_cell_u_t i = 0; table[i].name != NULL; i++) {
    froth_cell_u_t existing;
    if (froth_slot_find_name(table[i].name, &existing) == FROTH_OK) {
      return FROTH_ERROR_REDEF_PRIMITIVE;
    }
    froth_cell_u_t slot_index;
    FROTH_TRY(froth_slot_create(table[i].name, &vm->heap, &slot_index));
    FROTH_TRY(froth_slot_set_prim(slot_index, table[i].word));
    if (table[i].in_arity != FROTH_FFI_ARITY_UNKNOWN &&
        table[i].out_arity != FROTH_FFI_ARITY_UNKNOWN) {
      FROTH_TRY(
          froth_slot_set_arity(slot_index, table[i].in_arity, table[i].out_arity));
    }
  }
  registered_tables[registered_table_count++] = table;
  return FROTH_OK;
}

/* Find FFI metadata for a primitive by function pointer. Walks all registered
 * tables. */
const froth_ffi_entry_t *froth_ffi_find_entry(froth_native_word_t prim) {
  for (froth_cell_u_t t = 0; t < registered_table_count; t++) {
    const froth_ffi_entry_t *table = registered_tables[t];
    for (froth_cell_u_t i = 0; table[i].name != NULL; i++) {
      if (table[i].word == prim)
        return &table[i];
    }
  }
  return NULL;
}
