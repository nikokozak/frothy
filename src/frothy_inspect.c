#include "frothy_inspect.h"

#include "frothy_base_image.h"
#include "froth_ffi.h"
#include "froth_slot_table.h"
#include "frothy_ir.h"
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  const char *help;
} frothy_inspect_builtin_doc_t;

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} frothy_inspect_text_builder_t;

typedef struct {
  froth_cell_u_t slot_index;
  bool is_overlay;
  frothy_value_t value;
  frothy_value_class_t value_class;
  bool has_call_shape;
  uint8_t in_arity;
  uint8_t out_arity;
  const frothy_ir_program_t *program;
  frothy_ir_node_id_t body;
  size_t arity;
  size_t local_count;
  const void *native_context;
  const char *native_name;
} frothy_inspect_binding_t;

static const frothy_inspect_builtin_doc_t frothy_inspect_builtin_docs[] = {
    {"save", "Save the current overlay snapshot."},
    {"restore", "Restore the most recently saved snapshot."},
    {"dangerous.wipe", "Erase the saved snapshot and clear overlay bindings."},
    {"words", "List bound top-level slot names."},
    {"see", "Show the readable form of a slot binding."},
    {"core", "Show the normalized core form of a slot binding."},
    {"slotInfo", "Show slot metadata for a binding."},
};

static froth_error_t
frothy_inspect_render_binding(frothy_runtime_t *runtime,
                              const char *name,
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

static void
frothy_inspect_text_builder_free(frothy_inspect_text_builder_t *builder) {
  if (builder == NULL) {
    return;
  }

  free(builder->data);
  memset(builder, 0, sizeof(*builder));
}

static froth_error_t
frothy_inspect_text_builder_reserve(frothy_inspect_text_builder_t *builder,
                                    size_t needed) {
  size_t capacity = builder->capacity == 0 ? 128 : builder->capacity;
  char *resized = NULL;

  if (needed <= builder->capacity) {
    return FROTH_OK;
  }

  while (capacity < needed) {
    capacity *= 2;
  }

  resized = (char *)realloc(builder->data, capacity);
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  builder->data = resized;
  builder->capacity = capacity;
  return FROTH_OK;
}

static froth_error_t
frothy_inspect_text_builder_append_len(frothy_inspect_text_builder_t *builder,
                                       const char *text, size_t length) {
  FROTH_TRY(frothy_inspect_text_builder_reserve(builder,
                                                builder->length + length + 1));
  memcpy(builder->data + builder->length, text, length);
  builder->length += length;
  builder->data[builder->length] = '\0';
  return FROTH_OK;
}

static froth_error_t
frothy_inspect_text_builder_append(frothy_inspect_text_builder_t *builder,
                                   const char *text) {
  return frothy_inspect_text_builder_append_len(builder, text, strlen(text));
}

static froth_error_t
frothy_inspect_text_builder_appendf(frothy_inspect_text_builder_t *builder,
                                    const char *format, ...) {
  va_list args;
  va_list copy;
  int needed = 0;
  froth_error_t err;

  va_start(args, format);
  va_copy(copy, args);
  needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return FROTH_ERROR_IO;
  }

  err = frothy_inspect_text_builder_reserve(builder,
                                            builder->length + (size_t)needed + 1);
  if (err != FROTH_OK) {
    va_end(args);
    return err;
  }
  (void)vsnprintf(builder->data + builder->length,
                  builder->capacity - builder->length, format, args);
  builder->length += (size_t)needed;
  va_end(args);
  return FROTH_OK;
}

static char *
frothy_inspect_text_builder_take(frothy_inspect_text_builder_t *builder) {
  char *text = builder->data;

  if (builder->length > 0 && text[builder->length - 1] == '\n') {
    text[builder->length - 1] = '\0';
  }
  builder->data = NULL;
  builder->length = 0;
  builder->capacity = 0;
  return text;
}

static const frothy_inspect_builtin_doc_t *
frothy_inspect_lookup_builtin_doc(const char *name) {
  size_t i;

  for (i = 0; i < sizeof(frothy_inspect_builtin_docs) /
                     sizeof(frothy_inspect_builtin_docs[0]);
       i++) {
    if (strcmp(frothy_inspect_builtin_docs[i].name, name) == 0) {
      return &frothy_inspect_builtin_docs[i];
    }
  }

  return NULL;
}

static const froth_ffi_entry_t *
frothy_inspect_binding_ffi_entry(const frothy_inspect_binding_t *binding) {
  if (binding->value_class != FROTHY_VALUE_CLASS_NATIVE ||
      binding->native_context == NULL) {
    return NULL;
  }

  return (const froth_ffi_entry_t *)binding->native_context;
}

