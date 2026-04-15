#include "froth_slot_table.h"
#include "froth_tbuf.h"
#include "froth_vm.h"
#include "frothy_base_image.h"
#include "frothy_eval.h"
#include "frothy_inspect.h"
#include "frothy_parser.h"
#include "frothy_snapshot.h"
#include "frothy_value.h"
#include "platform.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  char original_dir[PATH_MAX];
  char path[PATH_MAX];
} temp_workspace_t;

static bool platform_ready = false;
static bool runtime_bootstrapped = false;

static frothy_runtime_t *runtime(void) {
  return &froth_vm.frothy_runtime;
}

static void release_value(frothy_value_t *value) {
  (void)frothy_value_release(runtime(), *value);
  *value = frothy_value_make_nil();
}

static int ensure_platform(void) {
  if (platform_ready) {
    return 1;
  }
  if (platform_init() != FROTH_OK) {
    fprintf(stderr, "failed to initialize platform runtime\n");
    return 0;
  }
  platform_ready = true;
  return 1;
}

static int enter_temp_workspace(temp_workspace_t *workspace) {
  char template_path[] = "/tmp/frothy-tm1629.XXXXXX";

  if (getcwd(workspace->original_dir, sizeof(workspace->original_dir)) == NULL) {
    perror("getcwd");
    return 0;
  }
  if (mkdtemp(template_path) == NULL) {
    perror("mkdtemp");
    return 0;
  }
  if (snprintf(workspace->path, sizeof(workspace->path), "%s", template_path) >=
      (int)sizeof(workspace->path)) {
    fprintf(stderr, "workspace path too long\n");
    return 0;
  }
  if (chdir(workspace->path) != 0) {
    perror("chdir");
    return 0;
  }

  return 1;
}

static void leave_temp_workspace(const temp_workspace_t *workspace) {
  if (workspace->original_dir[0] != '\0') {
    (void)chdir(workspace->original_dir);
  }
}

static int bootstrap_runtime(void) {
  if (runtime_bootstrapped) {
    return 1;
  }

  froth_vm.heap.pointer = 0;
  froth_vm.boot_complete = 1;
  froth_vm.trampoline_depth = 0;
  froth_vm.interrupted = 0;
  froth_vm.thrown = FROTH_OK;
  froth_vm.last_error_slot = -1;
  froth_vm.mark_offset = (froth_cell_u_t)-1;
  froth_cellspace_init(&froth_vm.cellspace);
  frothy_runtime_init(runtime(), &froth_vm.cellspace);
  if (frothy_base_image_install() != FROTH_OK) {
    fprintf(stderr, "failed to install base image\n");
    return 0;
  }
  froth_vm.watermark_heap_offset = froth_vm.heap.pointer;
  runtime_bootstrapped = true;
  return 1;
}

static int prepare_runtime(void) {
  if (!bootstrap_runtime()) {
    return 0;
  }
  if (frothy_snapshot_wipe() != FROTH_OK) {
    fprintf(stderr, "failed to wipe snapshot workspace\n");
    return 0;
  }
  froth_vm.interrupted = 0;
  froth_vm.thrown = FROTH_OK;
  froth_vm.last_error_slot = -1;
  return 1;
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
  if (!frothy_value_is_int(value) || frothy_value_as_int(value) != expected) {
    fprintf(stderr, "%s expected int %d\n", label, expected);
    return 0;
  }
  return 1;
}

