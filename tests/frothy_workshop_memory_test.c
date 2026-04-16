#include "froth_snapshot.h"
#include "froth_slot_table.h"
#include "froth_vm.h"
#include "frothy_base_image.h"
#include "frothy_boot.h"
#include "frothy_eval.h"
#include "frothy_inspect.h"
#include "frothy_parser.h"
#include "frothy_snapshot.h"
#include "frothy_snapshot_codec.h"
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

typedef struct {
  size_t heap_used;
  size_t heap_high_water;
  size_t heap_base_watermark;
  size_t slot_count;
  size_t slot_high_water;
  size_t overlay_slot_count;
  size_t cellspace_used;
  size_t cellspace_high_water;
  size_t live_objects;
  size_t object_high_water;
  size_t payload_used;
  size_t payload_high_water;
  size_t eval_value_used;
  size_t eval_value_high_water;
  size_t eval_frame_high_water;
  frothy_snapshot_codec_usage_t snapshot_usage;
} memory_report_t;

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
  char template_path[] = "/tmp/frothy-workshop-memory.XXXXXX";

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

  if (!ensure_platform()) {
    return 0;
  }

  froth_vm.heap.pointer = 0;
  froth_vm.heap.high_water = 0;
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

static int expect_error(const char *source, froth_error_t expected,
                        const char *label) {
  frothy_value_t value = frothy_value_make_nil();
  froth_error_t err = FROTH_OK;
  int ok = 1;

  if (eval_source(source, &value, &err)) {
    fprintf(stderr, "%s expected error %d for `%s`\n", label, (int)expected,
            source);
    ok = 0;
  } else if (err != expected) {
    fprintf(stderr, "%s expected error %d for `%s`, got %d\n", label,
            (int)expected, source, (int)err);
    ok = 0;
  }

  release_value(&value);
  return ok;
}

static int run_source(const char *source) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = expect_ok(source, &value);

  release_value(&value);
  return ok;
}

static int expect_int_expr(const char *source, int32_t expected,
                           const char *label) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  ok &= expect_ok(source, &value);
  if (!frothy_value_is_int(value) || frothy_value_as_int(value) != expected) {
    fprintf(stderr, "%s expected int %d\n", label, expected);
    ok = 0;
  }
  release_value(&value);
  return ok;
}

static int expect_base_owner(const char *name, const char *label) {
  char *report = NULL;
  int ok = 1;

  if (frothy_inspect_render_binding_report(runtime(), name,
                                           FROTHY_INSPECT_REPORT_SLOT_INFO,
                                           &report) != FROTH_OK) {
    fprintf(stderr, "%s failed to inspect `%s`\n", label, name);
    return 0;
  }
  if (strstr(report, "owner: base image") == NULL) {
    fprintf(stderr, "%s expected base-image owner for `%s`\n", label, name);
    ok = 0;
  }
  free(report);
  return ok;
}

static size_t overlay_slot_count(void) {
  froth_cell_u_t slot_count = froth_slot_count();
  froth_cell_u_t slot_index = 0;
  size_t overlay_count = 0;

  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    if (froth_slot_is_overlay(slot_index)) {
      overlay_count++;
    }
  }

  return overlay_count;
}

static void reset_measurement(void) {
  froth_heap_debug_reset_high_water(&froth_vm.heap);
  froth_cellspace_debug_reset_high_water(&froth_vm.cellspace);
  froth_slot_debug_reset_high_water();
  frothy_runtime_debug_reset_high_water(runtime());
  frothy_eval_debug_reset_frame_high_water();
  frothy_snapshot_codec_debug_reset_usage();
}

static void capture_report(memory_report_t *report) {
  memset(report, 0, sizeof(*report));
  report->heap_used = froth_vm.heap.pointer;
  report->heap_high_water = froth_heap_high_water(&froth_vm.heap);
  report->heap_base_watermark = froth_vm.watermark_heap_offset;
  report->slot_count = froth_slot_count();
  report->slot_high_water = froth_slot_high_water();
  report->overlay_slot_count = overlay_slot_count();
  report->cellspace_used = froth_vm.cellspace.used;
  report->cellspace_high_water = froth_cellspace_high_water(&froth_vm.cellspace);
  report->live_objects = frothy_runtime_live_object_count(runtime());
  report->object_high_water = frothy_runtime_object_high_water(runtime());
  report->payload_used = frothy_runtime_payload_used(runtime());
  report->payload_high_water = frothy_runtime_payload_high_water(runtime());
  report->eval_value_used = runtime()->eval_value_used;
  report->eval_value_high_water = frothy_runtime_eval_value_high_water(runtime());
  report->eval_frame_high_water = frothy_eval_frame_high_water();
  frothy_snapshot_codec_get_usage(&report->snapshot_usage);
}

