#include "froth_slot_table.h"
#include "froth_vm.h"
#include "frothy_eval.h"
#include "frothy_parser.h"
#include "frothy_value.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
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

static int expect_binding_view(const char *name,
                               frothy_value_class_t expected_class,
                               const char *expected_render,
                               const char *label) {
  froth_cell_u_t slot_index = 0;
  froth_cell_t impl = 0;
  frothy_value_t value = frothy_value_make_nil();
  frothy_value_class_t value_class;
  char *rendered = NULL;
  int ok = 1;

  if (froth_slot_find_name(name, &slot_index) != FROTH_OK ||
      froth_slot_get_impl(slot_index, &impl) != FROTH_OK) {
    fprintf(stderr, "%s failed to resolve slot `%s`\n", label, name);
    return 0;
  }
  value = frothy_value_from_cell(impl);
  if (frothy_value_class(runtime(), value, &value_class) != FROTH_OK) {
    fprintf(stderr, "%s failed to classify `%s`\n", label, name);
    return 0;
  }
  if (frothy_value_render(runtime(), value, &rendered) != FROTH_OK) {
    fprintf(stderr, "%s failed to render `%s`\n", label, name);
    return 0;
  }
  if (value_class != expected_class) {
    fprintf(stderr, "%s expected class %d for `%s`, got %d\n", label,
            (int)expected_class, name, (int)value_class);
    ok = 0;
  }
  if (strcmp(rendered, expected_render) != 0) {
    fprintf(stderr, "%s expected render `%s` for `%s`, got `%s`\n", label,
            expected_render, name, rendered);
    ok = 0;
  }
  free(rendered);
  return ok;
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

static int expect_payload_used(size_t expected, const char *label) {
  size_t used = frothy_runtime_payload_used(runtime());

  if (used != expected) {
    fprintf(stderr, "%s expected payload used %zu, got %zu\n", label, expected,
            used);
    return 0;
  }
  return 1;
}

static int expect_payload_high_water(size_t expected, const char *label) {
  size_t high_water = frothy_runtime_payload_high_water(runtime());

  if (high_water != expected) {
    fprintf(stderr, "%s expected payload high-water %zu, got %zu\n", label,
            expected, high_water);
    return 0;
  }
  return 1;
}

static int test_function_calls_and_blocks(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("inc is fn with x [ x + 1 ]", &value);
  release_value(&value);
  ok &= expect_ok("inc: 41", &value);
  ok &= expect_int_value(value, 42, "inc: 41");
  release_value(&value);

  ok &= expect_ok(
      "sumTo is fn with limit [ here total is 0; here i is 0; while i < limit [ set total to total + i; set i to i + 1 ]; total ]",
      &value);
  release_value(&value);
  ok &= expect_ok("sumTo: 4", &value);
  ok &= expect_int_value(value, 6, "sumTo: 4");
  release_value(&value);

  ok &= expect_ok("onlySet is fn [ here x is 1 ]", &value);
  release_value(&value);
  ok &= expect_ok("onlySet:", &value);
  ok &= expect_nil_value(value, "onlySet:");
  release_value(&value);

  ok &= expect_error("inc:", FROTH_ERROR_SIGNATURE);
  ok &= expect_ok("count is 7", &value);
  release_value(&value);
  ok &= expect_error("count:", FROTH_ERROR_TYPE_MISMATCH);

  return ok;
}

static int test_eval_scratch_limits_and_nested_calls(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("pair is fn with a, b [ a ]", &value);
  release_value(&value);
  ok &= expect_ok("wrap is fn with v [ pair: v, v + 1 ]", &value);
  release_value(&value);
  ok &= expect_ok("chain is fn with v [ wrap: (wrap: v) ]", &value);
  release_value(&value);

  frothy_runtime_debug_reset_high_water(runtime());
  ok &= expect_ok("pair: 7, 8", &value);
  ok &= expect_int_value(value, 7, "pair: 7, 8");
  release_value(&value);
  ok &= expect_eval_high_water(2, "pair eval scratch");

  frothy_runtime_debug_reset_high_water(runtime());
  ok &= expect_ok("pair: 9, 10", &value);
  ok &= expect_int_value(value, 9, "pair: 9, 10");
  release_value(&value);
  ok &= expect_eval_high_water(2, "pair eval scratch warmed");

  ok &= expect_ok("chain: 5", &value);
  ok &= expect_int_value(value, 5, "chain: 5");
  release_value(&value);

  frothy_runtime_test_set_eval_value_limit(runtime(), 2);
  ok &= expect_ok("pair: 11, 12", &value);
  ok &= expect_int_value(value, 11, "pair: 11, 12 at limit");
  release_value(&value);

  frothy_runtime_test_set_eval_value_limit(runtime(), 1);
  ok &= expect_error("pair: 13, 14", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  frothy_runtime_test_set_eval_value_limit(runtime(), FROTHY_EVAL_VALUE_CAPACITY);

  return ok;
}

static int test_stable_rebinding_and_reclamation(void) {
  frothy_value_t value = frothy_value_make_nil();
  size_t live_before;
  size_t payload_before;
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("adder is fn with x [ x + 1 ]", &value);
  release_value(&value);
  ok &= expect_ok("apply is fn with v [ adder: v ]", &value);
  release_value(&value);
  ok &= expect_ok("apply: 10", &value);
  ok &= expect_int_value(value, 11, "apply: 10 before rebind");
  release_value(&value);

  live_before = frothy_runtime_live_object_count(runtime());
  payload_before = frothy_runtime_payload_used(runtime());
  ok &= expect_ok("adder is fn with x [ x + 2 ]", &value);
  release_value(&value);
  ok &= expect_live_objects(live_before, "code slot rebind");
  ok &= expect_payload_used(payload_before, "code slot rebind payload");
  ok &= expect_ok("apply: 10", &value);
  ok &= expect_int_value(value, 12, "apply: 10 after rebind");
  release_value(&value);

  ok &= expect_ok("label is \"one\"", &value);
  release_value(&value);
  ok &= expect_ok("reader is fn [ label ]", &value);
  release_value(&value);
  ok &= expect_ok("reader:", &value);
  ok &= expect_text_value(value, "one", "reader: before rebind");
  release_value(&value);

  live_before = frothy_runtime_live_object_count(runtime());
  payload_before = frothy_runtime_payload_used(runtime());
  ok &= expect_ok("label is \"two\"", &value);
  release_value(&value);
  ok &= expect_live_objects(live_before, "text slot rebind");
  ok &= expect_payload_used(payload_before, "text slot rebind payload");
  ok &= expect_ok("reader:", &value);
  ok &= expect_text_value(value, "two", "reader: after rebind");
  release_value(&value);

  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("load is fn [ frame[0] ]", &value);
  release_value(&value);
  ok &= expect_ok("setCell is fn with v [ set frame[0] to v ]", &value);
  release_value(&value);
  ok &= expect_ok("setCell: 7", &value);
  ok &= expect_nil_value(value, "setCell: 7");
  release_value(&value);
  ok &= expect_ok("load:", &value);
  ok &= expect_int_value(value, 7, "load: before cells rebind");
  release_value(&value);

  live_before = frothy_runtime_live_object_count(runtime());
  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= expect_live_objects(live_before, "cells slot rebind");
  ok &= expect_ok("load:", &value);
  ok &= expect_nil_value(value, "load: after cells rebind");
  release_value(&value);

  return ok;
}

static int test_cells_store_rules_and_overwrite_reclamation(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("frame is cells(2)", &value);
  release_value(&value);
  ok &= expect_live_objects(1, "cells allocation");

  ok &= expect_ok("poke is fn with v [ set frame[0] to v ]", &value);
  release_value(&value);
  ok &= expect_ok("peek is fn [ frame[0] ]", &value);
  release_value(&value);
  ok &= expect_live_objects(3, "cells helpers");

  ok &= expect_ok("poke: \"alpha\"", &value);
  ok &= expect_nil_value(value, "poke(alpha)");
  release_value(&value);
  ok &= expect_live_objects(4, "cells store first text");

  ok &= expect_ok("poke: \"beta\"", &value);
  ok &= expect_nil_value(value, "poke(beta)");
  release_value(&value);
  ok &= expect_live_objects(4, "cells overwrite text");
  ok &= expect_ok("peek:", &value);
  ok &= expect_text_value(value, "beta", "peek:");
  release_value(&value);

  ok &= expect_ok("touch is fn [ set frame[1] to true; frame[1] ]", &value);
  release_value(&value);
  ok &= expect_ok("touch:", &value);
  if (!frothy_value_is_bool(value) || !frothy_value_as_bool(value)) {
    fprintf(stderr, "touch: expected true\n");
    ok = 0;
  }
  release_value(&value);

  ok &= expect_ok("readFar is fn [ frame[2] ]", &value);
  release_value(&value);
  ok &= expect_error("readFar:", FROTH_ERROR_BOUNDS);

  ok &= expect_ok("writeFar is fn with v [ set frame[2] to v ]", &value);
  release_value(&value);
  ok &= expect_error("writeFar: 9", FROTH_ERROR_BOUNDS);

  ok &= expect_ok("factory is fn [ fn [ 1 ] ]", &value);
  release_value(&value);
  ok &= expect_ok("storeCode is fn [ set frame[0] to factory: ]", &value);
  release_value(&value);
  ok &= expect_error("storeCode:", FROTH_ERROR_TYPE_MISMATCH);

  ok &= expect_ok("bad is fn [ set missing to 1 ]", &value);
  release_value(&value);
  ok &= expect_error("bad:", FROTH_ERROR_UNDEFINED_WORD);

  return ok;
}

static int test_cells_sample_program(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("frame is cells(3)", &value);
  release_value(&value);
  ok &= expect_ok(
      "writeFrame is fn [ set frame[0] to 7; set frame[1] to false; set frame[2] to \"ready\" ]",
      &value);
  release_value(&value);
  ok &= expect_ok("count is fn [ frame[0] ]", &value);
  release_value(&value);
  ok &= expect_ok("enabled is fn [ frame[1] ]", &value);
  release_value(&value);
  ok &= expect_ok("label is fn [ frame[2] ]", &value);
  release_value(&value);

  ok &= expect_ok("writeFrame:", &value);
  ok &= expect_nil_value(value, "writeFrame:");
  release_value(&value);

  ok &= expect_ok("count:", &value);
  ok &= expect_int_value(value, 7, "count:");
  release_value(&value);

  ok &= expect_ok("enabled:", &value);
  ok &= expect_bool_value(value, false, "enabled:");
  release_value(&value);

  ok &= expect_ok("label:", &value);
  ok &= expect_text_value(value, "ready", "label:");
  release_value(&value);

  return ok;
}

static int test_top_level_set_forms(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("unit is 1", &value);
  release_value(&value);
  ok &= expect_ok("set unit to 2", &value);
  ok &= expect_nil_value(value, "set unit to 2");
  release_value(&value);
  ok &= expect_ok("unit", &value);
  ok &= expect_int_value(value, 2, "unit");
  release_value(&value);

  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("set frame[0] to 7", &value);
  ok &= expect_nil_value(value, "set frame[0] to 7");
  release_value(&value);
  ok &= expect_ok("frame[0]", &value);
  ok &= expect_int_value(value, 7, "frame[0]");
  release_value(&value);

  return ok;
}

static int test_fixed_layout_records(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("record Point [ x, y ]", &value);
  ok &= expect_nil_value(value, "record Point");
  release_value(&value);
  ok &= expect_binding_view("Point", FROTHY_VALUE_CLASS_RECORD_DEF,
                            "record Point [ x, y ]", "record def inspect");

  ok &= expect_ok("point is Point: 10, 20", &value);
  ok &= expect_nil_value(value, "point binding");
  release_value(&value);
  ok &= expect_binding_view("point", FROTHY_VALUE_CLASS_RECORD,
                            "Point: 10, 20", "record value inspect");

  ok &= expect_ok("point->x", &value);
  ok &= expect_int_value(value, 10, "point->x");
  release_value(&value);
  ok &= expect_ok("set point->x to 11", &value);
  ok &= expect_nil_value(value, "set point->x");
  release_value(&value);
  ok &= expect_ok("point->x", &value);
  ok &= expect_int_value(value, 11, "point->x after write");
  release_value(&value);

  ok &= expect_error("Point: 1", FROTH_ERROR_SIGNATURE);
  ok &= expect_error("point->missing", FROTH_ERROR_BOUNDS);
  ok &= expect_error("set point->x to fn [ 1 ]", FROTH_ERROR_TYPE_MISMATCH);

  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("set frame[0] to point", &value);
  ok &= expect_nil_value(value, "set frame[0] to point");
  release_value(&value);
  ok &= expect_ok("frame[0]->x", &value);
  ok &= expect_int_value(value, 11, "frame[0]->x");
  release_value(&value);

  ok &= expect_ok("record Point [ x ]", &value);
  ok &= expect_nil_value(value, "record Point rebind");
  release_value(&value);
  ok &= expect_ok("saved = Point: 7", &value);
  ok &= expect_nil_value(value, "saved binding");
  release_value(&value);
  ok &= expect_ok("record Point [ y ]", &value);
  ok &= expect_nil_value(value, "record Point second rebind");
  release_value(&value);
  ok &= expect_ok("saved->x", &value);
  ok &= expect_int_value(value, 7, "saved->x after rebind");
  release_value(&value);
  ok &= expect_error("saved->y", FROTH_ERROR_BOUNDS);

  return ok;
}

static int test_spoken_ledger_surface_and_control_forms(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("count is 7", &value);
  release_value(&value);
  ok &= expect_ok("set count to 8", &value);
  ok &= expect_nil_value(value, "set count to 8");
  release_value(&value);
  ok &= expect_ok("count", &value);
  ok &= expect_int_value(value, 8, "count after spoken set");
  release_value(&value);

  ok &= expect_ok("to inc with x [ x + 1 ]", &value);
  release_value(&value);
  ok &= expect_ok("inc: 41", &value);
  ok &= expect_int_value(value, 42, "inc: 41");
  release_value(&value);

  ok &= expect_ok("to setFlag! with v [ v ]", &value);
  release_value(&value);
  ok &= expect_ok("setFlag!: 7", &value);
  ok &= expect_int_value(value, 7, "setFlag!: 7");
  release_value(&value);

  ok &= expect_ok("to pixel@ [ 9 ]", &value);
  release_value(&value);
  ok &= expect_ok("pixel@:", &value);
  ok &= expect_int_value(value, 9, "pixel@:");
  release_value(&value);

  ok &= expect_ok("to ready? [ true ]", &value);
  release_value(&value);
  ok &= expect_ok("ready?:", &value);
  ok &= expect_bool_value(value, true, "ready?:");
  release_value(&value);

  ok &= expect_ok("localDemo is fn [ here n is 5; n ]", &value);
  release_value(&value);
  ok &= expect_ok("localDemo:", &value);
  ok &= expect_int_value(value, 5, "localDemo:");
  release_value(&value);

  ok &= expect_ok("plainLocalDemo is fn [ n is 6; n ]", &value);
  release_value(&value);
  ok &= expect_ok("plainLocalDemo:", &value);
  ok &= expect_int_value(value, 6, "plainLocalDemo:");
  release_value(&value);

  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("set frame[0] to 7", &value);
  ok &= expect_nil_value(value, "set frame[0] to 7");
  release_value(&value);
  ok &= expect_ok("frame[0]", &value);
  ok &= expect_int_value(value, 7, "frame[0] after spoken set");
  release_value(&value);

  ok &= expect_ok("makeInc is fn [ fn with x [ x + 1 ] ]", &value);
  release_value(&value);
  ok &= expect_ok("call makeInc: with 41", &value);
  ok &= expect_int_value(value, 42, "call makeInc: with 41");
  release_value(&value);

  ok &= expect_ok("counter is cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("set counter[0] to 0", &value);
  release_value(&value);
  ok &= expect_ok("repeat 3 as i [ set counter[0] to counter[0] + i ]", &value);
  ok &= expect_nil_value(value, "repeat indexed");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 3, "indexed repeat sum");
  release_value(&value);

  ok &= expect_ok("set counter[0] to 0", &value);
  release_value(&value);
  ok &= expect_ok(
      "repeat 2 as i [ repeat 3 as j [ set counter[0] to counter[0] + i + j ] ]",
      &value);
  ok &= expect_nil_value(value, "nested repeat");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 9, "nested repeat sum");
  release_value(&value);

  ok &= expect_ok(
      "nextCount is fn [ here n is counter[0]; set counter[0] to counter[0] + 1; n ]",
      &value);
  release_value(&value);
  ok &= expect_ok("set counter[0] to 0", &value);
  release_value(&value);
  ok &= expect_ok("repeat nextCount: [ nil ]", &value);
  ok &= expect_nil_value(value, "repeat count evaluates once");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 1, "repeat count evaluated once");
  release_value(&value);

  ok &= expect_ok("set counter[0] to 5", &value);
  release_value(&value);
  ok &= expect_ok("repeat 0 [ set counter[0] to 99 ]", &value);
  ok &= expect_nil_value(value, "repeat zero");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 5, "repeat zero leaves state");
  release_value(&value);
  ok &= expect_ok("repeat -1 [ set counter[0] to 99 ]", &value);
  ok &= expect_nil_value(value, "repeat negative");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 5, "repeat negative leaves state");
  release_value(&value);

  ok &= expect_ok("when true [ 42 ]", &value);
  ok &= expect_int_value(value, 42, "when true");
  release_value(&value);
  ok &= expect_ok("when false [ 42 ]", &value);
  ok &= expect_nil_value(value, "when false");
  release_value(&value);
  ok &= expect_ok("unless false [ 42 ]", &value);
  ok &= expect_int_value(value, 42, "unless false");
  release_value(&value);
  ok &= expect_ok("unless true [ 42 ]", &value);
  ok &= expect_nil_value(value, "unless true");
  release_value(&value);

  ok &= expect_ok("set counter[0] to 0", &value);
  release_value(&value);
  ok &= expect_ok(
      "tick is fn [ set counter[0] to counter[0] + 1; true ]", &value);
  release_value(&value);
  ok &= expect_ok("false and tick:", &value);
  ok &= expect_bool_value(value, false, "false and tick:");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 0, "and short-circuit");
  release_value(&value);
  ok &= expect_ok("true or tick:", &value);
  ok &= expect_bool_value(value, true, "true or tick:");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 0, "or short-circuit");
  release_value(&value);
  ok &= expect_ok("true and tick:", &value);
  ok &= expect_bool_value(value, true, "true and tick:");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 1, "and evaluates rhs");
  release_value(&value);
  ok &= expect_ok("false or tick:", &value);
  ok &= expect_bool_value(value, true, "false or tick:");
  release_value(&value);
  ok &= expect_ok("counter[0]", &value);
  ok &= expect_int_value(value, 2, "or evaluates rhs");
  release_value(&value);

  ok &= expect_error("1 and true", FROTH_ERROR_TYPE_MISMATCH);
  ok &= expect_error("false or 1", FROTH_ERROR_TYPE_MISMATCH);

  return ok;
}

