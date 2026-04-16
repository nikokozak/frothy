#include "froth_ffi.h"
#include "froth_slot_table.h"
#include "froth_tbuf.h"
#include "froth_vm.h"
#include "frothy_eval.h"
#include "frothy_ffi.h"
#include "frothy_ffi.h"
#include "frothy_parser.h"
#include "frothy_value.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static frothy_runtime_t *runtime(void) {
  return &froth_vm.frothy_runtime;
}

static void release_value(frothy_value_t *value) {
  (void)frothy_value_release(runtime(), *value);
  *value = frothy_value_make_nil();
}

static void reset_frothy_state(void) {
  frothy_runtime_free(runtime());
  (void)froth_slot_reset_overlay();
  froth_vm.ds.pointer = 0;
  froth_vm.rs.pointer = 0;
  froth_vm.cs.pointer = 0;
  froth_vm.heap.pointer = 0;
  froth_vm.boot_complete = 1;
  froth_vm.trampoline_depth = 0;
  froth_vm.interrupted = 0;
  froth_vm.thrown = FROTH_OK;
  froth_vm.last_error_slot = -1;
  froth_vm.mark_offset = (froth_cell_u_t)-1;
  froth_cellspace_init(&froth_vm.cellspace);
  froth_tbuf_init(&froth_vm);
  frothy_runtime_init(runtime(), &froth_vm.cellspace);
}

static int eval_source(const char *source, frothy_value_t *out,
                       froth_error_t *error_out) {
  frothy_ir_program_t program;
  froth_error_t err;

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(source, &program);
  if (err == FROTH_OK) {
    err = frothy_eval_program(&program, out);
  }
  frothy_ir_program_free(&program);
  *error_out = err;
  return err == FROTH_OK;
}

static int expect_ok(const char *source, frothy_value_t *out) {
  froth_error_t err;

  if (!eval_source(source, out, &err)) {
    fprintf(stderr, "eval failed for `%s`: %d\n", source, (int)err);
    return 0;
  }
  return 1;
}

static int expect_error(const char *source, froth_error_t expected) {
  frothy_value_t ignored = frothy_value_make_nil();
  froth_error_t err;

  if (eval_source(source, &ignored, &err)) {
    fprintf(stderr, "expected error %d for `%s`\n", (int)expected, source);
    release_value(&ignored);
    return 0;
  }
  if (err != expected) {
    fprintf(stderr, "expected error %d for `%s`, got %d\n", (int)expected,
            source, (int)err);
    return 0;
  }
  return 1;
}

static int expect_int_value(frothy_value_t value, int32_t expected,
                            const char *label) {
  if (!frothy_value_is_int(value)) {
    fprintf(stderr, "%s expected int\n", label);
    return 0;
  }
  if (frothy_value_as_int(value) != expected) {
    fprintf(stderr, "%s expected %d, got %d\n", label, expected,
            frothy_value_as_int(value));
    return 0;
  }
  return 1;
}

static int expect_bool_value(frothy_value_t value, bool expected,
                             const char *label) {
  if (!frothy_value_is_bool(value)) {
    fprintf(stderr, "%s expected bool\n", label);
    return 0;
  }
  if (frothy_value_as_bool(value) != expected) {
    fprintf(stderr, "%s expected %s\n", label, expected ? "true" : "false");
    return 0;
  }
  return 1;
}

static int expect_nil_value(frothy_value_t value, const char *label) {
  if (!frothy_value_is_nil(value)) {
    fprintf(stderr, "%s expected nil\n", label);
    return 0;
  }
  return 1;
}

static int expect_text_value(frothy_value_t value, const char *expected,
                             const char *label) {
  const char *text = NULL;
  size_t length = 0;

  if (frothy_runtime_get_text(runtime(), value, &text, &length) != FROTH_OK) {
    fprintf(stderr, "%s expected text\n", label);
    return 0;
  }
  if (strlen(expected) != length || memcmp(text, expected, length) != 0) {
    fprintf(stderr, "%s expected `%s`, got `%.*s`\n", label, expected,
            (int)length, text);
    return 0;
  }
  return 1;
}