static void print_report(const char *scenario, const memory_report_t *report) {
  printf(
      "report scenario=%s "
      "heap_used=%zu heap_high=%zu heap_base=%zu "
      "slots=%zu slots_high=%zu overlay_slots=%zu "
      "cellspace_used=%zu cellspace_high=%zu "
      "live_objects=%zu object_high=%zu "
      "payload_used=%zu payload_high=%zu "
      "eval_used=%zu eval_high=%zu eval_frames_high=%zu "
      "snapshot_payload_high=%zu snapshot_symbols_high=%zu "
      "snapshot_objects_high=%zu snapshot_bindings_high=%zu\n",
      scenario, report->heap_used, report->heap_high_water,
      report->heap_base_watermark, report->slot_count, report->slot_high_water,
      report->overlay_slot_count, report->cellspace_used,
      report->cellspace_high_water, report->live_objects,
      report->object_high_water, report->payload_used,
      report->payload_high_water, report->eval_value_used,
      report->eval_value_high_water, report->eval_frame_high_water,
      report->snapshot_usage.payload_length_high_water,
      report->snapshot_usage.symbol_count_high_water,
      report->snapshot_usage.object_count_high_water,
      report->snapshot_usage.binding_count_high_water);
}

static int run_inspection_exercises(void) {
  const char **names = NULL;
  size_t count = 0;
  char *text = NULL;
  size_t i = 0;
  bool saw_demo_pong_run = false;
  bool saw_matrix_init = false;
  int ok = 1;

  if (frothy_inspect_collect_words(&names, &count) != FROTH_OK) {
    fprintf(stderr, "failed to collect words\n");
    return 0;
  }
  if (count < 32) {
    fprintf(stderr, "expected richer workshop word surface, got %zu names\n",
            count);
    ok = 0;
  }
  for (i = 0; i < count; i++) {
    if (strcmp(names[i], "demo.pong.run") == 0) {
      saw_demo_pong_run = true;
    } else if (strcmp(names[i], "matrix.init") == 0) {
      saw_matrix_init = true;
    }
  }
  if (!saw_demo_pong_run) {
    fprintf(stderr, "words surface missing demo.pong.run\n");
    ok = 0;
  }
  if (!saw_matrix_init) {
    fprintf(stderr, "words surface missing matrix.init\n");
    ok = 0;
  }
  frothy_inspect_free_words(names);

  if (frothy_inspect_render_binding_report(runtime(), "demo.pong.draw",
                                           FROTHY_INSPECT_REPORT_SEE,
                                           &text) != FROTH_OK) {
    fprintf(stderr, "failed to render see text\n");
    return 0;
  }
  free(text);
  text = NULL;

  if (frothy_inspect_render_binding_text(runtime(), "demo.pong.draw",
                                         FROTHY_INSPECT_RENDER_CORE,
                                         &text) != FROTH_OK) {
    fprintf(stderr, "failed to render core text\n");
    return 0;
  }
  free(text);
  text = NULL;

  if (frothy_inspect_render_binding_report(runtime(), "demo.pong.run",
                                           FROTHY_INSPECT_REPORT_SLOT_INFO,
                                           &text) != FROTH_OK) {
    fprintf(stderr, "failed to render slotInfo for demo.pong.run\n");
    return 0;
  }
  free(text);
  text = NULL;

  if (frothy_inspect_render_binding_report(runtime(), "matrix.init",
                                           FROTHY_INSPECT_REPORT_SLOT_INFO,
                                           &text) != FROTH_OK) {
    fprintf(stderr, "failed to render slotInfo for matrix.init\n");
    return 0;
  }
  free(text);

  ok &= expect_base_owner("demo.pong.run", "info owner demo.pong.run");
  ok &= expect_base_owner("matrix.init", "info owner matrix.init");
  return ok;
}

static int apply_modified_pong_overlay(void) {
  return run_source("demo.pong.frameMs is 30") &&
         run_source("demo.pong.ballStepMs is 90") &&
         run_source("demo.pong.paddleHeight is 4") &&
         run_source("demo.pong.maxPaddleTop is 4") &&
         run_source("to demo.pong.net [ matrix.line: 6, 0, 6, 7, true ]") &&
         run_source("to demo.pong.decorate [ demo.pong.net: ]") &&
         run_source(
             "to demo.pong.draw [ grid.clear:; matrix.fillRect: 0, demo.pong.left, 1, demo.pong.paddleHeight, true; matrix.fillRect: (grid.width - 1), demo.pong.right, 1, demo.pong.paddleHeight, true; demo.pong.decorate:; grid.set: demo.pong.ballX, demo.pong.ballY, true ]");
}

static int run_pong_frames(size_t frame_count) {
  size_t i;

  if (!run_source("demo.pong.setup:")) {
    return 0;
  }
  for (i = 0; i < frame_count; i++) {
    if (!run_source("demo.pong.frame:")) {
      return 0;
    }
  }
  return 1;
}

static int scenario_base_image_install(memory_report_t *report) {
  if (!bootstrap_runtime()) {
    return 0;
  }
  if (!expect_int_expr("matrix.width", 12, "base matrix.width") ||
      !expect_int_expr("demo.pong.frameMs", 42, "base demo.pong.frameMs") ||
      !expect_base_owner("demo.pong.run", "base run owner")) {
    return 0;
  }
  capture_report(report);
  return 1;
}

