#pragma once

#include "froth_types.h"
#include "froth_vm.h"
#include <stdint.h>

/* FFI binding metadata. Used by both kernel primitives and external FFI tables.
 * Null-terminated arrays of these are passed to froth_ffi_register(). */
typedef struct {
  const char *name;
  froth_native_word_t word;
  const char *stack_effect;
  uint8_t in_arity;
  uint8_t out_arity;
  const char *help;
} froth_ffi_entry_t;

/* --- Public API for FFI authors (ADR-019) --- */

froth_error_t froth_pop(froth_vm_t *vm, froth_cell_t *value);
froth_error_t froth_pop_bstring(froth_vm_t *vm, const uint8_t **data,
                                froth_cell_t *len);
froth_error_t froth_push_bstring(froth_vm_t *vm, const uint8_t *data,
                                 froth_cell_t len);
froth_error_t froth_pop_tagged(froth_vm_t *vm, froth_cell_t *payload,
                               froth_cell_tag_t *tag);
froth_error_t froth_push(froth_vm_t *vm, froth_cell_t value);
froth_error_t froth_throw(froth_vm_t *vm, froth_cell_t error_code);

/* --- Registration --- */

froth_error_t froth_ffi_register(froth_vm_t *vm,
                                 const froth_ffi_entry_t *table);

/* --- Lookup --- */

const froth_ffi_entry_t *froth_ffi_find_entry(froth_native_word_t prim);

/* --- Convenience macros --- */

#define FROTH_FFI_ARITY_UNKNOWN FROTH_ARITY_UNKNOWN

/* Declare an FFI function + companion metadata struct.
 * Use FROTH_FFI_ARITY for fixed-arity bindings.
 * FROTH_FFI keeps unknown arity for the rare dynamic case.
 * The VM pointer is always named `froth_vm`, which FROTH_POP/FROTH_PUSH depend
 * on. */
#define FROTH_FFI_ARITY(fn_name, name_str, effect, in_count, out_count,        \
                        help_str)                                               \
  static froth_error_t fn_name(froth_vm_t *froth_vm);                          \
  static const froth_ffi_entry_t fn_name##_entry = {                           \
      .name = name_str,                                                        \
      .word = fn_name,                                                         \
      .stack_effect = effect,                                                  \
      .in_arity = (in_count),                                                  \
      .out_arity = (out_count),                                                \
      .help = help_str,                                                        \
  };                                                                           \
  static froth_error_t fn_name(froth_vm_t *froth_vm)

#define FROTH_FFI(fn_name, name_str, effect, help_str)                         \
  FROTH_FFI_ARITY(fn_name, name_str, effect, FROTH_FFI_ARITY_UNKNOWN,          \
                  FROTH_FFI_ARITY_UNKNOWN, help_str)

/* Pop a number into a new local variable. Requires froth_vm in scope. */
#define FROTH_POP(into)                                                        \
  froth_cell_t into;                                                           \
  FROTH_TRY(froth_pop(froth_vm, &into))

/* Push a number onto DS. Requires froth_vm in scope. */
#define FROTH_PUSH(value) FROTH_TRY(froth_push(froth_vm, (value)))

/* Reference the metadata struct created by FROTH_FFI for use in a binding
 * table. */
#define FROTH_BIND(fn_name) fn_name##_entry

/* Board binding table macros. */
#define FROTH_BOARD_DECLARE(name) extern const froth_ffi_entry_t name[]
#define FROTH_BOARD_BEGIN(name) const froth_ffi_entry_t name[] = {
#define FROTH_BOARD_END           {0} }                                        \
  ;
