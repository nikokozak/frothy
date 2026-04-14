#include "froth_ffi.h"
#include "froth_slot_table.h"
#include "froth_tbuf.h"
#include "froth_vm.h"
#include "frothy_eval.h"
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

static const froth_ffi_entry_t test_bindings[] = {
    FROTH_BIND(test_echo_int),  FROTH_BIND(test_echo_text),
    FROTH_BIND(test_touch_nil), FROTH_BIND(test_bool_arg),
    FROTH_BIND(test_fail_push), {0},
};

static int install_test_bindings(void) {
  froth_error_t err = frothy_ffi_install_binding_table(test_bindings);

  if (err != FROTH_OK) {
    fprintf(stderr, "failed to install synthetic FFI table: %d\n", (int)err);
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
  ok &= test_bool_argument_coercion();
  ok &= test_arity_error();
  ok &= test_type_mismatch();
  ok &= test_stack_cleanup_after_native_failure();
  ok &= test_board_millis_monotonic();
  ok &= test_board_gpio_read_round_trip();
  ok &= test_wrap_uptime_ms_payload();

  frothy_runtime_free(runtime());
  return ok ? 0 : 1;
}
