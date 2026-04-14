#pragma once

#include "frothy_value.h"

#include <stdbool.h>

typedef struct {
  bool is_overlay;
  frothy_value_class_t value_class;
  bool has_call_shape;
  uint8_t in_arity;
  uint8_t out_arity;
  const char *owner;
  const char *persistence;
  const char *effect;
  const char *help;
  char *rendered;
} frothy_inspect_binding_view_t;

typedef enum {
  FROTHY_INSPECT_RENDER_SURFACE = 0,
  FROTHY_INSPECT_RENDER_CORE = 1,
} frothy_inspect_render_mode_t;

typedef enum {
  FROTHY_INSPECT_REPORT_SLOT_INFO = 0,
  FROTHY_INSPECT_REPORT_SEE = 1,
  FROTHY_INSPECT_REPORT_CORE = 2,
} frothy_inspect_report_mode_t;

const char *frothy_inspect_class_name(frothy_value_class_t value_class);

froth_error_t frothy_inspect_collect_words(const char ***names_out,
                                           size_t *count_out);
void frothy_inspect_free_words(const char **names);
froth_error_t frothy_inspect_render_binding_text(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_render_mode_t mode, char **out_text);
froth_error_t frothy_inspect_render_binding_view(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_binding_view_t *view_out);
froth_error_t frothy_inspect_render_binding_report(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_report_mode_t mode, char **out_text);
void frothy_inspect_binding_view_free(frothy_inspect_binding_view_t *view);

froth_error_t frothy_builtin_words(frothy_runtime_t *runtime,
                                   const void *context,
                                   const frothy_value_t *args,
                                   size_t arg_count, frothy_value_t *out);
froth_error_t frothy_builtin_see(frothy_runtime_t *runtime,
                                 const void *context,
                                 const frothy_value_t *args,
                                 size_t arg_count, frothy_value_t *out);
froth_error_t frothy_builtin_core(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out);
froth_error_t frothy_builtin_slot_info(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count, frothy_value_t *out);