static int expect_last_error(froth_error_t expected_code, const char *expected_kind,
                             const char *expected_origin,
                             const char *expected_detail, const char *label) {
  frothy_ffi_error_info_t info;

  frothy_ffi_get_last_error(runtime(), &info);
  if (info.code != expected_code) {
    fprintf(stderr, "%s expected error code %d, got %d\n", label,
            (int)expected_code, (int)info.code);
    return 0;
  }
  if (expected_kind != info.kind &&
      (expected_kind == NULL || info.kind == NULL ||
       strcmp(expected_kind, info.kind) != 0)) {
    fprintf(stderr, "%s expected error kind `%s`, got `%s`\n", label,
            expected_kind != NULL ? expected_kind : "(null)",
            info.kind != NULL ? info.kind : "(null)");
    return 0;
  }
  if (expected_origin != info.origin &&
      (expected_origin == NULL || info.origin == NULL ||
       strcmp(expected_origin, info.origin) != 0)) {
    fprintf(stderr, "%s expected error origin `%s`, got `%s`\n", label,
            expected_origin != NULL ? expected_origin : "(null)",
            info.origin != NULL ? info.origin : "(null)");
    return 0;
  }
  if (expected_detail != info.detail &&
      (expected_detail == NULL || info.detail == NULL ||
       strcmp(expected_detail, info.detail) != 0)) {
    fprintf(stderr, "%s expected error detail `%s`, got `%s`\n", label,
            expected_detail != NULL ? expected_detail : "(null)",
            info.detail != NULL ? info.detail : "(null)");
    return 0;
  }
  return 1;
}

static int expect_native_call(const char *name, const frothy_value_t *args,
                              size_t arg_count, frothy_value_t *out,
                              froth_error_t expected) {
  froth_cell_u_t slot_index = 0;
  froth_cell_t impl = 0;
  frothy_native_fn_t fn = NULL;
  const void *context = NULL;
  froth_error_t err;

  err = froth_slot_find_name(name, &slot_index);
  if (err != FROTH_OK) {
    fprintf(stderr, "missing slot `%s`: %d\n", name, (int)err);
    return 0;
  }

  err = froth_slot_get_impl(slot_index, &impl);
  if (err != FROTH_OK) {
    fprintf(stderr, "missing impl for `%s`: %d\n", name, (int)err);
    return 0;
  }

  err = frothy_runtime_get_native(runtime(), frothy_value_from_cell(impl), &fn,
                                  &context, NULL, NULL);
  if (err != FROTH_OK || fn == NULL) {
    fprintf(stderr, "missing native impl for `%s`: %d\n", name, (int)err);
    return 0;
  }

  err = fn(runtime(), context, args, arg_count, out);
  if (err != expected) {
    fprintf(stderr, "native `%s` expected %d, got %d\n", name, (int)expected,
            (int)err);
    return 0;
  }
  return 1;
}

FROTH_FFI_ARITY(test_echo_int, "echo.int", "( value -- value )", 1, 1,
                "Echo an integer argument") {
  FROTH_POP(value);
  FROTH_PUSH(value);
  return FROTH_OK;
}

FROTH_FFI_ARITY(test_echo_text, "echo.text", "( text -- text )", 1, 1,
                "Echo a text argument") {
  const uint8_t *data = NULL;
  froth_cell_t length = 0;

  FROTH_TRY(froth_pop_bstring(froth_vm, &data, &length));
  return froth_push_bstring(froth_vm, data, length);
}

FROTH_FFI_ARITY(test_touch_nil, "touch.nil", "( -- )", 0, 0,
                "Return no result") {
  return FROTH_OK;
}

FROTH_FFI_ARITY(test_bool_arg, "bool.arg", "( flag -- n )", 1, 1,
                "Observe Frothy bool coercion") {
  FROTH_POP(value);
  FROTH_PUSH(value);
  return FROTH_OK;
}

FROTH_FFI_ARITY(test_fail_push, "fail.push", "( value -- )", 1, 0,
                "Push then fail so the shim must unwind DS") {
  FROTH_POP(value);
  FROTH_PUSH(value);
  return FROTH_ERROR_IO;
}

FROTH_FFI_ARITY(test_force_zero, "echo.int", "( value -- 0 )", 1, 1,
                "Return zero for rollback verification") {
  FROTH_POP(value);
  FROTH_PUSH(0);
  return FROTH_OK;
}

static const froth_ffi_entry_t test_bindings[] = {
    FROTH_BIND(test_echo_int),  FROTH_BIND(test_echo_text),
    FROTH_BIND(test_touch_nil), FROTH_BIND(test_bool_arg),
    FROTH_BIND(test_fail_push), {0},
};

static const frothy_ffi_param_t maintained_echo_int_params[] = {
    FROTHY_FFI_PARAM_INT("value"),
};

static const frothy_ffi_param_t maintained_echo_text_params[] = {
    FROTHY_FFI_PARAM_TEXT("text"),
};

static const frothy_ffi_param_t maintained_not_bool_params[] = {
    FROTHY_FFI_PARAM_BOOL("flag"),
};

static const frothy_ffi_param_t maintained_make_cells_params[] = {
    FROTHY_FFI_PARAM_INT("length"),
};

static const frothy_ffi_param_t maintained_cells_len_params[] = {
    FROTHY_FFI_PARAM_CELLS("cells"),
};

static froth_error_t maintained_echo_int(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t value = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &value));
  return frothy_ffi_return_int(value, out);
}

