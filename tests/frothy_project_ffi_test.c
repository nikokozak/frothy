#include "froth_slot_table.h"
#include "froth_tbuf.h"
#include "froth_vm.h"
#include "frothy_eval.h"
#include "frothy_ffi.h"
#include "frothy_inspect.h"
#include "frothy_parser.h"
#include "frothy_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FROTHY_PROJECT_FFI_USE_LEGACY_EXPORT
#define FROTHY_TEST_PROJECT_BINDING_NAME "project.legacy.int"
#define FROTHY_TEST_PROJECT_REPORT_EFFECT "( value -- value )"
#define FROTHY_TEST_PROJECT_REPORT_HELP "Project-legacy FFI test binding."
#else
#define FROTHY_TEST_PROJECT_BINDING_NAME "project.echo.int"
#define FROTHY_TEST_PROJECT_REPORT_EFFECT "( value -- value )"
#define FROTHY_TEST_PROJECT_REPORT_HELP "Project-maintained FFI test binding."
#endif

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

static int install_board_base_slots(void) {
  froth_error_t err = frothy_ffi_install_board_base_slots();

  if (err != FROTH_OK) {
    fprintf(stderr, "failed to install board/project base slots: %d\n",
            (int)err);
    return 0;
  }
  return 1;
}

static int capture_slot_info_text(const char *name, char **report_out) {
  if (frothy_inspect_render_binding_report(runtime(), name,
                                           FROTHY_INSPECT_REPORT_SLOT_INFO,
                                           report_out) != FROTH_OK) {
    fprintf(stderr, "failed to capture slotInfo for `%s`\n", name);
    return 0;
  }
  return 1;
}

static int expect_text_equal(const char *actual, const char *expected,
                             const char *label) {
  if (actual == NULL || strcmp(actual, expected) != 0) {
    fprintf(stderr, "%s expected:\n%s\nactual:\n%s\n", label, expected,
            actual != NULL ? actual : "(null)");
    return 0;
  }
  return 1;
}

static int test_project_binding_round_trip_and_slot_info(void) {
  char source[128];
  char expected[512];
  frothy_value_t value = frothy_value_make_nil();
  char *slot_info_text = NULL;
  int ok = 1;

  reset_frothy_state();
  ok &= install_board_base_slots();
  if (snprintf(source, sizeof(source), "%s: 41",
               FROTHY_TEST_PROJECT_BINDING_NAME) >= (int)sizeof(source)) {
    fprintf(stderr, "project binding source too long\n");
    return 0;
  }
  ok &= expect_ok(source, &value);
  ok &= expect_int_value(value, 41, source);
  release_value(&value);
  ok &= install_board_base_slots();
  ok &= capture_slot_info_text(FROTHY_TEST_PROJECT_BINDING_NAME,
                               &slot_info_text);
  if (snprintf(expected, sizeof(expected),
               "%s\n"
               "  slot: base\n"
               "  kind: native\n"
               "  call: 1 -> 1\n"
               "  owner: project ffi\n"
               "  persistence: not saved\n"
               "  effect: %s\n"
               "  help: %s",
               FROTHY_TEST_PROJECT_BINDING_NAME,
               FROTHY_TEST_PROJECT_REPORT_EFFECT,
               FROTHY_TEST_PROJECT_REPORT_HELP) >= (int)sizeof(expected)) {
    fprintf(stderr, "expected project slotInfo text too long\n");
    free(slot_info_text);
    return 0;
  }
  ok &= expect_text_equal(slot_info_text, expected,
                          "formatted project FFI slotInfo report");
  free(slot_info_text);
  return ok;
}

int main(void) {
  int ok = 1;

  ok &= test_project_binding_round_trip_and_slot_info();

  frothy_runtime_free(runtime());
  return ok ? 0 : 1;
}
