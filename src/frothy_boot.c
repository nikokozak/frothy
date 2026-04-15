#include "frothy_boot.h"

#include "froth_slot_table.h"
#include "froth_tbuf.h"
#include "froth_vm.h"
#include "frothy_base_image.h"
#include "frothy_eval.h"
#include "frothy_parser.h"
#include "frothy_shell.h"
#include "frothy_snapshot.h"
#include "frothy_value.h"
#include "platform.h"

#include <stdbool.h>
#include <stdio.h>

#ifdef FROTH_HAS_SNAPSHOTS
#include "froth_snapshot.h"
#endif

static froth_error_t frothy_emit_text(const char *text) {
  while (*text != '\0') {
    FROTH_TRY(platform_emit((uint8_t)*text));
    text++;
  }
  return FROTH_OK;
}

static froth_error_t frothy_emit_line(const char *text) {
  FROTH_TRY(frothy_emit_text(text));
  return platform_emit('\n');
}

static void frothy_boot_fail(const char *step, froth_error_t err) {
  char buffer[96];

  snprintf(buffer, sizeof(buffer), "frothy boot: %s failed (%d)\n", step,
           (int)err);
  (void)frothy_emit_text(buffer);
}

static froth_error_t frothy_poll_for_safe_boot(bool *safe_boot) {
  size_t i;

  *safe_boot = false;
  FROTH_TRY(frothy_emit_line("boot: CTRL-C for safe boot"));

  for (i = 0; i < 75; i++) {
    platform_delay_ms(10);
    platform_check_interrupt(&froth_vm);
    if (froth_vm.interrupted) {
      froth_vm.interrupted = 0;
      *safe_boot = true;
    }

    if (*safe_boot) {
      break;
    }
  }

  return FROTH_OK;
}

static froth_error_t frothy_boot_eval_source(const char *source) {
  frothy_ir_program_t program;
  frothy_value_t value = frothy_value_make_nil();
  froth_error_t err;

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(source, &program);
  if (err == FROTH_OK) {
    err = frothy_eval_program(&program, &value);
  }
  if (err == FROTH_OK) {
    err = frothy_value_release(&froth_vm.frothy_runtime, value);
  }
  frothy_ir_program_free(&program);
  return err;
}

static froth_error_t frothy_boot_should_run(bool *should_run) {
  froth_cell_u_t slot_index = 0;
  froth_cell_t impl = 0;
  frothy_value_class_t value_class;
  froth_error_t err;

  *should_run = false;

  err = froth_slot_find_name("boot", &slot_index);
  if (err == FROTH_ERROR_UNDEFINED_WORD) {
    return FROTH_OK;
  }
  FROTH_TRY(err);

  err = froth_slot_get_impl(slot_index, &impl);
  if (err == FROTH_ERROR_UNDEFINED_WORD) {
    return FROTH_OK;
  }
  FROTH_TRY(err);

  FROTH_TRY(frothy_value_class(&froth_vm.frothy_runtime,
                               frothy_value_from_cell(impl), &value_class));
  *should_run = value_class == FROTHY_VALUE_CLASS_CODE;
  return FROTH_OK;
}

static froth_error_t
frothy_boot_report_error(frothy_startup_report_t *report, bool restore_phase,
                         froth_error_t err) {
  if (report != NULL) {
    if (restore_phase) {
      report->restore_error = err;
    } else {
      report->boot_error = err;
    }
    return FROTH_OK;
  }

  return err;
}

static froth_error_t frothy_boot_test_pick_active_error = FROTH_OK;
static bool frothy_boot_test_skip_boot = false;

void frothy_boot_test_set_pick_active_error(froth_error_t err) {
  frothy_boot_test_pick_active_error = err;
}

void frothy_boot_test_set_skip_boot(bool skip) {
  frothy_boot_test_skip_boot = skip;
}

froth_error_t frothy_boot_run_startup(frothy_startup_report_t *report) {
  bool should_run = false;
  froth_error_t err;

  if (report != NULL) {
    report->snapshot_found = false;
    report->restore_error = FROTH_OK;
    report->boot_attempted = false;
    report->boot_error = FROTH_OK;
  }

  err = frothy_base_image_reset();
  if (err != FROTH_OK) {
    if (report != NULL) {
      report->restore_error = err;
    }
    return err;
  }

#ifdef FROTH_HAS_SNAPSHOTS
  {
    uint8_t slot = 0;
    uint32_t generation = 0;

    err = frothy_boot_test_pick_active_error;
    frothy_boot_test_pick_active_error = FROTH_OK;
    if (err == FROTH_OK) {
      err = froth_snapshot_pick_active(&slot, &generation);
    }
    if (err == FROTH_OK) {
      if (report != NULL) {
        report->snapshot_found = true;
      }
      err = frothy_snapshot_restore();
      if (err != FROTH_OK) {
        return frothy_boot_report_error(report, true, err);
      }
    } else if (err != FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT) {
      return frothy_boot_report_error(report, true, err);
    }
  }
#endif

  err = frothy_boot_should_run(&should_run);
  if (err != FROTH_OK) {
    return frothy_boot_report_error(report, false, err);
  }
  if (frothy_boot_test_skip_boot) {
    should_run = false;
  }
  if (!should_run) {
    return FROTH_OK;
  }

  if (report != NULL) {
    report->boot_attempted = true;
  }
  err = frothy_boot_eval_source("boot()");
  if (err != FROTH_OK) {
    return frothy_boot_report_error(report, false, err);
  }
  return FROTH_OK;
}

froth_error_t frothy_boot(void) {
  froth_error_t err;
  bool snapshot_found = false;
  bool safe_boot = false;
  frothy_startup_report_t startup;

  err = platform_init();
  if (err != FROTH_OK) {
    frothy_boot_fail("platform init", err);
    return err;
  }

  froth_cellspace_init(&froth_vm.cellspace);
  froth_tbuf_init(&froth_vm);
  frothy_runtime_init(&froth_vm.frothy_runtime, &froth_vm.cellspace);

  err = frothy_base_image_install();
  if (err != FROTH_OK) {
    frothy_boot_fail("seed slots", err);
    return err;
  }

  froth_vm.boot_complete = 1;
  froth_vm.watermark_heap_offset = froth_vm.heap.pointer;

  FROTH_TRY(frothy_emit_line("Frothy shell"));
#ifdef FROTH_HAS_SNAPSHOTS
  {
    uint8_t slot;
    uint32_t generation;

    snapshot_found = froth_snapshot_pick_active(&slot, &generation) == FROTH_OK;
  }
#endif
  FROTH_TRY(frothy_emit_line(snapshot_found ? "snapshot: found"
                                            : "snapshot: none"));
  FROTH_TRY(frothy_poll_for_safe_boot(&safe_boot));

  if (safe_boot) {
    FROTH_TRY(frothy_emit_line("boot: Safe Boot, skipped restore and boot."));
  } else {
    err = frothy_boot_run_startup(&startup);
    if (err != FROTH_OK) {
      frothy_boot_fail("startup", err);
      return err;
    }
    if (startup.restore_error != FROTH_OK) {
      frothy_boot_fail("restore", startup.restore_error);
    }
    if (startup.boot_error != FROTH_OK) {
      frothy_boot_fail("boot", startup.boot_error);
    }
  }

  FROTH_TRY(frothy_emit_line("Type help for commands."));

  return frothy_shell_run();
}