static froth_error_t maintained_echo_text(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  const char *text = NULL;
  size_t length = 0;

  (void)context;
  (void)arg_count;
  FROTH_TRY(frothy_ffi_expect_text(runtime, args, 0, &text, &length));
  return frothy_ffi_return_text(runtime, text, length, out);
}

static froth_error_t maintained_not_bool(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  bool flag = false;

  (void)runtime;
  (void)context;
  (void)arg_count;
  FROTH_TRY(frothy_ffi_expect_bool(args, 0, &flag));
  return frothy_ffi_return_bool(!flag, out);
}

static froth_error_t maintained_make_cells(frothy_runtime_t *runtime,
                                           const void *context,
                                           const frothy_value_t *args,
                                           size_t arg_count,
                                           frothy_value_t *out) {
  int32_t length = 0;
  froth_cell_t base = 0;
  int32_t i;

  (void)context;
  (void)arg_count;
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &length));
  if (length <= 0) {
    return frothy_ffi_raise(runtime, FROTH_ERROR_BOUNDS, "ffi-test",
                            "maint.make.cells",
                            "length must be positive");
  }

  FROTH_TRY(frothy_ffi_return_cells(runtime, (size_t)length, out));
  FROTH_TRY(frothy_runtime_get_cells(runtime, *out, NULL, &base));
  for (i = 0; i < length; i++) {
    frothy_value_t value = frothy_value_make_nil();

    FROTH_TRY(frothy_value_make_int(i + 1, &value));
    FROTH_TRY(
        froth_cellspace_store(runtime->cellspace, base + i,
                              frothy_value_to_cell(value)));
  }

  return FROTH_OK;
}

static froth_error_t maintained_cells_len(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  size_t length = 0;

  (void)context;
  (void)arg_count;
  FROTH_TRY(frothy_ffi_expect_cells(runtime, args, 0, &length, NULL));
  return frothy_ffi_return_int((int32_t)length, out);
}

static froth_error_t maintained_raise_error(frothy_runtime_t *runtime,
                                            const void *context,
                                            const frothy_value_t *args,
                                            size_t arg_count,
                                            frothy_value_t *out) {
  (void)context;
  (void)args;
  (void)arg_count;
  (void)out;

  return frothy_ffi_raise(runtime, FROTH_ERROR_IO, "ffi-test",
                          "maint.raise.error", "synthetic failure");
}

static froth_error_t maintained_raise_temporary_error(
    frothy_runtime_t *runtime, const void *context, const frothy_value_t *args,
    size_t arg_count, frothy_value_t *out) {
  char kind[16];
  char origin[32];
  char detail[32];

  (void)context;
  (void)args;
  (void)arg_count;
  (void)out;

  snprintf(kind, sizeof(kind), "%s", "ffi-temp");
  snprintf(origin, sizeof(origin), "%s", "maint.temp.error");
  snprintf(detail, sizeof(detail), "%s", "temporary detail");
  return frothy_ffi_raise(runtime, FROTH_ERROR_IO, kind, origin, detail);
}

static froth_error_t maintained_bad_result(frothy_runtime_t *runtime,
                                           const void *context,
                                           const frothy_value_t *args,
                                           size_t arg_count,
                                           frothy_value_t *out) {
  (void)context;
  (void)args;
  (void)arg_count;

  return frothy_ffi_return_text(runtime, "oops", 4, out);
}

static froth_error_t maintained_force_zero(frothy_runtime_t *runtime,
                                           const void *context,
                                           const frothy_value_t *args,
                                           size_t arg_count,
                                           frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  return frothy_ffi_return_int(0, out);
}

static const frothy_ffi_entry_t maintained_bindings[] = {
    {
        .name = "maint.echo.int",
        .params = maintained_echo_int_params,
        .param_count = FROTHY_FFI_PARAM_COUNT(maintained_echo_int_params),
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_INT,
        .help = "Echo a Frothy int through the maintained FFI path.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_echo_int,
        .stack_effect = "( value -- value )",
    },
    {
        .name = "maint.echo.text",
        .params = maintained_echo_text_params,
        .param_count = FROTHY_FFI_PARAM_COUNT(maintained_echo_text_params),
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_TEXT,
        .help = "Echo a Frothy text value through the maintained FFI path.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_echo_text,
        .stack_effect = "( text -- text )",
    },
    {
        .name = "maint.not.bool",
        .params = maintained_not_bool_params,
        .param_count = FROTHY_FFI_PARAM_COUNT(maintained_not_bool_params),
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_BOOL,
        .help = "Invert a Frothy bool through the maintained FFI path.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_not_bool,
        .stack_effect = "( flag -- flag )",
    },
    {
        .name = "maint.make.cells",
        .params = maintained_make_cells_params,
        .param_count = FROTHY_FFI_PARAM_COUNT(maintained_make_cells_params),
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_CELLS,
        .help = "Allocate a cells object with ascending ints.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_make_cells,
        .stack_effect = "( length -- cells )",
    },
    {
        .name = "maint.cells.len",
        .params = maintained_cells_len_params,
        .param_count = FROTHY_FFI_PARAM_COUNT(maintained_cells_len_params),
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_INT,
        .help = "Return the length of a Frothy cells object.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_cells_len,
        .stack_effect = "( cells -- length )",
    },
    {
        .name = "maint.raise.error",
        .arity = 0,
        .result_type = FROTHY_FFI_VALUE_VOID,
        .help = "Raise a structured maintained FFI error.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_raise_error,
        .stack_effect = "( -- )",
    },
    {
        .name = "maint.raise.temp.error",
        .arity = 0,
        .result_type = FROTHY_FFI_VALUE_VOID,
        .help = "Raise a temporary-string maintained FFI error.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_raise_temporary_error,
        .stack_effect = "( -- )",
    },
    {
        .name = "maint.bad.result",
        .arity = 0,
        .result_type = FROTHY_FFI_VALUE_INT,
        .help = "Return the wrong result kind to test cleanup.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_bad_result,
        .stack_effect = "( -- value )",
    },
    {0},
};

