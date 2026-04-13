#include "frothy_inspect.h"

#include "frothy_base_image.h"
#include "froth_slot_table.h"
#include "frothy_ir.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  froth_cell_u_t slot_index;
  frothy_value_t value;
  frothy_value_class_t value_class;
  const frothy_ir_program_t *program;
  frothy_ir_node_id_t body;
  size_t arity;
  size_t local_count;
} frothy_inspect_binding_t;

static froth_error_t
frothy_inspect_render_binding(frothy_runtime_t *runtime,
                              const frothy_inspect_binding_t *binding,
                              char **out_text);

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

const char *frothy_inspect_class_name(frothy_value_class_t value_class) {
  switch (value_class) {
  case FROTHY_VALUE_CLASS_INT:
    return "int";
  case FROTHY_VALUE_CLASS_BOOL:
    return "bool";
  case FROTHY_VALUE_CLASS_NIL:
    return "nil";
  case FROTHY_VALUE_CLASS_TEXT:
    return "text";
  case FROTHY_VALUE_CLASS_CELLS:
    return "cells";
  case FROTHY_VALUE_CLASS_CODE:
    return "code";
  case FROTHY_VALUE_CLASS_NATIVE:
    return "native";
  }

  return "value";
}

const char *frothy_inspect_persistability_name(
    frothy_value_class_t value_class) {
  if (value_class == FROTHY_VALUE_CLASS_NATIVE) {
    return "non-persistable";
  }

  return "persistable";
}

const char *frothy_inspect_ownership_name(
    frothy_value_class_t value_class) {
  if (value_class == FROTHY_VALUE_CLASS_NATIVE) {
    return "foreign";
  }

  return "user";
}

static froth_error_t frothy_inspect_require_name_arg(frothy_runtime_t *runtime,
                                                     const frothy_value_t *args,
                                                     size_t arg_count,
                                                     const char **name_out) {
  size_t length = 0;

  if (arg_count != 1) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_runtime_get_text(runtime, args[0], name_out, &length));
  if (length == 0) {
    return FROTH_ERROR_BOUNDS;
  }

  return FROTH_OK;
}

static froth_error_t frothy_inspect_resolve_binding(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_binding_t *binding_out) {
  froth_cell_t impl = 0;

  memset(binding_out, 0, sizeof(*binding_out));
  binding_out->body = FROTHY_IR_NODE_INVALID;

  FROTH_TRY(froth_slot_find_name(name, &binding_out->slot_index));
  FROTH_TRY(froth_slot_get_impl(binding_out->slot_index, &impl));
  binding_out->value = frothy_value_from_cell(impl);
  FROTH_TRY(
      frothy_value_class(runtime, binding_out->value, &binding_out->value_class));
  if (binding_out->value_class == FROTHY_VALUE_CLASS_CODE) {
    FROTH_TRY(frothy_runtime_get_code(
        runtime, binding_out->value, &binding_out->program, &binding_out->body,
        &binding_out->arity, &binding_out->local_count));
  }

  return FROTH_OK;
}

static froth_error_t frothy_inspect_render_binding_name(
    frothy_runtime_t *runtime, const char *name, char **out_text) {
  frothy_inspect_binding_t binding;

  FROTH_TRY(frothy_inspect_resolve_binding(runtime, name, &binding));
  return frothy_inspect_render_binding(runtime, &binding, out_text);
}

static froth_error_t
frothy_inspect_render_binding(frothy_runtime_t *runtime,
                              const frothy_inspect_binding_t *binding,
                              char **out_text) {
  if (binding->value_class == FROTHY_VALUE_CLASS_CODE) {
    return frothy_ir_render_code(binding->program, binding->body, binding->arity,
                                 binding->local_count, out_text);
  }

  return frothy_value_render(runtime, binding->value, out_text);
}

froth_error_t frothy_inspect_collect_words(const char ***names_out,
                                           size_t *count_out) {
  froth_cell_u_t slot_count = froth_slot_count();
  const char **names = NULL;
  size_t count = 0;
  froth_cell_u_t slot_index;

  *names_out = NULL;
  *count_out = 0;

  if (slot_count == 0) {
    return FROTH_OK;
  }

  names = (const char **)calloc((size_t)slot_count, sizeof(*names));
  if (names == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    const char *name = NULL;
    froth_cell_t impl = 0;

    if (froth_slot_get_name(slot_index, &name) != FROTH_OK) {
      continue;
    }
    if (froth_slot_get_impl(slot_index, &impl) != FROTH_OK) {
      continue;
    }
    names[count++] = name;
  }

  *names_out = names;
  *count_out = count;
  return FROTH_OK;
}

void frothy_inspect_free_words(const char **names) { free((void *)names); }

