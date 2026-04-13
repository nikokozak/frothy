#include "frothy_value.h"

#include "froth_slot_table.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FROTHY_VALUE_TAG_MASK ((frothy_value_t)0x3u)
#define FROTHY_VALUE_TAG_INT ((frothy_value_t)0x0u)
#define FROTHY_VALUE_TAG_SPECIAL ((frothy_value_t)0x1u)
#define FROTHY_VALUE_TAG_SLOT ((frothy_value_t)0x2u)
#define FROTHY_VALUE_TAG_OBJECT ((frothy_value_t)0x3u)

#define FROTHY_SPECIAL_NIL ((frothy_value_t)0x1u)
#define FROTHY_SPECIAL_FALSE ((frothy_value_t)0x5u)
#define FROTHY_SPECIAL_TRUE ((frothy_value_t)0x9u)

static const int32_t frothy_value_int_min = -(1 << 29);
static const int32_t frothy_value_int_max = (1 << 29) - 1;

bool frothy_value_is_object_ref(frothy_value_t value) {
  return (value & FROTHY_VALUE_TAG_MASK) == FROTHY_VALUE_TAG_OBJECT;
}

static frothy_value_t frothy_object_value(size_t object_id) {
  return (frothy_value_t)(((uint32_t)object_id << 2) | FROTHY_VALUE_TAG_OBJECT);
}

size_t frothy_value_object_index(frothy_value_t value) { return value >> 2; }

static void frothy_object_reset(frothy_object_t *object) {
  memset(object, 0, sizeof(*object));
  object->kind = FROTHY_OBJECT_FREE;
}

static frothy_value_t frothy_cellspace_value(const froth_cellspace_t *cellspace,
                                             froth_cell_t index) {
  return (frothy_value_t)(uint32_t)(int32_t)cellspace->data[index];
}

froth_cell_t frothy_value_to_cell(frothy_value_t value) {
  return (froth_cell_t)(int32_t)value;
}

frothy_value_t frothy_value_from_cell(froth_cell_t cell) {
  return (frothy_value_t)(uint32_t)(int32_t)cell;
}

static void frothy_cellspace_store_value(froth_cellspace_t *cellspace,
                                         froth_cell_t index,
                                         frothy_value_t value) {
  cellspace->data[index] = frothy_value_to_cell(value);
}

