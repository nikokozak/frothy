#pragma once

#include "froth_types.h"

#include <stdbool.h>

typedef struct {
  bool snapshot_found;
  froth_error_t restore_error;
  bool boot_attempted;
  froth_error_t boot_error;
} frothy_startup_report_t;

froth_error_t frothy_boot_run_startup(frothy_startup_report_t *report);
froth_error_t frothy_boot(void);

void frothy_boot_test_set_skip_boot(bool skip);
