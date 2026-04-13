#include "froth_slot_table.h"
#include "froth_vm.h"
#include "frothy_eval.h"
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
  froth_vm.heap.pointer = 0;
  froth_vm.boot_complete = 1;
  froth_vm.trampoline_depth = 0;
  froth_vm.interrupted = 0;
  froth_vm.thrown = FROTH_OK;
  froth_vm.last_error_slot = -1;
  froth_cellspace_init(&froth_vm.cellspace);
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

static int expect_bool_value(frothy_value_t value, bool expected,
                             const char *label) {
  if (!frothy_value_is_bool(value)) {
    fprintf(stderr, "%s expected bool\n", label);
    return 0;
  }
  if (frothy_value_as_bool(value) != expected) {
    fprintf(stderr, "%s expected %s, got %s\n", label,
            expected ? "true" : "false",
            frothy_value_as_bool(value) ? "true" : "false");
    return 0;
  }
  return 1;
}

static int expect_live_objects(size_t expected, const char *label) {
  size_t live = frothy_runtime_live_object_count(runtime());

  if (live != expected) {
    fprintf(stderr, "%s expected %zu live objects, got %zu\n", label, expected,
            live);
    return 0;
  }
  return 1;
}

static int expect_eval_high_water(size_t expected, const char *label) {
  size_t high_water = frothy_runtime_eval_value_high_water(runtime());

  if (high_water != expected) {
    fprintf(stderr, "%s expected eval high-water %zu, got %zu\n", label,
            expected, high_water);
    return 0;
  }
  return 1;
}

static int test_function_calls_and_blocks(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("inc = fn(x) { x + 1 }", &value);
  release_value(&value);
  ok &= expect_ok("inc(41)", &value);
  ok &= expect_int_value(value, 42, "inc(41)");
  release_value(&value);

  ok &= expect_ok(
      "sumTo = fn(limit) { total = 0 i = 0 while i < limit { set total = total + i set i = i + 1 } total }",
      &value);
  release_value(&value);
  ok &= expect_ok("sumTo(4)", &value);
  ok &= expect_int_value(value, 6, "sumTo(4)");
  release_value(&value);

  ok &= expect_ok("onlySet = fn() { x = 1 }", &value);
  release_value(&value);
  ok &= expect_ok("onlySet()", &value);
  ok &= expect_nil_value(value, "onlySet()");
  release_value(&value);

  ok &= expect_error("inc()", FROTH_ERROR_SIGNATURE);
  ok &= expect_ok("count = 7", &value);
  release_value(&value);
  ok &= expect_error("count()", FROTH_ERROR_TYPE_MISMATCH);

  return ok;
}