static froth_error_t frothy_strdup(const char *text, size_t length,
                                   char **out) {
  char *copy = (char *)malloc(length + 1);

  if (copy == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  memcpy(copy, text, length);
  copy[length] = '\0';
  *out = copy;
  return FROTH_OK;
}

static froth_error_t frothy_strdup_printf(char **out, const char *format, ...) {
  va_list args;
  va_list copy;
  int needed;
  char *buffer;

  va_start(args, format);
  va_copy(copy, args);
  needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return FROTH_ERROR_IO;
  }

  buffer = (char *)malloc((size_t)needed + 1);
  if (buffer == NULL) {
    va_end(args);
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  (void)vsnprintf(buffer, (size_t)needed + 1, format, args);
  va_end(args);
  *out = buffer;
  return FROTH_OK;
}

static froth_error_t frothy_quote_text(const char *text, size_t length,
                                       char **out) {
  size_t extra = 2;
  size_t i;
  char *buffer;
  size_t cursor = 0;

  for (i = 0; i < length; i++) {
    switch (text[i]) {
    case '\\':
    case '"':
    case '\n':
    case '\r':
    case '\t':
      extra += 2;
      break;
    default:
      extra += 1;
      break;
    }
  }

  buffer = (char *)malloc(extra + 1);
  if (buffer == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  buffer[cursor++] = '"';
  for (i = 0; i < length; i++) {
    switch (text[i]) {
    case '\\':
      buffer[cursor++] = '\\';
      buffer[cursor++] = '\\';
      break;
    case '"':
      buffer[cursor++] = '\\';
      buffer[cursor++] = '"';
      break;
    case '\n':
      buffer[cursor++] = '\\';
      buffer[cursor++] = 'n';
      break;
    case '\r':
      buffer[cursor++] = '\\';
      buffer[cursor++] = 'r';
      break;
    case '\t':
      buffer[cursor++] = '\\';
      buffer[cursor++] = 't';
      break;
    default:
      buffer[cursor++] = text[i];
      break;
    }
  }

  buffer[cursor++] = '"';
  buffer[cursor] = '\0';
  *out = buffer;
  return FROTH_OK;
}

static froth_error_t frothy_runtime_reserve_objects(frothy_runtime_t *runtime,
                                                    size_t needed) {
  if (needed <= runtime->object_capacity) {
    return FROTH_OK;
  }
  return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
}

static void frothy_runtime_add_free_span(frothy_runtime_t *runtime,
                                         froth_cell_t base,
                                         froth_cell_t length) {
  size_t insert;
  size_t i;

  if (length <= 0) {
    return;
  }

  insert = 0;
  while (insert < runtime->free_span_count &&
         runtime->free_spans[insert].base < base) {
    insert++;
  }

  memmove(runtime->free_spans + insert + 1, runtime->free_spans + insert,
          (runtime->free_span_count - insert) * sizeof(*runtime->free_spans));
  runtime->free_spans[insert].base = base;
  runtime->free_spans[insert].length = length;
  runtime->free_span_count++;

  for (i = 1; i < runtime->free_span_count; i++) {
    frothy_cells_span_t *prev = &runtime->free_spans[i - 1];
    frothy_cells_span_t *current = &runtime->free_spans[i];
    froth_cell_t prev_end = prev->base + prev->length;
    froth_cell_t current_end = current->base + current->length;

    if (prev_end < current->base) {
      continue;
    }

    if (current_end > prev_end) {
      prev->length = current_end - prev->base;
    }
    memmove(current, current + 1,
            (runtime->free_span_count - i - 1) * sizeof(*current));
    runtime->free_span_count--;
    i--;
  }

  while (runtime->free_span_count > 0 && runtime->cellspace != NULL) {
    frothy_cells_span_t *tail =
        &runtime->free_spans[runtime->free_span_count - 1];

    if (tail->base + tail->length != runtime->cellspace->used) {
      break;
    }

    runtime->cellspace->used = (froth_cell_u_t)tail->base;
    runtime->free_span_count--;
  }
}

static bool frothy_runtime_take_free_span(frothy_runtime_t *runtime,
                                          froth_cell_t length,
                                          froth_cell_t *base_out) {
  size_t i;

  for (i = 0; i < runtime->free_span_count; i++) {
    frothy_cells_span_t *span = &runtime->free_spans[i];

    if (span->length < length) {
      continue;
    }

    *base_out = span->base;
    span->base += length;
    span->length -= length;
    if (span->length == 0) {
      memmove(span, span + 1,
              (runtime->free_span_count - i - 1) * sizeof(*span));
      runtime->free_span_count--;
    }
    return true;
  }

  return false;
}

static void frothy_runtime_discard_object(frothy_runtime_t *runtime,
                                          size_t object_id) {
  frothy_object_t *object = &runtime->objects[object_id];

  if (!object->live) {
    return;
  }

  switch (object->kind) {
  case FROTHY_OBJECT_TEXT:
    free(object->as.text.bytes);
    break;
  case FROTHY_OBJECT_CELLS:
    break;
  case FROTHY_OBJECT_CODE:
    frothy_ir_program_free(&object->as.code.program);
    break;
  case FROTHY_OBJECT_NATIVE:
  case FROTHY_OBJECT_FREE:
    break;
  }

  frothy_object_reset(object);
  runtime->live_object_count--;
}

static froth_error_t frothy_runtime_clear_live_object(frothy_runtime_t *runtime,
                                                      size_t object_id);

static froth_error_t frothy_runtime_get_object(const frothy_runtime_t *runtime,
                                               frothy_value_t value,
                                               frothy_object_kind_t expected,
                                               const frothy_object_t **out) {
  size_t object_id;

  if (!frothy_value_is_object_ref(value)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  object_id = frothy_value_object_index(value);
  if (object_id >= runtime->object_count) {
    return FROTH_ERROR_BOUNDS;
  }
  if (!runtime->objects[object_id].live) {
    return FROTH_ERROR_BOUNDS;
  }
  if (runtime->objects[object_id].kind != expected) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  *out = &runtime->objects[object_id];
  return FROTH_OK;
}

static froth_error_t frothy_runtime_append_object(frothy_runtime_t *runtime,
                                                  const frothy_object_t *object,
                                                  frothy_value_t *out) {
  size_t object_id;
  size_t object_limit;

  if (runtime->test_fail_next_append) {
    runtime->test_fail_next_append = false;
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  for (object_id = 0; object_id < runtime->object_count; object_id++) {
    if (!runtime->objects[object_id].live) {
      runtime->objects[object_id] = *object;
      runtime->objects[object_id].refcount = 1;
      runtime->objects[object_id].live = true;
      runtime->live_object_count++;
      *out = frothy_object_value(object_id);
      return FROTH_OK;
    }
  }

  object_limit = runtime->test_object_limit;
  if (object_limit > runtime->object_capacity) {
    object_limit = runtime->object_capacity;
  }
  if (runtime->object_count >= object_limit) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }
  FROTH_TRY(frothy_runtime_reserve_objects(runtime, runtime->object_count + 1));
  object_id = runtime->object_count++;
  if (runtime->object_count > runtime->object_high_water) {
    runtime->object_high_water = runtime->object_count;
  }
  runtime->objects[object_id] = *object;
  runtime->objects[object_id].refcount = 1;
  runtime->objects[object_id].live = true;
  runtime->live_object_count++;
  *out = frothy_object_value(object_id);
  return FROTH_OK;
}

static froth_error_t frothy_runtime_clear_live_object(frothy_runtime_t *runtime,
                                                      size_t object_id) {
  frothy_object_t *object = &runtime->objects[object_id];
  froth_cell_t base;
  froth_cell_t length;
  froth_cell_t i;

  if (!object->live) {
    return FROTH_OK;
  }

  switch (object->kind) {
  case FROTHY_OBJECT_TEXT:
    free(object->as.text.bytes);
    break;
  case FROTHY_OBJECT_CELLS:
    base = object->as.cells.span.base;
    length = object->as.cells.span.length;
    for (i = 0; i < length; i++) {
      frothy_value_t value = frothy_cellspace_value(runtime->cellspace, base + i);

      FROTH_TRY(frothy_value_release(runtime, value));
      runtime->cellspace->data[base + i] = 0;
    }
    frothy_runtime_add_free_span(runtime, base, length);
    break;
  case FROTHY_OBJECT_CODE:
    frothy_ir_program_free(&object->as.code.program);
    break;
  case FROTHY_OBJECT_NATIVE:
  case FROTHY_OBJECT_FREE:
    break;
  }

  frothy_object_reset(object);
  runtime->live_object_count--;
  return FROTH_OK;
}

void frothy_runtime_init(frothy_runtime_t *runtime, froth_cellspace_t *cellspace) {
  memset(runtime, 0, sizeof(*runtime));
  runtime->objects = runtime->object_storage;
  runtime->object_capacity = FROTHY_OBJECT_CAPACITY;
  runtime->free_spans = runtime->free_span_storage;
  runtime->free_span_capacity = FROTHY_OBJECT_CAPACITY;
  runtime->eval_values = runtime->eval_value_storage;
  runtime->eval_value_capacity = FROTHY_EVAL_VALUE_CAPACITY;
  runtime->eval_value_limit = FROTHY_EVAL_VALUE_CAPACITY;
  runtime->cellspace = cellspace;
  runtime->test_object_limit = FROTHY_OBJECT_CAPACITY;
}

void frothy_runtime_free(frothy_runtime_t *runtime) {
  size_t i;

  if (runtime == NULL) {
    return;
  }

  for (i = 0; i < runtime->object_count; i++) {
    frothy_runtime_discard_object(runtime, i);
  }

  if (runtime->cellspace != NULL) {
    froth_cellspace_reset_to_base(runtime->cellspace);
  }

  memset(runtime, 0, sizeof(*runtime));
}

froth_error_t frothy_runtime_reset_overlay(frothy_runtime_t *runtime) {
  FROTH_TRY(frothy_runtime_clear_overlay_state(runtime));
  FROTH_TRY(froth_slot_reset_overlay());
  return FROTH_OK;
}

froth_error_t frothy_runtime_clear_overlay_state(frothy_runtime_t *runtime) {
  froth_cell_u_t slot_count;
  froth_cell_u_t slot_index;

  slot_count = froth_slot_count();
  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    froth_cell_t impl;

    if (!froth_slot_is_overlay(slot_index)) {
      continue;
    }
    if (froth_slot_get_impl(slot_index, &impl) != FROTH_OK) {
      continue;
    }
    FROTH_TRY(frothy_value_release(runtime, frothy_value_from_cell(impl)));
  }

  if (runtime->cellspace != NULL) {
    froth_cellspace_reset_to_base(runtime->cellspace);
  }

  for (slot_index = 0; slot_index < runtime->object_count; slot_index++) {
    frothy_runtime_discard_object(runtime, slot_index);
  }

  runtime->object_count = 0;
  runtime->free_span_count = 0;
  runtime->live_object_count = 0;
  runtime->object_high_water = 0;
  runtime->test_fail_next_append = false;
  runtime->eval_value_used = 0;
  runtime->eval_value_high_water = 0;
  runtime->reset_epoch++;
  return FROTH_OK;
}

size_t frothy_runtime_live_object_count(const frothy_runtime_t *runtime) {
  return runtime->live_object_count;
}

size_t frothy_runtime_object_high_water(const frothy_runtime_t *runtime) {
  return runtime->object_high_water;
}

size_t frothy_runtime_eval_value_high_water(const frothy_runtime_t *runtime) {
  return runtime->eval_value_high_water;
}

void frothy_runtime_debug_reset_high_water(frothy_runtime_t *runtime) {
  runtime->object_high_water = 0;
  runtime->eval_value_high_water = 0;
}

void frothy_runtime_test_set_object_limit(frothy_runtime_t *runtime,
                                          size_t limit) {
  runtime->test_object_limit =
      limit > runtime->object_capacity ? runtime->object_capacity : limit;
}

void frothy_runtime_test_set_eval_value_limit(frothy_runtime_t *runtime,
                                              size_t limit) {
  runtime->eval_value_limit =
      limit > runtime->eval_value_capacity ? runtime->eval_value_capacity : limit;
}

void frothy_runtime_test_fail_next_append(frothy_runtime_t *runtime) {
  runtime->test_fail_next_append = true;
}

froth_error_t frothy_value_make_int(int32_t value, frothy_value_t *out) {
  if (value < frothy_value_int_min || value > frothy_value_int_max) {
    return FROTH_ERROR_VALUE_OVERFLOW;
  }

  *out = (frothy_value_t)((uint32_t)value << 2);
  return FROTH_OK;
}

frothy_value_t frothy_value_make_bool(bool value) {
  return value ? FROTHY_SPECIAL_TRUE : FROTHY_SPECIAL_FALSE;
}

frothy_value_t frothy_value_make_nil(void) { return FROTHY_SPECIAL_NIL; }

bool frothy_value_is_int(frothy_value_t value) {
  return (value & FROTHY_VALUE_TAG_MASK) == FROTHY_VALUE_TAG_INT;
}

bool frothy_value_is_bool(frothy_value_t value) {
  return value == FROTHY_SPECIAL_TRUE || value == FROTHY_SPECIAL_FALSE;
}

bool frothy_value_is_nil(frothy_value_t value) { return value == FROTHY_SPECIAL_NIL; }

int32_t frothy_value_as_int(frothy_value_t value) {
  return ((int32_t)value) >> 2;
}

bool frothy_value_as_bool(frothy_value_t value) { return value == FROTHY_SPECIAL_TRUE; }

froth_error_t frothy_value_class(const frothy_runtime_t *runtime,
                                 frothy_value_t value,
                                 frothy_value_class_t *out) {
  if (frothy_value_is_int(value)) {
    *out = FROTHY_VALUE_CLASS_INT;
    return FROTH_OK;
  }
  if (frothy_value_is_bool(value)) {
    *out = FROTHY_VALUE_CLASS_BOOL;
    return FROTH_OK;
  }
  if (frothy_value_is_nil(value)) {
    *out = FROTHY_VALUE_CLASS_NIL;
    return FROTH_OK;
  }
  if (frothy_value_is_object_ref(value)) {
    const frothy_object_t *object;
    size_t object_id = frothy_value_object_index(value);

    if (object_id >= runtime->object_count || !runtime->objects[object_id].live) {
      return FROTH_ERROR_BOUNDS;
    }

    object = &runtime->objects[object_id];
    switch (object->kind) {
    case FROTHY_OBJECT_TEXT:
      *out = FROTHY_VALUE_CLASS_TEXT;
      return FROTH_OK;
    case FROTHY_OBJECT_CELLS:
      *out = FROTHY_VALUE_CLASS_CELLS;
      return FROTH_OK;
    case FROTHY_OBJECT_CODE:
      *out = FROTHY_VALUE_CLASS_CODE;
      return FROTH_OK;
    case FROTHY_OBJECT_NATIVE:
      *out = FROTHY_VALUE_CLASS_NATIVE;
      return FROTH_OK;
    case FROTHY_OBJECT_FREE:
      return FROTH_ERROR_BOUNDS;
    }
  }

  return FROTH_ERROR_TYPE_MISMATCH;
}

froth_error_t frothy_value_from_literal(frothy_runtime_t *runtime,
                                        const frothy_ir_literal_t *literal,
                                        frothy_value_t *out) {
  switch (literal->kind) {
  case FROTHY_IR_LITERAL_INT:
    return frothy_value_make_int((int32_t)literal->as.int_value, out);
  case FROTHY_IR_LITERAL_BOOL:
    *out = frothy_value_make_bool(literal->as.bool_value);
    return FROTH_OK;
  case FROTHY_IR_LITERAL_NIL:
    *out = frothy_value_make_nil();
    return FROTH_OK;
  case FROTHY_IR_LITERAL_TEXT:
    return frothy_runtime_alloc_text(runtime, literal->as.text_value,
                                     strlen(literal->as.text_value), out);
  }

  return FROTH_ERROR_SIGNATURE;
}

froth_error_t frothy_value_render(const frothy_runtime_t *runtime,
                                  frothy_value_t value, char **out_text) {
  *out_text = NULL;

  if (frothy_value_is_int(value)) {
    return frothy_strdup_printf(out_text, "%d", frothy_value_as_int(value));
  }
  if (frothy_value_is_bool(value)) {
    if (frothy_value_as_bool(value)) {
      return frothy_strdup("true", 4, out_text);
    }
    return frothy_strdup("false", 5, out_text);
  }
  if (frothy_value_is_nil(value)) {
    return frothy_strdup("nil", 3, out_text);
  }
  if (frothy_value_is_object_ref(value)) {
    const frothy_object_t *object;
    size_t object_id = frothy_value_object_index(value);

    if (object_id >= runtime->object_count || !runtime->objects[object_id].live) {
      return FROTH_ERROR_BOUNDS;
    }

    object = &runtime->objects[object_id];
    switch (object->kind) {
    case FROTHY_OBJECT_TEXT:
      return frothy_quote_text(object->as.text.bytes, object->as.text.length,
                               out_text);
    case FROTHY_OBJECT_CELLS:
      return frothy_strdup_printf(out_text, "<cells %d>",
                                  object->as.cells.span.length);
    case FROTHY_OBJECT_CODE:
      return frothy_strdup_printf(out_text, "<fn/%zu>",
                                  object->as.code.arity);
    case FROTHY_OBJECT_NATIVE:
      return frothy_strdup_printf(out_text, "<native %s/%zu>",
                                  object->as.native.name,
                                  object->as.native.arity);
    case FROTHY_OBJECT_FREE:
      return FROTH_ERROR_BOUNDS;
    }
  }

  return FROTH_ERROR_TYPE_MISMATCH;
}

froth_error_t frothy_value_equals(const frothy_runtime_t *runtime,
                                  frothy_value_t lhs, frothy_value_t rhs,
                                  bool *equal_out) {
  frothy_value_class_t lhs_class;
  frothy_value_class_t rhs_class;

  if (lhs == rhs) {
    *equal_out = true;
    return FROTH_OK;
  }

  FROTH_TRY(frothy_value_class(runtime, lhs, &lhs_class));
  FROTH_TRY(frothy_value_class(runtime, rhs, &rhs_class));
  if (lhs_class != rhs_class) {
    *equal_out = false;
    return FROTH_OK;
  }

  if (lhs_class == FROTHY_VALUE_CLASS_TEXT) {
    const char *lhs_text;
    const char *rhs_text;
    size_t lhs_length;
    size_t rhs_length;

    FROTH_TRY(frothy_runtime_get_text(runtime, lhs, &lhs_text, &lhs_length));
    FROTH_TRY(frothy_runtime_get_text(runtime, rhs, &rhs_text, &rhs_length));
    *equal_out = lhs_length == rhs_length &&
                 memcmp(lhs_text, rhs_text, lhs_length) == 0;
    return FROTH_OK;
  }

  *equal_out = false;
  return FROTH_OK;
}

froth_error_t frothy_value_retain(frothy_runtime_t *runtime, frothy_value_t value) {
  size_t object_id;

  if (!frothy_value_is_object_ref(value)) {
    return FROTH_OK;
  }

  object_id = frothy_value_object_index(value);
  if (object_id >= runtime->object_count || !runtime->objects[object_id].live) {
    return FROTH_ERROR_BOUNDS;
  }

  runtime->objects[object_id].refcount++;
  return FROTH_OK;
}

froth_error_t frothy_value_release(frothy_runtime_t *runtime,
                                   frothy_value_t value) {
  size_t object_id;
  frothy_object_t *object;

  if (!frothy_value_is_object_ref(value)) {
    return FROTH_OK;
  }

  object_id = frothy_value_object_index(value);
  if (object_id >= runtime->object_count) {
    return FROTH_ERROR_BOUNDS;
  }

  object = &runtime->objects[object_id];
  if (!object->live || object->refcount == 0) {
    return FROTH_ERROR_BOUNDS;
  }

  object->refcount--;
  if (object->refcount > 0) {
    return FROTH_OK;
  }

  return frothy_runtime_clear_live_object(runtime, object_id);
}

froth_error_t frothy_runtime_alloc_text(frothy_runtime_t *runtime,
                                        const char *text, size_t length,
                                        frothy_value_t *out) {
  frothy_object_t object;
  froth_error_t err;

  memset(&object, 0, sizeof(object));
  object.kind = FROTHY_OBJECT_TEXT;
  err = frothy_strdup(text, length, &object.as.text.bytes);
  if (err != FROTH_OK) {
    return err;
  }
  object.as.text.length = length;

  err = frothy_runtime_append_object(runtime, &object, out);
  if (err != FROTH_OK) {
    free(object.as.text.bytes);
  }
  return err;
}

froth_error_t frothy_runtime_get_text(const frothy_runtime_t *runtime,
                                      frothy_value_t value, const char **text,
                                      size_t *length_out) {
  const frothy_object_t *object;

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_TEXT,
                                      &object));
  *text = object->as.text.bytes;
  if (length_out != NULL) {
    *length_out = object->as.text.length;
  }
  return FROTH_OK;
}