static int test_temporary_results_release_cleanly(void) {
  frothy_value_t value = frothy_value_make_nil();
  size_t high_water = 0;
  int ok = 1;

  reset_frothy_state();

  ok &= expect_live_objects(0, "fresh runtime");
  ok &= expect_payload_used(0, "fresh payload");
  ok &= expect_ok("\"temp\"", &value);
  ok &= expect_text_value(value, "temp", "temporary text");
  ok &= expect_live_objects(1, "temporary text retained by caller");
  if (frothy_runtime_payload_used(runtime()) == 0) {
    fprintf(stderr, "temporary text should consume payload\n");
    ok = 0;
  }
  release_value(&value);
  ok &= expect_live_objects(0, "temporary text released");
  ok &= expect_payload_used(0, "temporary text payload released");

  ok &= expect_ok("fn [ 1 ]", &value);
  ok &= expect_live_objects(1, "temporary code retained by caller");
  if (frothy_runtime_payload_used(runtime()) == 0) {
    fprintf(stderr, "temporary code should consume payload\n");
    ok = 0;
  }
  high_water = frothy_runtime_payload_high_water(runtime());
  if (high_water == 0) {
    fprintf(stderr, "temporary code should raise payload high-water\n");
    ok = 0;
  }
  release_value(&value);
  ok &= expect_live_objects(0, "temporary code released");
  ok &= expect_payload_used(0, "temporary code payload released");

  return ok;
}