static const frothy_ffi_entry_t malformed_missing_params_bindings[] = {
    {
        .name = "maint.bad.params",
        .param_count = 1,
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_INT,
        .help = "Malformed maintained binding with missing params.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_echo_int,
        .stack_effect = "( value -- value )",
    },
    {0},
};

static const frothy_ffi_entry_t maintained_partial_failure_bindings[] = {
    {
        .name = "maint.echo.int",
        .params = maintained_echo_int_params,
        .param_count = FROTHY_FFI_PARAM_COUNT(maintained_echo_int_params),
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_INT,
        .help = "Override maintained echo for rollback verification.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_force_zero,
        .stack_effect = "( value -- 0 )",
    },
    {
        .name = "maint.bad.params",
        .param_count = 1,
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_INT,
        .help = "Malformed maintained binding with missing params.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = maintained_echo_int,
        .stack_effect = "( value -- value )",
    },
    {0},
};

static const froth_ffi_entry_t legacy_partial_failure_bindings[] = {
    FROTH_BIND(test_force_zero),
    {
        .name = "legacy.bad",
        .stack_effect = "( value -- value )",
        .in_arity = 1,
        .out_arity = 1,
        .help = "Malformed legacy binding with missing word.",
    },
    {0},
};

static int install_test_bindings(void) {
  froth_error_t err = frothy_ffi_install_binding_table(test_bindings);

  if (err != FROTH_OK) {
    fprintf(stderr, "failed to install synthetic FFI table: %d\n", (int)err);
    return 0;
  }
  return 1;
}

static int install_maintained_bindings(void) {
  froth_error_t err = frothy_ffi_install_table(maintained_bindings);

  if (err != FROTH_OK) {
    fprintf(stderr, "failed to install maintained FFI table: %d\n", (int)err);
    return 0;
  }
  return 1;
}

static int install_board_base_slots(void) {
  froth_error_t err = frothy_ffi_install_board_base_slots();

  if (err != FROTH_OK) {
    fprintf(stderr, "failed to install board base slots: %d\n", (int)err);
    return 0;
  }
  return 1;
}

static int test_int_round_trip(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_test_bindings();
  ok &= expect_ok("echo.int: 7", &value);
  ok &= expect_int_value(value, 7, "echo.int: 7");
  release_value(&value);
  return ok;
}

static int test_text_round_trip(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_test_bindings();
  ok &= expect_ok("echo.text: \"frothy\"", &value);
  ok &= expect_text_value(value, "frothy", "echo.text");
  release_value(&value);
  return ok;
}

static int test_nil_no_return(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_test_bindings();
  ok &= expect_ok("touch.nil:", &value);
  ok &= expect_nil_value(value, "touch.nil:");
  release_value(&value);
  return ok;
}

static int test_maintained_int_round_trip(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  ok &= expect_ok("maint.echo.int: 7", &value);
  ok &= expect_int_value(value, 7, "maint.echo.int: 7");
  release_value(&value);
  return ok;
}

static int test_maintained_text_round_trip(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  ok &= expect_ok("maint.echo.text: \"frothy\"", &value);
  ok &= expect_text_value(value, "frothy", "maint.echo.text");
  release_value(&value);
  return ok;
}

static int test_maintained_bool_round_trip(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  ok &= expect_ok("maint.not.bool: true", &value);
  ok &= expect_bool_value(value, false, "maint.not.bool: true");
  release_value(&value);
  ok &= expect_ok("maint.not.bool: false", &value);
  ok &= expect_bool_value(value, true, "maint.not.bool: false");
  release_value(&value);
  return ok;
}