froth_error_t frothy_inspect_render_binding_view(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_binding_view_t *view_out) {
  frothy_inspect_binding_t binding;

  memset(view_out, 0, sizeof(*view_out));
  FROTH_TRY(frothy_inspect_resolve_binding(runtime, name, &binding));
  view_out->is_overlay = froth_slot_is_overlay(binding.slot_index);
  view_out->value_class = binding.value_class;
  return frothy_inspect_render_binding(runtime, &binding, &view_out->rendered);
}

void frothy_inspect_binding_view_free(frothy_inspect_binding_view_t *view) {
  if (view == NULL) {
    return;
  }

  free(view->rendered);
  memset(view, 0, sizeof(*view));
}

froth_error_t frothy_builtin_words(frothy_runtime_t *runtime,
                                   const void *context,
                                   const frothy_value_t *args,
                                   size_t arg_count, frothy_value_t *out) {
  const char **names = NULL;
  size_t count = 0;
  size_t i;
  froth_error_t err;

  (void)runtime;
  (void)context;
  (void)args;
  if (arg_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }

  err = frothy_inspect_collect_words(&names, &count);
  if (err != FROTH_OK) {
    return err;
  }
  for (i = 0; i < count; i++) {
    err = frothy_emit_line(names[i]);
    if (err != FROTH_OK) {
      break;
    }
  }
  frothy_inspect_free_words(names);
  if (err != FROTH_OK) {
    return err;
  }

  *out = frothy_value_make_nil();
  return FROTH_OK;
}

froth_error_t frothy_builtin_see(frothy_runtime_t *runtime,
                                 const void *context,
                                 const frothy_value_t *args,
                                 size_t arg_count, frothy_value_t *out) {
  const char *name = NULL;
  frothy_inspect_binding_view_t view = {0};
  char header[160];
  froth_error_t err;

  (void)context;
  err = frothy_inspect_require_name_arg(runtime, args, arg_count, &name);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_inspect_render_binding_view(runtime, name, &view);
  if (err != FROTH_OK) {
    return err;
  }

  snprintf(header, sizeof(header), "%s | %s | %s", name,
           view.is_overlay ? "overlay" : "base",
           frothy_inspect_class_name(view.value_class));
  err = frothy_emit_line(header);
  if (err == FROTH_OK) {
    err = frothy_emit_line(view.rendered);
  }
  frothy_inspect_binding_view_free(&view);
  if (err != FROTH_OK) {
    return err;
  }

  *out = frothy_value_make_nil();
  return FROTH_OK;
}

froth_error_t frothy_builtin_core(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out) {
  const char *name = NULL;
  frothy_inspect_binding_view_t view = {0};
  froth_error_t err;

  (void)context;
  err = frothy_inspect_require_name_arg(runtime, args, arg_count, &name);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_inspect_render_binding_name(runtime, name, &view.rendered);
  if (err != FROTH_OK) {
    return err;
  }

  err = frothy_emit_line(view.rendered);
  frothy_inspect_binding_view_free(&view);
  if (err != FROTH_OK) {
    return err;
  }

  *out = frothy_value_make_nil();
  return FROTH_OK;
}

froth_error_t frothy_builtin_slot_info(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count, frothy_value_t *out) {
  const char *name = NULL;
  frothy_inspect_binding_view_t view = {0};
  char *line = NULL;
  int length;
  froth_error_t err;

  (void)context;
  err = frothy_inspect_require_name_arg(runtime, args, arg_count, &name);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_inspect_render_binding_view(runtime, name, &view);
  if (err != FROTH_OK) {
    return err;
  }

  length = snprintf(NULL, 0, "%s | %s | %s | %s | %s", name,
                    view.is_overlay ? "overlay" : "base",
                    frothy_inspect_class_name(view.value_class),
                    frothy_inspect_persistability_name(view.value_class),
                    frothy_inspect_ownership_name(view.value_class));
  if (length < 0) {
    frothy_inspect_binding_view_free(&view);
    return FROTH_ERROR_IO;
  }

  line = (char *)malloc((size_t)length + 1);
  if (line == NULL) {
    frothy_inspect_binding_view_free(&view);
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  snprintf(line, (size_t)length + 1, "%s | %s | %s | %s | %s", name,
           view.is_overlay ? "overlay" : "base",
           frothy_inspect_class_name(view.value_class),
           frothy_inspect_persistability_name(view.value_class),
           frothy_inspect_ownership_name(view.value_class));
  err = frothy_emit_line(line);
  free(line);
  frothy_inspect_binding_view_free(&view);
  if (err != FROTH_OK) {
    return err;
  }

  *out = frothy_value_make_nil();
  return FROTH_OK;
}