static int test_code_payload_reuse_with_factory_churn(void) {
  frothy_value_t value = frothy_value_make_nil();
  size_t baseline_payload = 0;
  size_t first_high_water = 0;
  size_t i;
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("factory is fn [ fn [ 1 ] ]", &value);
  release_value(&value);
  baseline_payload = frothy_runtime_payload_used(runtime());
  frothy_runtime_debug_reset_high_water(runtime());

  ok &= expect_ok("factory:", &value);
  first_high_water = frothy_runtime_payload_high_water(runtime());
  if (first_high_water == 0) {
    fprintf(stderr, "factory() should allocate payload for returned code\n");
    ok = 0;
  }
  release_value(&value);
  ok &= expect_payload_used(baseline_payload, "factory churn baseline");

  for (i = 0; i < 32 && ok; i++) {
    ok &= expect_ok("factory:", &value);
    release_value(&value);
    ok &= expect_payload_used(baseline_payload, "factory churn payload reuse");
    ok &= expect_payload_high_water(first_high_water,
                                    "factory churn high-water reuse");
  }

  return ok;
}

static int test_allocator_failure_cleanup(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  frothy_runtime_test_fail_next_append(runtime());
  ok &= expect_error("\"boom\"", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  ok &= expect_live_objects(0, "text append failure");
  ok &= expect_payload_used(0, "text append payload cleanup");

  frothy_runtime_test_fail_next_append(runtime());
  ok &= expect_error("fn [ 1 ]", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  ok &= expect_live_objects(0, "code append failure");
  ok &= expect_payload_used(0, "code append payload cleanup");

  frothy_runtime_test_fail_next_append(runtime());
  ok &= expect_error("frame is cells(2)", FROTH_ERROR_HEAP_OUT_OF_MEMORY);
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

static size_t test_payload_align_up(size_t value) {
  const size_t alignment = _Alignof(max_align_t);
  size_t remainder = value % alignment;

  if (remainder == 0) {
    return value;
  }
  return value + (alignment - remainder);
}

static int test_payload_capacity_and_fragmentation(void) {
  frothy_payload_span_t spans[256];
  frothy_payload_span_t reused = {0};
  void *data = NULL;
  size_t count = 0;
  size_t i;
  size_t chunk = test_payload_align_up(_Alignof(max_align_t) * 8);
  size_t expected_used = 0;
  int ok = 1;

  reset_frothy_state();
  memset(spans, 0, sizeof(spans));

  while (count < (sizeof(spans) / sizeof(spans[0])) &&
         frothy_runtime_alloc_payload(runtime(), chunk, &spans[count], &data) ==
             FROTH_OK) {
    memset(data, 0, chunk);
    expected_used += spans[count].length;
    count++;
  }

  if (count < 3) {
    fprintf(stderr, "payload fill expected at least three spans, got %zu\n",
            count);
    ok = 0;
    goto cleanup;
  }
  ok &= expect_payload_used(expected_used, "payload fill used");

  if (frothy_runtime_alloc_payload(runtime(), chunk, &reused, &data) !=
      FROTH_ERROR_HEAP_OUT_OF_MEMORY) {
    fprintf(stderr, "payload fill expected heap out of memory at capacity\n");
    ok = 0;
  }

  frothy_runtime_release_payload(runtime(), spans[count / 2]);
  expected_used -= spans[count / 2].length;
  spans[count / 2].length = 0;
  ok &= expect_payload_used(expected_used, "payload release used");

  if (frothy_runtime_alloc_payload(runtime(), chunk / 2, &reused, &data) !=
      FROTH_OK) {
    fprintf(stderr, "payload fragmentation expected smaller span reuse\n");
    ok = 0;
  } else {
    memset(data, 0, chunk / 2);
    expected_used += reused.length;
    ok &= expect_payload_used(expected_used, "payload reuse used");
    frothy_runtime_release_payload(runtime(), reused);
    expected_used -= reused.length;
    reused.length = 0;
    ok &= expect_payload_used(expected_used, "payload reuse release used");
  }

  if (frothy_runtime_alloc_payload(runtime(), chunk, &reused, &data) !=
      FROTH_OK) {
    fprintf(stderr, "payload fragmentation expected merged span reuse\n");
    ok = 0;
  } else {
    memset(data, 0, chunk);
    expected_used += reused.length;
    ok &= expect_payload_used(expected_used, "payload merged reuse used");
  }

cleanup:
  frothy_runtime_release_payload(runtime(), reused);
  for (i = 0; i < count; i++) {
    frothy_runtime_release_payload(runtime(), spans[i]);
  }
  ok &= expect_payload_used(0, "payload cleanup");
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

static int test_readability_language_tranche(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  reset_frothy_state();

  ok &= expect_ok("count is 7", &value);
  release_value(&value);
  ok &= expect_ok("bump is fn [ here count is 1; set @count to count + 2 ]",
                  &value);
  release_value(&value);
  ok &= expect_ok("bump:", &value);
  ok &= expect_nil_value(value, "bump:");
  release_value(&value);
  ok &= expect_ok("count", &value);
  ok &= expect_int_value(value, 3, "count after @count write");
  release_value(&value);

  ok &= expect_error("alias is @count", FROTH_ERROR_TYPE_MISMATCH);
  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= expect_error("set frame[0] to @count", FROTH_ERROR_TYPE_MISMATCH);

  ok &= expect_ok(
      "choose is fn with ready, fallback [ cond [ when ready [ 1 ]; when fallback [ 2 ]; else [ nil ] ] ]",
      &value);
  release_value(&value);
  ok &= expect_ok("choose: false, true", &value);
  ok &= expect_int_value(value, 2, "cond fallback branch");
  release_value(&value);
  ok &= expect_ok("choose: false, false", &value);
  ok &= expect_nil_value(value, "cond nil default");
  release_value(&value);

  ok &= expect_ok("count is 0", &value);
  release_value(&value);
  ok &= expect_ok(
      "nextMode is fn [ set @count to count + 1; if count == 1 [ \"on\" ] else [ \"off\" ] ]",
      &value);
  release_value(&value);
  ok &= expect_ok(
      "route is fn [ case nextMode: [ \"off\" [ 0 ]; \"on\" [ count ]; else [ 2 ] ] ]",
      &value);
  release_value(&value);
  ok &= expect_ok("route:", &value);
  ok &= expect_int_value(value, 1, "case scrutinee result");
  release_value(&value);
  ok &= expect_ok("count", &value);
  ok &= expect_int_value(value, 1, "case scrutinee evaluated once");
  release_value(&value);

  ok &= expect_ok("board is 21", &value);
  release_value(&value);
  ok &= expect_ok(
      "in led [ pin is 13; to showPin [ pin ]; to showBoard [ board ]; to retarget [ set pin to 14; set board to 30 ] ]",
      &value);
  ok &= expect_nil_value(value, "in led [...]");
  release_value(&value);
  ok &= expect_ok("led.showPin:", &value);
  ok &= expect_int_value(value, 13, "in prefix prefixed-first read");
  release_value(&value);
  ok &= expect_ok("led.showBoard:", &value);
  ok &= expect_int_value(value, 21, "in prefix global fallback read");
  release_value(&value);
  ok &= expect_ok("led.retarget:", &value);
  ok &= expect_nil_value(value, "in prefix fallback write");
  release_value(&value);
  ok &= expect_ok("led.pin", &value);
  ok &= expect_int_value(value, 14, "in prefix prefixed-first write");
  release_value(&value);
  ok &= expect_ok("board", &value);
  ok &= expect_int_value(value, 30, "in prefix global fallback write");
  release_value(&value);

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
  ok &= test_fixed_layout_records();
  ok &= test_spoken_ledger_surface_and_control_forms();
  ok &= test_temporary_results_release_cleanly();
  ok &= test_code_payload_reuse_with_factory_churn();
  ok &= test_allocator_failure_cleanup();
  ok &= test_payload_capacity_and_fragmentation();
  ok &= test_object_slot_reuse_at_capacity();
  ok &= test_readability_language_tranche();

  return ok ? 0 : 1;
}