static int test_eval_scratch_limits_and_nested_calls(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("pair = fn(a, b) { a }", &value);
  release_value(&value);
  ok &= expect_ok("wrap = fn(v) { pair(v, v + 1) }", &value);
  release_value(&value);
  ok &= expect_ok("chain = fn(v) { wrap(wrap(v)) }", &value);
  release_value(&value);

  frothy_runtime_debug_reset_high_water(runtime());
  ok &= expect_ok("pair(7, 8)", &value);
  ok &= expect_int_value(value, 7, "pair(7, 8)");
  release_value(&value);
  ok &= expect_eval_high_water(2, "pair eval scratch");

  frothy_runtime_debug_reset_high_water(runtime());
  ok &= expect_ok("pair(9, 10)", &value);
  ok &= expect_int_value(value, 9, "pair(9, 10)");
  release_value(&value);
  ok &= expect_eval_high_water(2, "pair eval scratch warmed");

  ok &= expect_ok("chain(5)", &value);
  ok &= expect_int_value(value, 5, "chain(5)");
  release_value(&value);

  frothy_runtime_test_set_eval_value_limit(runtime(), 2);
  ok &= expect_ok("pair(11, 12)", &value);
  ok &= expect_int_value(value, 11, "pair(11, 12) at limit");
  release_value(&value);

  frothy_runtime_test_set_eval_value_limit(runtime(), 1);
  ok &= expect_error("pair(13, 14)", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  frothy_runtime_test_set_eval_value_limit(runtime(), FROTHY_EVAL_VALUE_CAPACITY);

  return ok;
}

static int test_stable_rebinding_and_reclamation(void) {
  frothy_value_t value = frothy_value_make_nil();
  size_t live_before;
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("adder = fn(x) { x + 1 }", &value);
  release_value(&value);
  ok &= expect_ok("apply = fn(v) { adder(v) }", &value);
  release_value(&value);
  ok &= expect_ok("apply(10)", &value);
  ok &= expect_int_value(value, 11, "apply(10) before rebind");
  release_value(&value);

  live_before = frothy_runtime_live_object_count(runtime());
  ok &= expect_ok("adder = fn(x) { x + 2 }", &value);
  release_value(&value);
  ok &= expect_live_objects(live_before, "code slot rebind");
  ok &= expect_ok("apply(10)", &value);
  ok &= expect_int_value(value, 12, "apply(10) after rebind");
  release_value(&value);

  ok &= expect_ok("label = \"one\"", &value);
  release_value(&value);
  ok &= expect_ok("reader = fn() { label }", &value);
  release_value(&value);
  ok &= expect_ok("reader()", &value);
  ok &= expect_text_value(value, "one", "reader() before rebind");
  release_value(&value);

  live_before = frothy_runtime_live_object_count(runtime());
  ok &= expect_ok("label = \"two\"", &value);
  release_value(&value);
  ok &= expect_live_objects(live_before, "text slot rebind");
  ok &= expect_ok("reader()", &value);
  ok &= expect_text_value(value, "two", "reader() after rebind");
  release_value(&value);

  ok &= expect_ok("frame = cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("load = fn() { frame[0] }", &value);
  release_value(&value);
  ok &= expect_ok("setCell = fn(v) { set frame[0] = v }", &value);
  release_value(&value);
  ok &= expect_ok("setCell(7)", &value);
  ok &= expect_nil_value(value, "setCell(7)");
  release_value(&value);
  ok &= expect_ok("load()", &value);
  ok &= expect_int_value(value, 7, "load() before cells rebind");
  release_value(&value);

  live_before = frothy_runtime_live_object_count(runtime());
  ok &= expect_ok("frame = cells(1)", &value);
  release_value(&value);
  ok &= expect_live_objects(live_before, "cells slot rebind");
  ok &= expect_ok("load()", &value);
  ok &= expect_nil_value(value, "load() after cells rebind");
  release_value(&value);

  return ok;
}

static int test_cells_store_rules_and_overwrite_reclamation(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("frame = cells(2)", &value);
  release_value(&value);
  ok &= expect_live_objects(1, "cells allocation");

  ok &= expect_ok("poke = fn(v) { set frame[0] = v }", &value);
  release_value(&value);
  ok &= expect_ok("peek = fn() { frame[0] }", &value);
  release_value(&value);
  ok &= expect_live_objects(3, "cells helpers");

  ok &= expect_ok("poke(\"alpha\")", &value);
  ok &= expect_nil_value(value, "poke(alpha)");
  release_value(&value);
  ok &= expect_live_objects(4, "cells store first text");

  ok &= expect_ok("poke(\"beta\")", &value);
  ok &= expect_nil_value(value, "poke(beta)");
  release_value(&value);
  ok &= expect_live_objects(4, "cells overwrite text");
  ok &= expect_ok("peek()", &value);
  ok &= expect_text_value(value, "beta", "peek()");
  release_value(&value);

  ok &= expect_ok("touch = fn() { set frame[1] = true frame[1] }", &value);
  release_value(&value);
  ok &= expect_ok("touch()", &value);
  if (!frothy_value_is_bool(value) || !frothy_value_as_bool(value)) {
    fprintf(stderr, "touch() expected true\n");
    ok = 0;
  }
  release_value(&value);

  ok &= expect_ok("readFar = fn() { frame[2] }", &value);
  release_value(&value);
  ok &= expect_error("readFar()", FROTH_ERROR_BOUNDS);

  ok &= expect_ok("writeFar = fn(v) { set frame[2] = v }", &value);
  release_value(&value);
  ok &= expect_error("writeFar(9)", FROTH_ERROR_BOUNDS);

  ok &= expect_ok("factory = fn() { fn() { 1 } }", &value);
  release_value(&value);
  ok &= expect_ok("storeCode = fn() { set frame[0] = factory() }", &value);
  release_value(&value);
  ok &= expect_error("storeCode()", FROTH_ERROR_TYPE_MISMATCH);

  ok &= expect_ok("bad = fn() { set missing = 1 }", &value);
  release_value(&value);
  ok &= expect_error("bad()", FROTH_ERROR_UNDEFINED_WORD);

  return ok;
}

static int test_cells_sample_program(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("frame = cells(3)", &value);
  release_value(&value);
  ok &= expect_ok(
      "writeFrame = fn() { set frame[0] = 7 set frame[1] = false set frame[2] = \"ready\" }",
      &value);
  release_value(&value);
  ok &= expect_ok("count = fn() { frame[0] }", &value);
  release_value(&value);
  ok &= expect_ok("enabled = fn() { frame[1] }", &value);
  release_value(&value);
  ok &= expect_ok("label = fn() { frame[2] }", &value);
  release_value(&value);

  ok &= expect_ok("writeFrame()", &value);
  ok &= expect_nil_value(value, "writeFrame()");
  release_value(&value);

  ok &= expect_ok("count()", &value);
  ok &= expect_int_value(value, 7, "count()");
  release_value(&value);

  ok &= expect_ok("enabled()", &value);
  ok &= expect_bool_value(value, false, "enabled()");
  release_value(&value);

  ok &= expect_ok("label()", &value);
  ok &= expect_text_value(value, "ready", "label()");
  release_value(&value);

  return ok;
}

static int test_top_level_set_forms(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("unit = 1", &value);
  release_value(&value);
  ok &= expect_ok("set unit = 2", &value);
  ok &= expect_nil_value(value, "set unit = 2");
  release_value(&value);
  ok &= expect_ok("unit", &value);
  ok &= expect_int_value(value, 2, "unit");
  release_value(&value);

  ok &= expect_ok("frame = cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("set frame[0] = 7", &value);
  ok &= expect_nil_value(value, "set frame[0] = 7");
  release_value(&value);
  ok &= expect_ok("frame[0]", &value);
  ok &= expect_int_value(value, 7, "frame[0]");
  release_value(&value);

  return ok;
}

static int test_temporary_results_release_cleanly(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_live_objects(0, "fresh runtime");
  ok &= expect_ok("\"temp\"", &value);
  ok &= expect_text_value(value, "temp", "temporary text");
  ok &= expect_live_objects(1, "temporary text retained by caller");
  release_value(&value);
  ok &= expect_live_objects(0, "temporary text released");

  ok &= expect_ok("fn() { 1 }", &value);
  ok &= expect_live_objects(1, "temporary code retained by caller");
  release_value(&value);
  ok &= expect_live_objects(0, "temporary code released");

  return ok;
}

static int test_allocator_failure_cleanup(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  frothy_runtime_test_fail_next_append(runtime());
  ok &= expect_error("\"boom\"", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  ok &= expect_live_objects(0, "text append failure");

  frothy_runtime_test_fail_next_append(runtime());
  ok &= expect_error("fn() { 1 }", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  ok &= expect_live_objects(0, "code append failure");

  frothy_runtime_test_fail_next_append(runtime());
  ok &= expect_error("frame = cells(2)", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  ok &= expect_live_objects(0, "cells append failure");
  if (froth_vm.cellspace.used != 0) {
    fprintf(stderr, "cells append failure expected cellspace used 0, got %" FROTH_CELL_U_FORMAT "\n",
            froth_vm.cellspace.used);
    ok = 0;
  }

  reset_frothy_state();
  frothy_runtime_test_set_object_limit(runtime(), 1);
  ok &= expect_ok("held = \"one\"", &value);
  release_value(&value);
  ok &= expect_live_objects(1, "object limit seed");
  ok &= expect_error("\"two\"", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  ok &= expect_live_objects(1, "object limit cleanup");

  return ok;
}

static int test_object_slot_reuse_at_capacity(void) {
  frothy_value_t values[FROTHY_OBJECT_CAPACITY];
  frothy_value_t recycled = frothy_value_make_nil();
  size_t churn_round;
  size_t i;
  int ok = 1;

  reset_frothy_state();
  for (i = 0; i < FROTHY_OBJECT_CAPACITY; i++) {
    values[i] = frothy_value_make_nil();
    if (frothy_runtime_alloc_cells(runtime(), 1, &values[i]) != FROTH_OK) {
      fprintf(stderr, "alloc_cells failed at capacity fill index %zu\n", i);
      ok = 0;
      break;
    }
  }
  if (!ok) {
    for (i = 0; i < FROTHY_OBJECT_CAPACITY; i++) {
      release_value(&values[i]);
    }
    return 0;
  }

  release_value(&values[FROTHY_OBJECT_CAPACITY / 2]);
  if (frothy_runtime_alloc_cells(runtime(), 1, &recycled) != FROTH_OK) {
    fprintf(stderr, "alloc_cells failed to reuse a dead slot at capacity\n");
    ok = 0;
  }

  for (churn_round = 0; churn_round < 16 && ok; churn_round++) {
    release_value(&recycled);
    if (frothy_runtime_alloc_cells(runtime(), 1, &recycled) != FROTH_OK) {
      fprintf(stderr, "alloc_cells failed during churn round %zu\n",
              churn_round);
      ok = 0;
    }
  }

  release_value(&recycled);
  for (i = 0; i < FROTHY_OBJECT_CAPACITY; i++) {
    release_value(&values[i]);
  }
  ok &= expect_live_objects(0, "object slot reuse cleanup");
  return ok;
}

int main(void) {
  int ok = 1;

  ok &= test_function_calls_and_blocks();
  ok &= test_eval_scratch_limits_and_nested_calls();
  ok &= test_stable_rebinding_and_reclamation();
  ok &= test_cells_store_rules_and_overwrite_reclamation();
  ok &= test_cells_sample_program();
  ok &= test_top_level_set_forms();
  ok &= test_temporary_results_release_cleanly();
  ok &= test_allocator_failure_cleanup();
  ok &= test_object_slot_reuse_at_capacity();

  return ok ? 0 : 1;
}