static int expect_bool_value(frothy_value_t value, bool expected,
                             const char *label) {
  if (!frothy_value_is_bool(value) ||
      frothy_value_as_bool(value) != expected) {
    fprintf(stderr, "%s expected bool %s\n", label,
            expected ? "true" : "false");
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

static int capture_slot_info_text(const char *name, char **report_out) {
  if (frothy_inspect_render_binding_report(runtime(), name,
                                           FROTHY_INSPECT_REPORT_SLOT_INFO,
                                           report_out) != FROTH_OK) {
    fprintf(stderr, "failed to capture slotInfo for `%s`\n", name);
    return 0;
  }
  return 1;
}

static int expect_contains(const char *text, const char *needle,
                           const char *label) {
  if (text == NULL || strstr(text, needle) == NULL) {
    fprintf(stderr, "%s expected substring `%s`\n", label, needle);
    return 0;
  }
  return 1;
}

static int test_surface_metadata_and_smoke(void) {
  frothy_value_t value = frothy_value_make_nil();
  char *slot_info = NULL;
  int ok = 1;

  if (!prepare_runtime()) {
    return 0;
  }

  ok &= expect_ok("TM1629_STB", &value);
  ok &= expect_int_value(value, 18, "TM1629_STB");
  release_value(&value);
  ok &= expect_ok("TM1629_CLK", &value);
  ok &= expect_int_value(value, 19, "TM1629_CLK");
  release_value(&value);
  ok &= expect_ok("TM1629_DIO", &value);
  ok &= expect_int_value(value, 23, "TM1629_DIO");
  release_value(&value);
  ok &= expect_ok("matrix.width", &value);
  ok &= expect_int_value(value, 12, "matrix.width");
  release_value(&value);
  ok &= expect_ok("matrix.height", &value);
  ok &= expect_int_value(value, 8, "matrix.height");
  release_value(&value);

  ok &= expect_ok("matrix.init:", &value);
  ok &= expect_nil_value(value, "matrix.init:");
  release_value(&value);
  ok &= expect_ok("matrix.fill:", &value);
  ok &= expect_nil_value(value, "matrix.fill:");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 0", &value);
  ok &= expect_int_value(value, 4095, "matrix.fill affects tm1629 buffer");
  release_value(&value);
  ok &= expect_ok("matrix.clear:", &value);
  ok &= expect_nil_value(value, "matrix.clear:");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 0", &value);
  ok &= expect_int_value(value, 0, "matrix.clear resets tm1629 buffer");
  release_value(&value);

  ok &= capture_slot_info_text("tm1629.raw.init", &slot_info);
  ok &= expect_contains(slot_info, "owner: board ffi",
                        "tm1629.raw.init owner");
  ok &= expect_contains(slot_info, "call: 3 -> 1",
                        "tm1629.raw.init call shape");
  free(slot_info);
  slot_info = NULL;

  ok &= capture_slot_info_text("matrix.init", &slot_info);
  ok &= expect_contains(slot_info, "owner: base image", "matrix.init owner");
  ok &= expect_contains(slot_info, "kind: code", "matrix.init kind");
  free(slot_info);
  slot_info = NULL;

  ok &= capture_slot_info_text("TM1629_STB", &slot_info);
  ok &= expect_contains(slot_info, "owner: base image", "TM1629_STB owner");
  ok &= expect_contains(slot_info, "kind: int", "TM1629_STB kind");
  free(slot_info);
  return ok;
}

static int test_language_vectors(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  if (!prepare_runtime()) {
    return 0;
  }

  ok &= expect_ok("tm1629.raw.row!: 8191, 0", &value);
  ok &= expect_nil_value(value, "tm1629.raw.row! clamp");
  release_value(&value);
  ok &= expect_ok("tm1629.raw.row@: 0", &value);
  ok &= expect_int_value(value, 4095, "tm1629.raw.row@ clamp result");
  release_value(&value);
  ok &= expect_ok("tm1629.raw.row!: 9, 99", &value);
  ok &= expect_nil_value(value, "tm1629.raw.row! oob");
  release_value(&value);
  ok &= expect_ok("tm1629.raw.row@: 99", &value);
  ok &= expect_int_value(value, 0, "tm1629.raw.row@ oob");
  release_value(&value);
  ok &= expect_ok("tm1629.raw.pixel@: -1, 0", &value);
  ok &= expect_bool_value(value, false, "tm1629.raw.pixel@ oob");
  release_value(&value);

  ok &= expect_ok("tm1629.clear:", &value);
  ok &= expect_nil_value(value, "tm1629.clear before hLine");
  release_value(&value);
  ok &= expect_ok("tm1629.hLine: 2, 1, 5, true", &value);
  ok &= expect_nil_value(value, "tm1629.hLine");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 1", &value);
  ok &= expect_int_value(value, 124, "tm1629.hLine vector");
  release_value(&value);

  ok &= expect_ok("tm1629.clear:", &value);
  ok &= expect_nil_value(value, "tm1629.clear before fillRect");
  release_value(&value);
  ok &= expect_ok("tm1629.fillRect: 1, 1, 3, 2, true", &value);
  ok &= expect_nil_value(value, "tm1629.fillRect");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 1", &value);
  ok &= expect_int_value(value, 14, "tm1629.fillRect row 1");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 2", &value);
  ok &= expect_int_value(value, 14, "tm1629.fillRect row 2");
  release_value(&value);

  ok &= expect_ok("tm1629.clear:", &value);
  ok &= expect_nil_value(value, "tm1629.clear before populate");
  release_value(&value);
  ok &= expect_ok(
      "tm1629.populate: fn with x, y [ ((x + y) % 2) == 1 ]",
      &value);
  ok &= expect_nil_value(value, "tm1629.populate");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 0", &value);
  ok &= expect_int_value(value, 2730, "tm1629.populate vector");
  release_value(&value);

  ok &= expect_ok("tm1629.clear:", &value);
  ok &= expect_nil_value(value, "tm1629.clear before lifeStep");
  release_value(&value);
  ok &= expect_ok("tm1629.pixel!: 5, 2, true", &value);
  ok &= expect_nil_value(value, "tm1629.pixel row 2");
  release_value(&value);
  ok &= expect_ok("tm1629.pixel!: 5, 3, true", &value);
  ok &= expect_nil_value(value, "tm1629.pixel row 3");
  release_value(&value);
  ok &= expect_ok("tm1629.pixel!: 5, 4, true", &value);
  ok &= expect_nil_value(value, "tm1629.pixel row 4");
  release_value(&value);
  ok &= expect_ok("tm1629.lifeStep:", &value);
  ok &= expect_nil_value(value, "tm1629.lifeStep");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 2", &value);
  ok &= expect_int_value(value, 0, "tm1629.lifeStep row 2");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 3", &value);
  ok &= expect_int_value(value, 112, "tm1629.lifeStep row 3");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 4", &value);
  ok &= expect_int_value(value, 0, "tm1629.lifeStep row 4");
  release_value(&value);

  ok &= expect_ok("tm1629.clear:", &value);
  ok &= expect_nil_value(value, "tm1629.clear before rect");
  release_value(&value);
  ok &= expect_ok("tm1629.rect: 0, 0, 12, 8, true", &value);
  ok &= expect_nil_value(value, "tm1629.rect");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 0", &value);
  ok &= expect_int_value(value, 4095, "tm1629.rect row 0");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 3", &value);
  ok &= expect_int_value(value, 2049, "tm1629.rect row 3");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 7", &value);
  ok &= expect_int_value(value, 4095, "tm1629.rect row 7");
  release_value(&value);

  ok &= expect_ok("matrix.clear:", &value);
  ok &= expect_nil_value(value, "matrix.clear before line");
  release_value(&value);
  ok &= expect_ok("matrix.line: 2, 1, 6, 1, true", &value);
  ok &= expect_nil_value(value, "matrix.line");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 1", &value);
  ok &= expect_int_value(value, 124, "matrix.line");
  release_value(&value);
  ok &= expect_ok("matrix.clear:", &value);
  ok &= expect_nil_value(value, "matrix.clear before matrix.fillRect");
  release_value(&value);
  ok &= expect_ok("matrix.fillRect: 1, 1, 3, 2, true", &value);
  ok &= expect_nil_value(value, "matrix.fillRect");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 1", &value);
  ok &= expect_int_value(value, 14, "matrix.fillRect");
  release_value(&value);
  ok &= expect_ok("matrix.clear:", &value);
  ok &= expect_nil_value(value, "matrix.clear before matrix.rect");
  release_value(&value);
  ok &= expect_ok("matrix.rect: 0, 0, 12, 8, true", &value);
  ok &= expect_nil_value(value, "matrix.rect");
  release_value(&value);
  ok &= expect_ok("tm1629.row@: 3", &value);
  ok &= expect_int_value(value, 2049, "matrix.rect");
  release_value(&value);
  return ok;
}

static int test_wipe_restores_base_surface(void) {
  frothy_value_t value = frothy_value_make_nil();
  char *slot_info = NULL;
  int ok = 1;

  if (!prepare_runtime()) {
    return 0;
  }

  ok &= expect_ok("matrix.init:", &value);
  ok &= expect_nil_value(value, "matrix.init before wipe");
  release_value(&value);
  ok &= expect_ok("tm1629.raw.row!: 255, 0", &value);
  ok &= expect_nil_value(value, "tm1629.raw.row! before wipe");
  release_value(&value);
  ok &= expect_ok("matrix.width is 1", &value);
  ok &= expect_nil_value(value, "overlay matrix.width");
  release_value(&value);
  ok &= expect_ok("to matrix.clear [ 99 ]", &value);
  ok &= expect_nil_value(value, "overlay matrix.clear");
  release_value(&value);
  ok &= expect_ok("to tm1629.show [ 77 ]", &value);
  ok &= expect_nil_value(value, "overlay tm1629.show");
  release_value(&value);

  ok &= expect_ok("matrix.width", &value);
  ok &= expect_int_value(value, 1, "overlay matrix.width");
  release_value(&value);
  ok &= expect_ok("matrix.clear:", &value);
  ok &= expect_int_value(value, 99, "overlay matrix.clear");
  release_value(&value);
  ok &= expect_ok("tm1629.show:", &value);
  ok &= expect_int_value(value, 77, "overlay tm1629.show");
  release_value(&value);

  ok &= expect_ok("dangerous.wipe:", &value);
  ok &= expect_nil_value(value, "dangerous.wipe:");
  release_value(&value);

  ok &= expect_ok("matrix.width", &value);
  ok &= expect_int_value(value, 12, "restored matrix.width");
  release_value(&value);
  ok &= expect_ok("matrix.clear:", &value);
  ok &= expect_nil_value(value, "restored matrix.clear");
  release_value(&value);
  ok &= expect_ok("tm1629.show:", &value);
  ok &= expect_nil_value(value, "restored tm1629.show");
  release_value(&value);
  ok &= expect_ok("tm1629.raw.row@: 0", &value);
  ok &= expect_int_value(value, 0, "runtime state reset after wipe");
  release_value(&value);
  ok &= expect_ok("TM1629_STB", &value);
  ok &= expect_int_value(value, 18, "pin survives wipe");
  release_value(&value);

  ok &= capture_slot_info_text("tm1629.raw.init", &slot_info);
  ok &= expect_contains(slot_info, "owner: board ffi",
                        "tm1629.raw.init restored owner");
  free(slot_info);
  slot_info = NULL;
  ok &= capture_slot_info_text("matrix.clear", &slot_info);
  ok &= expect_contains(slot_info, "owner: base image",
                        "matrix.clear restored owner");
  free(slot_info);

  ok &= expect_error("undefined.tm1629.word", FROTH_ERROR_UNDEFINED_WORD);
  return ok;
}

int main(void) {
  temp_workspace_t workspace = {{0}};
  int ok = 1;

  ok &= ensure_platform();
  if (!ok) {
    return 1;
  }
  if (!enter_temp_workspace(&workspace)) {
    return 1;
  }

  ok &= test_surface_metadata_and_smoke();
  ok &= test_language_vectors();
  ok &= test_wipe_restores_base_surface();

  frothy_runtime_free(runtime());
  leave_temp_workspace(&workspace);
  return ok ? 0 : 1;
}