static int test_bool_argument_coercion(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_test_bindings();
  ok &= expect_ok("bool.arg: true", &value);
  ok &= expect_int_value(value, -1, "bool.arg: true");
  release_value(&value);
  ok &= expect_ok("bool.arg: false", &value);
  ok &= expect_int_value(value, 0, "bool.arg: false");
  release_value(&value);
  return ok;
}

static int test_maintained_cells_round_trip(void) {
  frothy_value_t length_arg = frothy_value_make_nil();
  frothy_value_t cells = frothy_value_make_nil();
  frothy_value_t out = frothy_value_make_nil();
  size_t length = 0;
  froth_cell_t base = 0;
  froth_cell_t cell = 0;
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  ok &= frothy_value_make_int(3, &length_arg) == FROTH_OK;
  ok &= expect_native_call("maint.make.cells", &length_arg, 1, &cells,
                           FROTH_OK);
  ok &= frothy_runtime_get_cells(runtime(), cells, &length, &base) == FROTH_OK;
  if (ok && length != 3) {
    fprintf(stderr, "maint.make.cells expected length 3, got %zu\n", length);
    ok = 0;
  }
  if (ok) {
    ok &= froth_cellspace_fetch(runtime()->cellspace, base + 0, &cell) ==
          FROTH_OK;
    ok &= expect_int_value(frothy_value_from_cell(cell), 1, "cells[0]");
    ok &= froth_cellspace_fetch(runtime()->cellspace, base + 2, &cell) ==
          FROTH_OK;
    ok &= expect_int_value(frothy_value_from_cell(cell), 3, "cells[2]");
  }
  ok &= expect_native_call("maint.cells.len", &cells, 1, &out, FROTH_OK);
  ok &= expect_int_value(out, 3, "maint.cells.len");
  release_value(&length_arg);
  release_value(&cells);
  release_value(&out);
  return ok;
}

static int test_arity_error(void) {
  frothy_value_t arg = frothy_value_make_nil();
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_test_bindings();
  ok &= frothy_value_make_int(1, &arg) == FROTH_OK;
  ok &= expect_error("echo.int:", FROTH_ERROR_SIGNATURE);
  ok &= expect_native_call("touch.nil", &arg, 1, &out, FROTH_ERROR_SIGNATURE);
  return ok;
}

static int test_maintained_rejections(void) {
  frothy_value_t arg = frothy_value_make_nil();
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  ok &= expect_error("maint.echo.int:", FROTH_ERROR_SIGNATURE);
  ok &= frothy_value_make_int(1, &arg) == FROTH_OK;
  ok &= expect_native_call("maint.not.bool", &arg, 1, &out,
                           FROTH_ERROR_TYPE_MISMATCH);
  ok &= expect_last_error(FROTH_OK, NULL, NULL, NULL,
                          "maintained rejection should not leave a raised error");
  release_value(&arg);
  release_value(&out);
  return ok;
}

static int test_type_mismatch(void) {
  frothy_value_t cells = frothy_value_make_nil();
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_test_bindings();
  ok &= frothy_runtime_alloc_cells(runtime(), 1, &cells) == FROTH_OK;
  ok &= expect_native_call("echo.int", &cells, 1, &out,
                           FROTH_ERROR_TYPE_MISMATCH);
  release_value(&cells);
  return ok;
}

static int test_maintained_structured_error(void) {
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  ok &= expect_native_call("maint.raise.error", NULL, 0, &out, FROTH_ERROR_IO);
  ok &= expect_last_error(FROTH_ERROR_IO, "ffi-test", "maint.raise.error",
                          "synthetic failure",
                          "maint.raise.error should publish structured detail");
  ok &= expect_ok("maint.echo.int: 5", &out);
  ok &= expect_int_value(out, 5, "maint.echo.int: 5");
  ok &= expect_last_error(FROTH_OK, NULL, NULL, NULL,
                          "maintained success should clear the last error");
  release_value(&out);
  return ok;
}

static int test_maintained_structured_error_copies_text(void) {
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  ok &= expect_native_call("maint.raise.temp.error", NULL, 0, &out,
                           FROTH_ERROR_IO);
  ok &= expect_last_error(FROTH_ERROR_IO, "ffi-temp", "maint.temp.error",
                          "temporary detail",
                          "maint.raise.temp.error should copy transient text");
  release_value(&out);
  return ok;
}

static int test_maintained_reinstall_is_idempotent(void) {
  froth_error_t err = FROTH_OK;
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  err = frothy_ffi_install_table(maintained_bindings);
  if (err != FROTH_OK) {
    fprintf(stderr, "expected maintained reinstall to succeed, got %d\n",
            (int)err);
    ok = 0;
  }
  ok &= expect_ok("maint.echo.int: 9", &out);
  ok &= expect_int_value(out, 9, "maint.echo.int: 9");
  release_value(&out);
  return ok;
}

