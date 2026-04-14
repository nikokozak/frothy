#pragma once

#include "froth_types.h"
#include "frothy_value.h"

#include <stdbool.h>

#ifndef FROTHY_SHELL_SOURCE_CAPACITY
#define FROTHY_SHELL_SOURCE_CAPACITY 2048
#endif

typedef enum {
  FROTHY_SHELL_EVAL_PHASE_NONE = 0,
  FROTHY_SHELL_EVAL_PHASE_PARSE = 1,
  FROTHY_SHELL_EVAL_PHASE_EVAL = 2,
} frothy_shell_eval_phase_t;

typedef struct {
  frothy_value_t value;
  char *rendered;
  bool suppress_raw_output;
  frothy_shell_eval_phase_t phase;
} frothy_shell_eval_result_t;

froth_error_t frothy_shell_run(void);
bool frothy_shell_is_idle(void);
froth_error_t frothy_shell_eval_source(const char *source,
                                       frothy_shell_eval_result_t *out);
void frothy_shell_eval_result_free(frothy_shell_eval_result_t *result);

#ifdef FROTHY_SHELL_TESTING
void frothy_shell_test_reset_pending_source(void);
froth_error_t frothy_shell_test_append_pending_line(const char *line);
const char *frothy_shell_test_pending_source(void);
size_t frothy_shell_test_pending_length(void);
bool frothy_shell_test_pending_is_complete(void);
bool frothy_shell_test_rewrite_simple_call(const char *command, char *buffer,
                                           size_t capacity);
#endif