static const char *
frothy_inspect_binding_owner(const frothy_inspect_binding_t *binding) {
  if (binding->value_class == FROTHY_VALUE_CLASS_NATIVE) {
    if (frothy_inspect_binding_ffi_entry(binding) != NULL) {
      return "board ffi";
    }
    if (frothy_inspect_lookup_builtin_doc(binding->native_name) != NULL) {
      return "runtime builtin";
    }
    return binding->is_overlay ? "overlay image" : "base image";
  }

  return binding->is_overlay ? "overlay image" : "base image";
}

static const char *
frothy_inspect_binding_persistence(const frothy_inspect_binding_t *binding) {
  if (binding->value_class == FROTHY_VALUE_CLASS_NATIVE ||
      !binding->is_overlay) {
    return "not saved";
  }
  if (binding->value_class == FROTHY_VALUE_CLASS_CELLS) {
    return "saved if contents are persistable";
  }

  return "saved in snapshot";
}

static const char *
frothy_inspect_binding_effect(const frothy_inspect_binding_t *binding) {
  const froth_ffi_entry_t *entry =
      frothy_inspect_binding_ffi_entry(binding);

  if (entry == NULL || entry->stack_effect == NULL ||
      entry->stack_effect[0] == '\0') {
    return NULL;
  }

  return entry->stack_effect;
}

static const char *
frothy_inspect_binding_help(const frothy_inspect_binding_t *binding) {
  const froth_ffi_entry_t *entry =
      frothy_inspect_binding_ffi_entry(binding);
  const frothy_inspect_builtin_doc_t *builtin_doc = NULL;

  if (entry != NULL && entry->help != NULL && entry->help[0] != '\0') {
    return entry->help;
  }
  if (binding->value_class != FROTHY_VALUE_CLASS_NATIVE ||
      binding->native_context != NULL) {
    return NULL;
  }

  builtin_doc = frothy_inspect_lookup_builtin_doc(binding->native_name);
  if (builtin_doc == NULL) {
    return NULL;
  }
  return builtin_doc->help;
}

static void frothy_inspect_binding_view_from_binding(
    const frothy_inspect_binding_t *binding,
    frothy_inspect_binding_view_t *view_out) {
  memset(view_out, 0, sizeof(*view_out));
  view_out->is_overlay = binding->is_overlay;
  view_out->value_class = binding->value_class;
  view_out->has_call_shape = binding->has_call_shape;
  view_out->in_arity = binding->in_arity;
  view_out->out_arity = binding->out_arity;
  view_out->owner = frothy_inspect_binding_owner(binding);
  view_out->persistence = frothy_inspect_binding_persistence(binding);
  view_out->effect = frothy_inspect_binding_effect(binding);
  view_out->help = frothy_inspect_binding_help(binding);
}

static froth_error_t
frothy_inspect_builder_append_field(frothy_inspect_text_builder_t *builder,
                                    const char *label, const char *value) {
  if (value == NULL || value[0] == '\0') {
    return FROTH_OK;
  }

  return frothy_inspect_text_builder_appendf(builder, "  %s: %s\n", label,
                                             value);
}

static froth_error_t
frothy_inspect_builder_append_call(frothy_inspect_text_builder_t *builder,
                                   const frothy_inspect_binding_view_t *view) {
  if (view->has_call_shape) {
    return frothy_inspect_text_builder_appendf(
        builder, "  call: %u -> %u\n", (unsigned)view->in_arity,
        (unsigned)view->out_arity);
  }
  if (view->value_class == FROTHY_VALUE_CLASS_CODE ||
      view->value_class == FROTHY_VALUE_CLASS_NATIVE) {
    return frothy_inspect_text_builder_append(builder, "  call: callable\n");
  }

  return frothy_inspect_text_builder_append(builder,
                                            "  call: not callable\n");
}

static froth_error_t
frothy_inspect_builder_append_rendered(frothy_inspect_text_builder_t *builder,
                                       const char *label, const char *text) {
  const char *cursor = text;
  const char *line_end = NULL;
  bool first_line = true;

  if (text == NULL) {
    return FROTH_OK;
  }
  if (text[0] == '\0') {
    return frothy_inspect_text_builder_appendf(builder, "  %s:\n", label);
  }

  while (1) {
    line_end = strchr(cursor, '\n');
    if (line_end == NULL) {
      if (first_line) {
        return frothy_inspect_text_builder_appendf(builder, "  %s: %s\n", label,
                                                   cursor);
      }
      return frothy_inspect_text_builder_appendf(builder, "    %s\n", cursor);
    }

    if (first_line) {
      FROTH_TRY(frothy_inspect_text_builder_appendf(
          builder, "  %s: %.*s\n", label, (int)(line_end - cursor), cursor));
      first_line = false;
    } else {
      FROTH_TRY(frothy_inspect_text_builder_appendf(
          builder, "    %.*s\n", (int)(line_end - cursor), cursor));
    }
    cursor = line_end + 1;
  }
}