froth_error_t frothy_runtime_alloc_cells(frothy_runtime_t *runtime,
                                         size_t length, frothy_value_t *out) {
  frothy_object_t object;
  froth_cell_t base = 0;
  froth_cell_t i;
  froth_error_t err;

  if (length == 0 || length > (size_t)INT32_MAX || runtime->cellspace == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  memset(&object, 0, sizeof(object));
  object.kind = FROTHY_OBJECT_CELLS;

  if (!frothy_runtime_take_free_span(runtime, (froth_cell_t)length, &base)) {
    FROTH_TRY(froth_cellspace_allot(runtime->cellspace, (froth_cell_t)length, &base));
  }

  object.as.cells.span.base = base;
  object.as.cells.span.length = (froth_cell_t)length;
  for (i = 0; i < (froth_cell_t)length; i++) {
    frothy_cellspace_store_value(runtime->cellspace, base + i,
                                 frothy_value_make_nil());
  }

  err = frothy_runtime_append_object(runtime, &object, out);
  if (err != FROTH_OK) {
    frothy_runtime_add_free_span(runtime, base, (froth_cell_t)length);
  }
  return err;
}

froth_error_t frothy_runtime_get_cells(const frothy_runtime_t *runtime,
                                       frothy_value_t value, size_t *length_out,
                                       froth_cell_t *base_out) {
  const frothy_object_t *object;

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_CELLS,
                                      &object));
  if (length_out != NULL) {
    *length_out = (size_t)object->as.cells.span.length;
  }
  if (base_out != NULL) {
    *base_out = object->as.cells.span.base;
  }
  return FROTH_OK;
}