static int test_maintained_return_helpers_validate_out(void) {
  frothy_value_t out = frothy_value_make_nil();
  const char *text = NULL;
  size_t length = 0;
  int ok = 1;

  reset_frothy_state();
  ok &= frothy_ffi_return_int(1, NULL) == FROTH_ERROR_BOUNDS;
  ok &= frothy_ffi_return_text(runtime(), "x", 1, NULL) == FROTH_ERROR_BOUNDS;
  ok &= frothy_ffi_return_cells(runtime(), 1, NULL) == FROTH_ERROR_BOUNDS;
  ok &= frothy_ffi_return_text(runtime(), "", (size_t)-2, &out) ==
        FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  ok &= frothy_ffi_return_int(1, &out) == FROTH_OK;
  release_value(&out);
  ok &= frothy_ffi_return_text(runtime(), NULL, 0, &out) == FROTH_OK;
  ok &= expect_text_value(out, "", "empty ffi text");
  release_value(&out);
  ok &= frothy_ffi_return_text(runtime(), "x", 1, &out) == FROTH_OK;
  release_value(&out);
  ok &= frothy_ffi_return_cells(runtime(), 1, &out) == FROTH_OK;
  release_value(&out);
  ok &= frothy_runtime_alloc_text(runtime(), NULL, 0, &out) == FROTH_OK;
  ok &= expect_text_value(out, "", "empty runtime text");
  release_value(&out);
  ok &= frothy_runtime_alloc_text(runtime(), NULL, 1, &out) ==
        FROTH_ERROR_BOUNDS;
  ok &= frothy_runtime_alloc_text(runtime(), "", (size_t)-2, &out) ==
        FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  ok &= frothy_runtime_alloc_text(runtime(), "", (size_t)-1, &out) ==
        FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  ok &= frothy_runtime_alloc_cells(NULL, 1, &out) == FROTH_ERROR_BOUNDS;
  ok &= frothy_runtime_alloc_cells(runtime(), 1, NULL) == FROTH_ERROR_BOUNDS;
  ok &= frothy_runtime_get_text(NULL, out, &text, &length) ==
        FROTH_ERROR_BOUNDS;
  ok &= frothy_runtime_get_text(runtime(), out, NULL, &length) ==
        FROTH_ERROR_BOUNDS;
  ok &= frothy_runtime_get_cells(NULL, out, &length, NULL) ==
        FROTH_ERROR_BOUNDS;
  ok &= frothy_runtime_alloc_native(NULL, maintained_echo_int, "temp", 1, NULL,
                                    &out) == FROTH_ERROR_BOUNDS;
  return ok;
}

static int test_reinstall_failure_preserves_existing_slots(void) {
  froth_error_t err = FROTH_OK;
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  ok &= expect_ok("maint.echo.int: 9", &out);
  ok &= expect_int_value(out, 9, "maint.echo.int: 9");
  release_value(&out);
  frothy_runtime_test_fail_next_append(runtime());
  err = frothy_ffi_install_table(maintained_bindings);
  if (err != FROTH_ERROR_HEAP_OUT_OF_MEMORY) {
    fprintf(stderr, "expected maintained reinstall failure to report %d, got %d\n",
            (int)FROTH_ERROR_HEAP_OUT_OF_MEMORY, (int)err);
    ok = 0;
  }
  ok &= expect_ok("maint.echo.int: 11", &out);
  ok &= expect_int_value(out, 11, "maint.echo.int: 11");
  release_value(&out);

  reset_frothy_state();
  ok &= install_test_bindings();
  ok &= expect_ok("echo.int: 5", &out);
  ok &= expect_int_value(out, 5, "echo.int: 5");
  release_value(&out);
  frothy_runtime_test_fail_next_append(runtime());
  err = frothy_ffi_install_binding_table(test_bindings);
  if (err != FROTH_ERROR_HEAP_OUT_OF_MEMORY) {
    fprintf(stderr, "expected legacy reinstall failure to report %d, got %d\n",
            (int)FROTH_ERROR_HEAP_OUT_OF_MEMORY, (int)err);
    ok = 0;
  }
  ok &= expect_ok("echo.int: 13", &out);
  ok &= expect_int_value(out, 13, "echo.int: 13");
  release_value(&out);
  return ok;
}

