#pragma once

#include "froth_ffi.h"
#include "froth_types.h"
#include "frothy_value.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *name;
  froth_cell_t value;
} frothy_board_pin_t;

typedef enum {
  FROTHY_FFI_VALUE_VOID = 0,
  FROTHY_FFI_VALUE_INT,
  FROTHY_FFI_VALUE_BOOL,
  FROTHY_FFI_VALUE_NIL,
  FROTHY_FFI_VALUE_TEXT,
  FROTHY_FFI_VALUE_CELLS,
} frothy_ffi_value_type_t;

typedef struct {
  const char *name;
  frothy_ffi_value_type_t type;
} frothy_ffi_param_t;

typedef struct {
  froth_error_t code;
  const char *kind;
  const char *origin;
  const char *detail;
} frothy_ffi_error_info_t;

typedef struct {
  const char *name;
  const frothy_ffi_param_t *params;
  uint8_t param_count;
  uint8_t arity;
  frothy_ffi_value_type_t result_type;
  const char *help;
  uint32_t flags;
  frothy_native_fn_t callback;
  const void *context;
  const char *stack_effect;
} frothy_ffi_entry_t;

#define FROTHY_FFI_FLAG_NONE 0u

#define FROTHY_FFI_PARAM_INT(name) { (name), FROTHY_FFI_VALUE_INT }
#define FROTHY_FFI_PARAM_BOOL(name) { (name), FROTHY_FFI_VALUE_BOOL }
#define FROTHY_FFI_PARAM_NIL(name) { (name), FROTHY_FFI_VALUE_NIL }
#define FROTHY_FFI_PARAM_TEXT(name) { (name), FROTHY_FFI_VALUE_TEXT }
#define FROTHY_FFI_PARAM_CELLS(name) { (name), FROTHY_FFI_VALUE_CELLS }
#define FROTHY_FFI_PARAM_COUNT(params)                                        \
  ((uint8_t)(sizeof(params) / sizeof((params)[0])))

#define FROTHY_FFI_DECLARE(name) extern const frothy_ffi_entry_t name[]
#define FROTHY_FFI_TABLE_BEGIN(name) const frothy_ffi_entry_t name[] = {
#define FROTHY_FFI_TABLE_END {0} }

froth_error_t frothy_ffi_expect_int(const frothy_value_t *args, size_t arg_index,
                                    int32_t *out);
froth_error_t frothy_ffi_expect_bool(const frothy_value_t *args,
                                     size_t arg_index, bool *out);
froth_error_t frothy_ffi_expect_nil(const frothy_value_t *args,
                                    size_t arg_index);
froth_error_t frothy_ffi_expect_text(frothy_runtime_t *runtime,
                                     const frothy_value_t *args,
                                     size_t arg_index, const char **text_out,
                                     size_t *length_out);
froth_error_t frothy_ffi_expect_cells(frothy_runtime_t *runtime,
                                      const frothy_value_t *args,
                                      size_t arg_index, size_t *length_out,
                                      froth_cell_t *base_out);

froth_error_t frothy_ffi_return_int(int32_t value, frothy_value_t *out);
froth_error_t frothy_ffi_return_bool(bool value, frothy_value_t *out);
froth_error_t frothy_ffi_return_nil(frothy_value_t *out);
froth_error_t frothy_ffi_return_text(frothy_runtime_t *runtime, const char *text,
                                     size_t length, frothy_value_t *out);
froth_error_t frothy_ffi_return_cells(frothy_runtime_t *runtime, size_t length,
                                      frothy_value_t *out);

froth_error_t frothy_ffi_raise(frothy_runtime_t *runtime, froth_error_t code,
                               const char *kind, const char *origin,
                               const char *detail);
void frothy_ffi_clear_last_error(frothy_runtime_t *runtime);
void frothy_ffi_get_last_error(const frothy_runtime_t *runtime,
                               frothy_ffi_error_info_t *out);

froth_error_t frothy_ffi_install_table(const frothy_ffi_entry_t *table);
froth_error_t frothy_ffi_install_binding_table(const froth_ffi_entry_t *table);
froth_error_t frothy_ffi_install_pin_table(const frothy_board_pin_t *pins);
froth_error_t frothy_ffi_install_board_base_slots(void);
bool frothy_ffi_native_is_foreign(frothy_native_fn_t fn, const void *context);
const char *frothy_ffi_native_owner(frothy_native_fn_t fn, const void *context);
const char *frothy_ffi_native_effect(frothy_native_fn_t fn, const void *context);
const char *frothy_ffi_native_help(frothy_native_fn_t fn, const void *context);
froth_cell_t frothy_ffi_wrap_uptime_ms(uint32_t uptime_ms);
