#include "froth_ffi.h"
#include "froth_slot_table.h"
#include "froth_tbuf.h"
#include "froth_vm.h"
#include "frothy_base_image.h"
#include "frothy_eval.h"
#include "frothy_ffi.h"
#include "frothy_parser.h"
#include "frothy_snapshot.h"
#include "frothy_value.h"
#include "platform.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  char original_dir[PATH_MAX];
  char path[PATH_MAX];
  bool active;
} bench_workspace_t;

typedef struct bench_case bench_case_t;

typedef froth_error_t (*bench_prepare_fn)(bench_case_t *bench_case);
typedef froth_error_t (*bench_run_fn)(bench_case_t *bench_case);
typedef void (*bench_cleanup_fn)(bench_case_t *bench_case);

typedef struct {
  size_t iterations;
  uint64_t total_ns;
  size_t peak_eval_values;
  size_t peak_objects;
} bench_result_t;

struct bench_case {
  const char *name;
  bench_prepare_fn prepare;
  bench_run_fn run;
  bench_cleanup_fn cleanup;
  const char *setup_source;
  const char *run_source;
  bool install_test_ffi;
  bool use_workspace;
  bool seed_snapshot;
  frothy_ir_program_t program;
  bench_workspace_t workspace;
};

static bool bench_platform_initialized = false;
static bool bench_snapshot_runtime_ready = false;

static const uint64_t bench_target_ns = 250000000ull;

static frothy_runtime_t *runtime(void) {
  return &froth_vm.frothy_runtime;
}