static froth_error_t
frothy_inspect_render_report_text(const char *name,
                                  const frothy_inspect_binding_view_t *view,
                                  frothy_inspect_report_mode_t mode,
                                  const char *rendered, char **out_text) {
  frothy_inspect_text_builder_t builder = {0};
  froth_error_t err = FROTH_OK;

  *out_text = NULL;

  err = frothy_inspect_text_builder_appendf(&builder, "%s\n", name);
  if (err == FROTH_OK) {
    err = frothy_inspect_builder_append_field(&builder, "slot",
                                              view->is_overlay ? "overlay"
                                                               : "base");
  }
  if (err == FROTH_OK) {
    err = frothy_inspect_builder_append_field(
        &builder, "kind", frothy_inspect_class_name(view->value_class));
  }
  if (err == FROTH_OK) {
    err = frothy_inspect_builder_append_call(&builder, view);
  }
  if (err == FROTH_OK) {
    err = frothy_inspect_builder_append_field(&builder, "owner", view->owner);
  }
  if (err == FROTH_OK) {
    err = frothy_inspect_builder_append_field(&builder, "persistence",
                                              view->persistence);
  }
  if (err == FROTH_OK) {
    err = frothy_inspect_builder_append_field(&builder, "effect", view->effect);
  }
  if (err == FROTH_OK) {
    err = frothy_inspect_builder_append_field(&builder, "help", view->help);
  }
  if (err == FROTH_OK && mode == FROTHY_INSPECT_REPORT_SEE) {
    err = frothy_inspect_builder_append_rendered(&builder, "see", rendered);
  }
  if (err == FROTH_OK && mode == FROTHY_INSPECT_REPORT_CORE) {
    err = frothy_inspect_builder_append_rendered(&builder, "core", rendered);
  }
  if (err != FROTH_OK) {
    frothy_inspect_text_builder_free(&builder);
    return err;
  }

  *out_text = frothy_inspect_text_builder_take(&builder);
  return FROTH_OK;
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
  case FROTHY_VALUE_CLASS_RECORD_DEF:
    return "record-def";
  case FROTHY_VALUE_CLASS_RECORD:
    return "record";
  }

  return "value";
}

static froth_error_t frothy_inspect_require_name_arg(frothy_runtime_t *runtime,
                                                     const frothy_value_t *args,
                                                     size_t arg_count,
                                                     const char **name_out) {
  size_t length = 0;
  froth_error_t err;

  if (arg_count != 1) {
    return FROTH_ERROR_SIGNATURE;
  }

  err = frothy_runtime_get_text(runtime, args[0], name_out, &length);
  if (err == FROTH_OK) {
    if (length == 0) {
      return FROTH_ERROR_BOUNDS;
    }
    return FROTH_OK;
  }
  if (err != FROTH_ERROR_TYPE_MISMATCH) {
    return err;
  }

  FROTH_TRY(frothy_value_get_slot_designator_name(args[0], name_out));
  length = strlen(*name_out);
  if (length == 0) {
    return FROTH_ERROR_BOUNDS;
  }

  return FROTH_OK;
}

static froth_error_t frothy_inspect_resolve_binding(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_binding_t *binding_out) {
  froth_cell_t impl = 0;
  size_t native_arity = 0;

  memset(binding_out, 0, sizeof(*binding_out));
  binding_out->body = FROTHY_IR_NODE_INVALID;
  binding_out->in_arity = FROTH_SLOT_ARITY_UNKNOWN;
  binding_out->out_arity = FROTH_SLOT_ARITY_UNKNOWN;

  FROTH_TRY(froth_slot_find_name(name, &binding_out->slot_index));
  binding_out->is_overlay = froth_slot_is_overlay(binding_out->slot_index);
  FROTH_TRY(froth_slot_get_impl(binding_out->slot_index, &impl));
  binding_out->value = frothy_value_from_cell(impl);
  FROTH_TRY(
      frothy_value_class(runtime, binding_out->value, &binding_out->value_class));

  if (froth_slot_get_arity(binding_out->slot_index, &binding_out->in_arity,
                           &binding_out->out_arity) == FROTH_OK &&
      binding_out->in_arity < FROTH_SLOT_ARITY_UNKNOWN &&
      binding_out->out_arity < FROTH_SLOT_ARITY_UNKNOWN) {
    binding_out->has_call_shape = true;
  }

  if (binding_out->value_class == FROTHY_VALUE_CLASS_CODE) {
    FROTH_TRY(frothy_runtime_get_code(
        runtime, binding_out->value, &binding_out->program, &binding_out->body,
        &binding_out->arity, &binding_out->local_count));
    if (!binding_out->has_call_shape &&
        binding_out->arity < FROTH_SLOT_ARITY_UNKNOWN) {
      binding_out->has_call_shape = true;
      binding_out->in_arity = (uint8_t)binding_out->arity;
      binding_out->out_arity = 1;
    }
  } else if (binding_out->value_class == FROTHY_VALUE_CLASS_NATIVE) {
    FROTH_TRY(frothy_runtime_get_native(runtime, binding_out->value, NULL,
                                        &binding_out->native_context,
                                        &binding_out->native_name,
                                        &native_arity));
    if (!binding_out->has_call_shape &&
        native_arity < FROTH_SLOT_ARITY_UNKNOWN) {
      binding_out->has_call_shape = true;
      binding_out->in_arity = (uint8_t)native_arity;
      binding_out->out_arity = 1;
    }
  }

  return FROTH_OK;
}