froth_error_t frothy_runtime_alloc_code(frothy_runtime_t *runtime,
                                        const frothy_ir_program_t *program,
                                        frothy_ir_node_id_t body, size_t arity,
                                        size_t local_count,
                                        frothy_value_t *out) {
  frothy_object_t object;
  froth_error_t err;

  memset(&object, 0, sizeof(object));
  object.kind = FROTHY_OBJECT_CODE;
  object.as.code.arity = arity;
  object.as.code.local_count = local_count;
  object.as.code.body = body;

  err = frothy_ir_program_clone(program, &object.as.code.program);
  if (err != FROTH_OK) {
    return err;
  }

  err = frothy_runtime_append_object(runtime, &object, out);
  if (err != FROTH_OK) {
    frothy_ir_program_free(&object.as.code.program);
  }
  return err;
}

froth_error_t frothy_runtime_get_code(const frothy_runtime_t *runtime,
                                      frothy_value_t value,
                                      const frothy_ir_program_t **program_out,
                                      frothy_ir_node_id_t *body_out,
                                      size_t *arity_out,
                                      size_t *local_count_out) {
  const frothy_object_t *object;

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_CODE,
                                      &object));
  if (program_out != NULL) {
    *program_out = &object->as.code.program;
  }
  if (body_out != NULL) {
    *body_out = object->as.code.body;
  }
  if (arity_out != NULL) {
    *arity_out = object->as.code.arity;
  }
  if (local_count_out != NULL) {
    *local_count_out = object->as.code.local_count;
  }
  return FROTH_OK;
}