static uint64_t bench_now_ns(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static void release_value(frothy_value_t *value) {
  (void)frothy_value_release(runtime(), *value);
  *value = frothy_value_make_nil();
}

static froth_error_t ensure_platform_runtime(void) {
  if (bench_platform_initialized) {
    return FROTH_OK;
  }

  FROTH_TRY(platform_init());
  bench_platform_initialized = true;
  return FROTH_OK;
}

static froth_error_t reset_frothy_state(bool install_base_image) {
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
  froth_vm.watermark_heap_offset = 0;
  froth_cellspace_init(&froth_vm.cellspace);
  froth_tbuf_init(&froth_vm);
  frothy_runtime_init(runtime(), &froth_vm.cellspace);
  if (install_base_image) {
    FROTH_TRY(frothy_base_image_install());
    froth_vm.watermark_heap_offset = froth_vm.heap.pointer;
  }
  return FROTH_OK;
}

static froth_error_t reset_snapshot_state(void) {
  froth_vm.ds.pointer = 0;
  froth_vm.rs.pointer = 0;
  froth_vm.cs.pointer = 0;
  froth_vm.boot_complete = 1;
  froth_vm.trampoline_depth = 0;
  froth_vm.interrupted = 0;
  froth_vm.thrown = FROTH_OK;
  froth_vm.last_error_slot = -1;
  froth_vm.mark_offset = (froth_cell_u_t)-1;
  froth_tbuf_init(&froth_vm);

  if (!bench_snapshot_runtime_ready) {
    FROTH_TRY(reset_frothy_state(true));
    bench_snapshot_runtime_ready = true;
    return FROTH_OK;
  }

  return frothy_base_image_reset();
}

static froth_error_t compile_source(const char *source,
                                    frothy_ir_program_t *program) {
  frothy_ir_program_init(program);
  return frothy_parse_top_level(source, program);
}

static froth_error_t eval_program(const frothy_ir_program_t *program) {
  frothy_value_t value = frothy_value_make_nil();
  froth_error_t err;

  err = frothy_eval_program(program, &value);
  if (err == FROTH_OK) {
    release_value(&value);
  }
  return err;
}

static froth_error_t eval_source(const char *source) {
  frothy_ir_program_t program;
  froth_error_t err;

  err = compile_source(source, &program);
  if (err == FROTH_OK) {
    err = eval_program(&program);
  }
  frothy_ir_program_free(&program);
  return err;
}

static froth_error_t eval_setup_lines(const char *source) {
  char *copy;
  char *cursor;
  char *line;
  froth_error_t err = FROTH_OK;

  if (source == NULL) {
    return FROTH_OK;
  }

  copy = strdup(source);
  if (copy == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  cursor = copy;
  while ((line = strsep(&cursor, "\n")) != NULL) {
    if (*line == '\0') {
      continue;
    }
    err = eval_source(line);
    if (err != FROTH_OK) {
      fprintf(stderr, "setup line failed: `%s` -> %d\n", line, (int)err);
      break;
    }
  }

  free(copy);
  return err;
}

static froth_error_t enter_workspace(bench_workspace_t *workspace) {
  char template_path[] = "/tmp/frothy-bench.XXXXXX";

  memset(workspace, 0, sizeof(*workspace));
  if (getcwd(workspace->original_dir, sizeof(workspace->original_dir)) == NULL) {
    return FROTH_ERROR_IO;
  }
  if (mkdtemp(template_path) == NULL) {
    return FROTH_ERROR_IO;
  }
  if (snprintf(workspace->path, sizeof(workspace->path), "%s", template_path) >=
      (int)sizeof(workspace->path)) {
    return FROTH_ERROR_IO;
  }
  if (chdir(workspace->path) != 0) {
    return FROTH_ERROR_IO;
  }
  workspace->active = true;
  return FROTH_OK;
}

static void leave_workspace(bench_workspace_t *workspace) {
  char snapshot_a[PATH_MAX];
  char snapshot_b[PATH_MAX];

  if (!workspace->active) {
    return;
  }

  (void)snprintf(snapshot_a, sizeof(snapshot_a), "%s/%s", workspace->path,
                 FROTH_SNAPSHOT_PATH_A);
  (void)snprintf(snapshot_b, sizeof(snapshot_b), "%s/%s", workspace->path,
                 FROTH_SNAPSHOT_PATH_B);
  (void)remove(snapshot_a);
  (void)remove(snapshot_b);
  (void)chdir(workspace->original_dir);
  (void)rmdir(workspace->path);
  workspace->active = false;
}

FROTH_FFI_ARITY(bench_echo_int, "echo.int", "( value -- value )", 1, 1,
                "Benchmark integer echo binding") {
  FROTH_POP(value);
  FROTH_PUSH(value);
  return FROTH_OK;
}

static const froth_ffi_entry_t bench_bindings[] = {
    FROTH_BIND(bench_echo_int),
    {0},
};

static froth_error_t prepare_program_case(bench_case_t *bench_case) {
  FROTH_TRY(reset_frothy_state(false));

  if (bench_case->install_test_ffi) {
    FROTH_TRY(frothy_ffi_install_binding_table(bench_bindings));
  }
  FROTH_TRY(eval_setup_lines(bench_case->setup_source));

  return compile_source(bench_case->run_source, &bench_case->program);
}

static froth_error_t run_program_case(bench_case_t *bench_case) {
  return eval_program(&bench_case->program);
}

static void cleanup_program_case(bench_case_t *bench_case) {
  frothy_ir_program_free(&bench_case->program);
}

static froth_error_t prepare_parse_case(bench_case_t *bench_case) {
  (void)bench_case;
  return reset_frothy_state(false);
}

static froth_error_t run_parse_case(bench_case_t *bench_case) {
  frothy_ir_program_t program;
  froth_error_t err;

  err = compile_source(bench_case->run_source, &program);
  if (err != FROTH_OK) {
    return err;
  }

  frothy_ir_program_free(&program);
  return FROTH_OK;
}

static void cleanup_parse_case(bench_case_t *bench_case) {
  (void)bench_case;
}

static froth_error_t prepare_snapshot_case(bench_case_t *bench_case) {
  FROTH_TRY(enter_workspace(&bench_case->workspace));
  FROTH_TRY(reset_snapshot_state());
  FROTH_TRY(eval_setup_lines(
      "label = \"alpha\"\n"
      "adder = fn(x) { x + 1 }\n"
      "frame = cells(4)\n"
      "seedFrame = fn() { set frame[0] = \"ready\" set frame[1] = 7 set frame[2] = true set frame[3] = nil nil }\n"
      "seedFrame()"));
  if (bench_case->seed_snapshot) {
    FROTH_TRY(frothy_snapshot_save());
  }
  return FROTH_OK;
}

static froth_error_t run_snapshot_save_case(bench_case_t *bench_case) {
  (void)bench_case;
  return frothy_snapshot_save();
}

static froth_error_t run_snapshot_restore_case(bench_case_t *bench_case) {
  (void)bench_case;
  return frothy_snapshot_restore();
}

static void cleanup_snapshot_case(bench_case_t *bench_case) {
  leave_workspace(&bench_case->workspace);
}

static froth_error_t bench_time_case(bench_case_t *bench_case,
                                     size_t iterations,
                                     bench_result_t *out) {
  froth_error_t err;
  uint64_t start_ns;
  uint64_t end_ns;
  size_t i;

  err = bench_case->prepare(bench_case);
  if (err != FROTH_OK) {
    fprintf(stderr, "%s prepare failed: %d\n", bench_case->name, (int)err);
    return err;
  }
  err = bench_case->run(bench_case);
  if (err != FROTH_OK) {
    fprintf(stderr, "%s warmup failed: %d\n", bench_case->name, (int)err);
    bench_case->cleanup(bench_case);
    return err;
  }
  frothy_runtime_debug_reset_high_water(runtime());

  start_ns = bench_now_ns();
  for (i = 0; i < iterations; i++) {
    err = bench_case->run(bench_case);
    if (err != FROTH_OK) {
      fprintf(stderr, "%s timed run failed at %zu: %d\n", bench_case->name, i,
              (int)err);
      bench_case->cleanup(bench_case);
      return err;
    }
  }
  end_ns = bench_now_ns();

  out->iterations = iterations;
  out->total_ns = end_ns - start_ns;
  out->peak_eval_values = frothy_runtime_eval_value_high_water(runtime());
  out->peak_objects = frothy_runtime_object_high_water(runtime());
  bench_case->cleanup(bench_case);
  return FROTH_OK;
}

static froth_error_t calibrate_iterations(bench_case_t *bench_case,
                                          size_t *iterations_out) {
  bench_result_t result;
  size_t iterations = 1;

  while (1) {
    froth_error_t err = bench_time_case(bench_case, iterations, &result);
    uint64_t scaled;
    size_t next_iterations;

    if (err != FROTH_OK) {
      return err;
    }
    if (result.total_ns >= bench_target_ns) {
      *iterations_out = iterations;
      return FROTH_OK;
    }

    if (result.total_ns == 0) {
      iterations *= 10;
      continue;
    }

    scaled = (bench_target_ns * (uint64_t)iterations) / result.total_ns;
    next_iterations = (size_t)scaled;
    if (next_iterations <= iterations) {
      next_iterations = iterations * 2;
    }
    iterations = next_iterations + (next_iterations / 10) + 1;
  }
}

static void print_result(const bench_case_t *bench_case,
                         const bench_result_t *result) {
  double total_ms = (double)result->total_ns / 1000000.0;
  double ns_per_iter = (double)result->total_ns / (double)result->iterations;

  printf("%-16s %12zu %12.3f %14.1f %18zu %13zu\n", bench_case->name,
         result->iterations, total_ms, ns_per_iter, result->peak_eval_values,
         result->peak_objects);
}

static int run_single_case(bench_case_t *bench_case) {
  bench_result_t result;
  size_t iterations = 0;
  froth_error_t err = calibrate_iterations(bench_case, &iterations);

  if (err != FROTH_OK) {
    fprintf(stderr, "%s calibration failed: %d\n", bench_case->name, (int)err);
    bench_case->cleanup(bench_case);
    return 1;
  }

  while (1) {
    uint64_t scaled;
    size_t next_iterations;

    err = bench_time_case(bench_case, iterations, &result);
    if (err != FROTH_OK) {
      fprintf(stderr, "%s benchmark failed: %d\n", bench_case->name, (int)err);
      bench_case->cleanup(bench_case);
      return 1;
    }
    if (result.total_ns >= bench_target_ns) {
      break;
    }

    if (result.total_ns == 0) {
      iterations *= 2;
      continue;
    }

    scaled = (bench_target_ns * (uint64_t)iterations) / result.total_ns;
    next_iterations = (size_t)scaled;
    if (next_iterations <= iterations) {
      next_iterations = iterations * 2;
    }
    iterations = next_iterations + (next_iterations / 10) + 1;
  }

  print_result(bench_case, &result);
  return 0;
}

int main(void) {
  bench_case_t bench_cases[] = {
      {.name = "arithmetic",
       .prepare = prepare_program_case,
       .run = run_program_case,
       .cleanup = cleanup_program_case,
       .setup_source =
           "sumTo = fn(limit) { total = 0 i = 0 while i < limit { "
           "set total = total + i set i = i + 1 } total }",
       .run_source = "sumTo(64)"},
      {.name = "call_dispatch",
       .prepare = prepare_program_case,
       .run = run_program_case,
       .cleanup = cleanup_program_case,
       .setup_source = "hop = fn(v) { v }",
       .run_source = "hop(7)"},
      {.name = "slot_access",
       .prepare = prepare_program_case,
       .run = run_program_case,
       .cleanup = cleanup_program_case,
       .setup_source = "counter = 0",
       .run_source = "counter = counter + 1"},
      {.name = "parse_named_fn",
       .prepare = prepare_parse_case,
       .run = run_parse_case,
       .cleanup = cleanup_parse_case,
       .run_source =
           "pulse(pin, duration) { label = \"tick\"; total = 0; i = 0; "
           "while i < duration { set total = total + i; set i = i + 1 }; "
           "total }"},
      {.name = "snapshot_save",
       .prepare = prepare_snapshot_case,
       .run = run_snapshot_save_case,
       .cleanup = cleanup_snapshot_case,
       .use_workspace = true,
       .seed_snapshot = false},
      {.name = "snapshot_restore",
       .prepare = prepare_snapshot_case,
       .run = run_snapshot_restore_case,
       .cleanup = cleanup_snapshot_case,
       .use_workspace = true,
       .seed_snapshot = true},
      {.name = "ffi_loop",
       .prepare = prepare_program_case,
       .run = run_program_case,
       .cleanup = cleanup_program_case,
       .run_source = "echo.int(7)",
       .install_test_ffi = true},
  };
  const size_t case_count = sizeof(bench_cases) / sizeof(bench_cases[0]);
  size_t i;

  if (ensure_platform_runtime() != FROTH_OK) {
    fprintf(stderr, "failed to initialize platform runtime\n");
    return 1;
  }

  printf("Frothy runtime benchmark\n");
  printf("eval_value_capacity: %d\n", FROTHY_EVAL_VALUE_CAPACITY);
  printf("object_capacity: %d\n\n", FROTHY_OBJECT_CAPACITY);
  printf("%-16s %12s %12s %14s %18s %13s\n", "case", "iterations",
         "total_ms", "ns_per_iter", "peak_eval_values", "peak_objects");
  fflush(stdout);

  for (i = 0; i < case_count; i++) {
    pid_t child = fork();
    int status = 0;

    if (child < 0) {
      perror("fork");
      return 1;
    }
    if (child == 0) {
      int child_status = run_single_case(&bench_cases[i]);

      fflush(stdout);
      fflush(stderr);
      _exit(child_status);
    }
    if (waitpid(child, &status, 0) < 0) {
      perror("waitpid");
      return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      return 1;
    }
  }

  return 0;
}