static int test_failed_install_does_not_partially_rebind_existing_slot(void) {
  froth_error_t err = FROTH_OK;
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  err = frothy_ffi_install_table(maintained_partial_failure_bindings);
  if (err != FROTH_ERROR_SIGNATURE) {
    fprintf(stderr, "expected maintained partial install to fail with %d, got %d\n",
            (int)FROTH_ERROR_SIGNATURE, (int)err);
    ok = 0;
  }
  ok &= expect_ok("maint.echo.int: 21", &out);
  ok &= expect_int_value(out, 21, "maintained install rollback should preserve echo");
  release_value(&out);

  reset_frothy_state();
  ok &= install_test_bindings();
  err = frothy_ffi_install_binding_table(legacy_partial_failure_bindings);
  if (err != FROTH_ERROR_SIGNATURE) {
    fprintf(stderr, "expected legacy partial install to fail with %d, got %d\n",
            (int)FROTH_ERROR_SIGNATURE, (int)err);
    ok = 0;
  }
  ok &= expect_ok("echo.int: 34", &out);
  ok &= expect_int_value(out, 34, "legacy install rollback should preserve echo");
  release_value(&out);
  return ok;
}

static int test_maintained_install_rejects_missing_params(void) {
  froth_error_t err = FROTH_OK;
  int ok = 1;

  reset_frothy_state();
  err = frothy_ffi_install_table(malformed_missing_params_bindings);
  if (err != FROTH_ERROR_SIGNATURE) {
    fprintf(stderr, "expected malformed maintained install to fail with %d, got %d\n",
            (int)FROTH_ERROR_SIGNATURE, (int)err);
    ok = 0;
  }
  return ok;
}

static int test_maintained_result_validation_releases_output(void) {
  frothy_value_t out = frothy_value_make_nil();
  size_t base_live_objects = 0;
  int ok = 1;

  reset_frothy_state();
  ok &= install_maintained_bindings();
  base_live_objects = frothy_runtime_live_object_count(runtime());
  ok &= expect_native_call("maint.bad.result", NULL, 0, &out,
                           FROTH_ERROR_TYPE_MISMATCH);
  if (ok && frothy_runtime_live_object_count(runtime()) != base_live_objects) {
    fprintf(stderr, "maint.bad.result should not leak a live object on type mismatch\n");
    ok = 0;
  }
  ok &= expect_nil_value(out, "maint.bad.result should leave nil output");
  release_value(&out);
  return ok;
}

static int test_stack_cleanup_after_native_failure(void) {
  frothy_value_t arg = frothy_value_make_nil();
  frothy_value_t out = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_test_bindings();
  ok &= frothy_value_make_int(99, &arg) == FROTH_OK;
  froth_vm.ds.data[0] = 1234;
  froth_vm.ds.pointer = 1;
  froth_vm.thrown = 777;
  ok &= expect_native_call("fail.push", &arg, 1, &out, FROTH_ERROR_IO);
  if (froth_vm.ds.pointer != 1 || froth_vm.ds.data[0] != 1234) {
    fprintf(stderr, "fail.push did not restore data stack depth\n");
    ok = 0;
  }
  if (froth_vm.thrown != 777) {
    fprintf(stderr, "fail.push did not restore thrown state\n");
    ok = 0;
  }
  return ok;
}

static int test_board_millis_monotonic(void) {
  frothy_value_t start = frothy_value_make_nil();
  frothy_value_t after = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_board_base_slots();
  ok &= expect_ok("millis:", &start);
  ok &= frothy_value_is_int(start);
  ok &= expect_ok("ms: 20", &after);
  ok &= expect_nil_value(after, "ms: 20");
  release_value(&after);
  ok &= expect_ok("millis:", &after);
  ok &= frothy_value_is_int(after);
  if (ok && frothy_value_as_int(after) <= frothy_value_as_int(start)) {
    fprintf(stderr, "millis: should advance across ms: 20\n");
    ok = 0;
  }
  release_value(&start);
  release_value(&after);
  return ok;
}

static int test_board_gpio_read_round_trip(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_board_base_slots();
  ok &= expect_ok("gpio.mode: LED_BUILTIN, 1", &value);
  ok &= expect_nil_value(value, "gpio.mode");
  release_value(&value);
  ok &= expect_ok("gpio.write: LED_BUILTIN, 1", &value);
  ok &= expect_nil_value(value, "gpio.write high");
  release_value(&value);
  ok &= expect_ok("gpio.read: LED_BUILTIN", &value);
  ok &= expect_int_value(value, 1, "gpio.read after high");
  release_value(&value);
  ok &= expect_ok("gpio.write: LED_BUILTIN, 0", &value);
  ok &= expect_nil_value(value, "gpio.write low");
  release_value(&value);
  ok &= expect_ok("gpio.read: LED_BUILTIN", &value);
  ok &= expect_int_value(value, 0, "gpio.read after low");
  release_value(&value);
  return ok;
}

