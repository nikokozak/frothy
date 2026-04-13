#pragma once

#include "frothy_value.h"

#include <stdbool.h>

typedef struct {
  bool is_overlay;
  frothy_value_class_t value_class;
  char *rendered;
} frothy_inspect_binding_view_t;

const char *frothy_inspect_class_name(frothy_value_class_t value_class);
const char *frothy_inspect_persistability_name(
    frothy_value_class_t value_class);
const char *frothy_inspect_ownership_name(frothy_value_class_t value_class);

froth_error_t frothy_inspect_collect_words(const char ***names_out,
                                           size_t *count_out);
void frothy_inspect_free_words(const char **names);
froth_error_t frothy_inspect_render_binding_view(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_binding_view_t *view_out);
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