froth_error_t frothy_runtime_alloc_native(frothy_runtime_t *runtime,
                                          frothy_native_fn_t fn,
                                          const char *name, size_t arity,
                                          const void *context,
                                          frothy_value_t *out) {
  frothy_object_t object;

  if (fn == NULL || name == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  memset(&object, 0, sizeof(object));
  object.kind = FROTHY_OBJECT_NATIVE;
  object.as.native.fn = fn;
  object.as.native.context = context;
  object.as.native.name = name;
  object.as.native.arity = arity;
  return frothy_runtime_append_object(runtime, &object, out);
}

froth_error_t frothy_runtime_get_native(const frothy_runtime_t *runtime,
                                        frothy_value_t value,
                                        frothy_native_fn_t *fn_out,
                                        const void **context_out,
                                        const char **name_out,
                                        size_t *arity_out) {
  const frothy_object_t *object;

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_NATIVE,
                                      &object));
  if (fn_out != NULL) {
    *fn_out = object->as.native.fn;
  }
  if (context_out != NULL) {
    *context_out = object->as.native.context;
  }
  if (name_out != NULL) {
    *name_out = object->as.native.name;
  }
  if (arity_out != NULL) {
    *arity_out = object->as.native.arity;
  }
  return FROTH_OK;
}