static int test_board_workshop_parity_guards(void) {
  frothy_value_t value = frothy_value_make_nil();
  frothy_value_t delay = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_board_base_slots();
  ok &= expect_error("gpio.mode: 99, 1", FROTH_ERROR_BOUNDS);
  ok &= expect_error("gpio.write: 99, 1", FROTH_ERROR_BOUNDS);
  ok &= expect_error("gpio.read: 99", FROTH_ERROR_BOUNDS);
  ok &= expect_error("adc.read: 99", FROTH_ERROR_BOUNDS);
  ok &= expect_ok("ms: -1", &value);
  ok &= expect_nil_value(value, "ms: -1");
  release_value(&value);

  ok &= frothy_value_make_int(20, &delay) == FROTH_OK;
  if (ok) {
    froth_vm.interrupted = 1;
    ok &= expect_native_call("ms", &delay, 1, &value,
                             FROTH_ERROR_PROGRAM_INTERRUPTED);
    froth_vm.interrupted = 0;
  }
  release_value(&delay);
  release_value(&value);
  return ok;
}

static int test_board_embedded_tool_surface(void) {
  frothy_value_t first = frothy_value_make_nil();
  frothy_value_t second = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  ok &= install_board_base_slots();

  ok &= expect_ok("random.seed!: 1234", &first);
  ok &= expect_nil_value(first, "random.seed!");
  release_value(&first);
  ok &= expect_ok("random.next:", &first);
  ok &= frothy_value_is_int(first);
  ok &= expect_ok("random.seed!: 1234", &second);
  ok &= expect_nil_value(second, "random.seed! reset");
  release_value(&second);
  ok &= expect_ok("random.next:", &second);
  ok &= frothy_value_is_int(second);
  if (ok && frothy_value_as_int(first) != frothy_value_as_int(second)) {
    fprintf(stderr, "random.next should repeat after reseeding with the same value\n");
    ok = 0;
  }
  release_value(&first);
  release_value(&second);

  ok &= expect_ok("random.seed!: 99", &first);
  ok &= expect_nil_value(first, "random.seed!: 99");
  release_value(&first);
  ok &= expect_ok("random.below: 10", &first);
  ok &= frothy_value_is_int(first);
  if (ok && (frothy_value_as_int(first) < 0 || frothy_value_as_int(first) >= 10)) {
    fprintf(stderr, "random.below should stay within [0, 10)\n");
    ok = 0;
  }
  release_value(&first);
  ok &= expect_ok("random.range: 7, 3", &second);
  ok &= frothy_value_is_int(second);
  if (ok && (frothy_value_as_int(second) < 3 ||
             frothy_value_as_int(second) > 7)) {
    fprintf(stderr, "random.range should stay within [3, 7]\n");
    ok = 0;
  }
  release_value(&second);
  ok &= expect_error("random.below: 0", FROTH_ERROR_BOUNDS);
  return ok;
}

static int test_wrap_uptime_ms_payload(void) {
  froth_cell_t wrapped = frothy_ffi_wrap_uptime_ms(UINT32_C(0xffffffff));
  froth_cell_t round_trip = 0;
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();
  if (froth_push(&froth_vm, wrapped) != FROTH_OK) {
    fprintf(stderr, "wrapped uptime should fit the Froth payload range\n");
    return 0;
  }
  if (froth_pop(&froth_vm, &round_trip) != FROTH_OK) {
    fprintf(stderr, "wrapped uptime should round-trip through the Froth stack\n");
    return 0;
  }
  if (round_trip != wrapped) {
    fprintf(stderr, "wrapped uptime should preserve its payload value\n");
    ok = 0;
  }
  if (frothy_value_make_int((int32_t)wrapped, &value) != FROTH_OK) {
    fprintf(stderr, "wrapped uptime should fit the Frothy int range\n");
    ok = 0;
  }
  release_value(&value);
  return ok;
}

int main(void) {
  int ok = 1;

  ok &= test_int_round_trip();
  ok &= test_text_round_trip();
  ok &= test_nil_no_return();
  ok &= test_maintained_int_round_trip();
  ok &= test_maintained_text_round_trip();
  ok &= test_maintained_bool_round_trip();
  ok &= test_bool_argument_coercion();
  ok &= test_maintained_cells_round_trip();
  ok &= test_arity_error();
  ok &= test_maintained_rejections();
  ok &= test_type_mismatch();
  ok &= test_maintained_structured_error();
  ok &= test_maintained_structured_error_copies_text();
  ok &= test_maintained_reinstall_is_idempotent();
  ok &= test_maintained_return_helpers_validate_out();
  ok &= test_reinstall_failure_preserves_existing_slots();
  ok &= test_failed_install_does_not_partially_rebind_existing_slot();
  ok &= test_maintained_install_rejects_missing_params();
  ok &= test_maintained_result_validation_releases_output();
  ok &= test_stack_cleanup_after_native_failure();
  ok &= test_board_millis_monotonic();
  ok &= test_board_gpio_read_round_trip();
  ok &= test_board_workshop_parity_guards();
  ok &= test_board_embedded_tool_surface();
  ok &= test_wrap_uptime_ms_payload();

  frothy_runtime_free(runtime());
  return ok ? 0 : 1;
}