static int scenario_fresh_boot(memory_report_t *report) {
  frothy_startup_report_t startup = {0};

  if (!prepare_runtime()) {
    return 0;
  }
  reset_measurement();
  frothy_boot_test_set_skip_boot(false);
  if (frothy_boot_run_startup(&startup) != FROTH_OK) {
    fprintf(stderr, "fresh boot startup failed\n");
    return 0;
  }
  if (!startup.boot_attempted || startup.restore_error != FROTH_OK ||
      startup.boot_error != FROTH_OK) {
    fprintf(stderr,
            "fresh boot report unexpected found=%d restore=%d boot_attempted=%d boot=%d\n",
            startup.snapshot_found ? 1 : 0, (int)startup.restore_error,
            startup.boot_attempted ? 1 : 0, (int)startup.boot_error);
    return 0;
  }
  capture_report(report);
  return 1;
}

static int scenario_inspection(memory_report_t *report) {
  if (!prepare_runtime()) {
    return 0;
  }
  reset_measurement();
  if (!run_inspection_exercises()) {
    return 0;
  }
  capture_report(report);
  return 1;
}

static int scenario_shipped_pong(memory_report_t *report) {
  if (!prepare_runtime()) {
    return 0;
  }
  reset_measurement();
  if (!run_pong_frames(8) ||
      !expect_base_owner("demo.pong.run", "shipped pong owner")) {
    return 0;
  }
  capture_report(report);
  return 1;
}

static int scenario_modified_pong(memory_report_t *report) {
  if (!prepare_runtime()) {
    return 0;
  }
  reset_measurement();
  if (!apply_modified_pong_overlay() || !run_pong_frames(8) ||
      !expect_int_expr("demo.pong.frameMs", 30, "modified frameMs")) {
    return 0;
  }
  capture_report(report);
  return 1;
}

static int scenario_save_restore_wipe(memory_report_t *restore_report,
                                      memory_report_t *wipe_report) {
  if (!prepare_runtime()) {
    return 0;
  }
  reset_measurement();
  if (!apply_modified_pong_overlay() || !run_source("save:") ||
      !run_source("demo.pong.frameMs is 11") ||
      !expect_int_expr("demo.pong.frameMs", 11, "mutated frameMs") ||
      !run_source("restore:") ||
      !expect_int_expr("demo.pong.frameMs", 30, "restored frameMs")) {
    return 0;
  }
  capture_report(restore_report);
  if (!run_source("dangerous.wipe:") ||
      !expect_int_expr("demo.pong.frameMs", 42, "wiped frameMs") ||
      !expect_base_owner("demo.pong.run", "wiped run owner") ||
      !expect_base_owner("demo.pong.draw", "wiped draw owner") ||
      !expect_error("demo.pong.net", FROTH_ERROR_UNDEFINED_WORD,
                    "wiped demo.pong.net removed") ||
      !expect_error("demo.pong.decorate", FROTH_ERROR_UNDEFINED_WORD,
                    "wiped demo.pong.decorate removed") ||
      !expect_error("restore:", FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT,
                    "wiped snapshot removed")) {
    return 0;
  }
  capture_report(wipe_report);
  return 1;
}

int main(void) {
  temp_workspace_t workspace = {{0}};
  memory_report_t report = {0};
  memory_report_t restore_report = {0};
  memory_report_t wipe_report = {0};

  printf(
      "capacity board=%s heap=%d data_space=%d slots=%d eval_values=%d "
      "eval_frames=%d objects=%d payload=%d snapshot_payload=%d\n",
      FROTH_BOARD_NAME, FROTH_HEAP_SIZE, FROTH_DATA_SPACE_SIZE,
      FROTH_SLOT_TABLE_SIZE, FROTHY_EVAL_VALUE_CAPACITY,
      FROTHY_EVAL_FRAME_CAPACITY, FROTHY_OBJECT_CAPACITY,
      FROTHY_PAYLOAD_CAPACITY, FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES);

  if (!enter_temp_workspace(&workspace)) {
    return 1;
  }

  if (!scenario_base_image_install(&report)) {
    leave_temp_workspace(&workspace);
    return 1;
  }
  print_report("base_image_install", &report);

  if (!scenario_fresh_boot(&report)) {
    leave_temp_workspace(&workspace);
    return 1;
  }
  print_report("fresh_boot", &report);

  if (!scenario_inspection(&report)) {
    leave_temp_workspace(&workspace);
    return 1;
  }
  print_report("inspection_discovery", &report);

  if (!scenario_shipped_pong(&report)) {
    leave_temp_workspace(&workspace);
    return 1;
  }
  print_report("shipped_pong", &report);

  if (!scenario_modified_pong(&report)) {
    leave_temp_workspace(&workspace);
    return 1;
  }
  print_report("modified_pong", &report);

  if (!scenario_save_restore_wipe(&restore_report, &wipe_report)) {
    leave_temp_workspace(&workspace);
    return 1;
  }
  print_report("save_restore_peak", &restore_report);
  print_report("wipe_post_state", &wipe_report);

  leave_temp_workspace(&workspace);
  return 0;
}