froth_error_t frothy_inspect_render_binding_text(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_render_mode_t mode, char **out_text) {
  frothy_inspect_binding_t binding;

  FROTH_TRY(frothy_inspect_resolve_binding(runtime, name, &binding));
  if (mode == FROTHY_INSPECT_RENDER_CORE &&
      binding.value_class == FROTHY_VALUE_CLASS_CODE) {
    return frothy_ir_render_code(binding.program, binding.body, binding.arity,
                                 binding.local_count, out_text);
  }

  return frothy_inspect_render_binding(runtime, name, &binding, out_text);
}

static froth_error_t
frothy_inspect_render_binding(frothy_runtime_t *runtime,
                              const char *name,
                              const frothy_inspect_binding_t *binding,
                              char **out_text) {
  if (binding->value_class == FROTHY_VALUE_CLASS_CODE) {
    return frothy_ir_render_surface_code(binding->program, binding->body,
                                         binding->arity, binding->local_count,
                                         name, out_text);
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

  FROTH_TRY(frothy_inspect_resolve_binding(runtime, name, &binding));
  frothy_inspect_binding_view_from_binding(&binding, view_out);
  return frothy_inspect_render_binding(runtime, name, &binding,
                                       &view_out->rendered);
}

froth_error_t frothy_inspect_render_binding_report(
    frothy_runtime_t *runtime, const char *name,
    frothy_inspect_report_mode_t mode, char **out_text) {
  frothy_inspect_binding_t binding;
  frothy_inspect_binding_view_t view = {0};
  char *rendered = NULL;
  froth_error_t err = FROTH_OK;

  *out_text = NULL;

  FROTH_TRY(frothy_inspect_resolve_binding(runtime, name, &binding));
  frothy_inspect_binding_view_from_binding(&binding, &view);
  if (mode == FROTHY_INSPECT_REPORT_SEE) {
    err = frothy_inspect_render_binding(runtime, name, &binding, &rendered);
  } else if (mode == FROTHY_INSPECT_REPORT_CORE) {
    err = frothy_inspect_render_binding_text(runtime, name,
                                             FROTHY_INSPECT_RENDER_CORE,
                                             &rendered);
  }
  if (err == FROTH_OK) {
    err = frothy_inspect_render_report_text(name, &view, mode, rendered,
                                            out_text);
  }

  free(rendered);
  return err;
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
  char *report = NULL;
  froth_error_t err;

  (void)context;
  err = frothy_inspect_require_name_arg(runtime, args, arg_count, &name);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_inspect_render_binding_report(runtime, name,
                                             FROTHY_INSPECT_REPORT_SEE,
                                             &report);
  if (err != FROTH_OK) {
    return err;
  }

  err = frothy_emit_line(report);
  free(report);
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
  char *report = NULL;
  froth_error_t err;

  (void)context;
  err = frothy_inspect_require_name_arg(runtime, args, arg_count, &name);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_inspect_render_binding_report(runtime, name,
                                             FROTHY_INSPECT_REPORT_CORE,
                                             &report);
  if (err != FROTH_OK) {
    return err;
  }

  err = frothy_emit_line(report);
  free(report);
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
  char *report = NULL;
  froth_error_t err;

  (void)context;
  err = frothy_inspect_require_name_arg(runtime, args, arg_count, &name);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_inspect_render_binding_report(runtime, name,
                                             FROTHY_INSPECT_REPORT_SLOT_INFO,
                                             &report);
  if (err != FROTH_OK) {
    return err;
  }

  err = frothy_emit_line(report);
  free(report);
  if (err != FROTH_OK) {
    return err;
  }

  *out = frothy_value_make_nil();
  return FROTH_OK;
}
