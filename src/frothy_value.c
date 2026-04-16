#include "frothy_value.h"

#include "frothy_ir_internal.h"
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

bool frothy_value_is_slot_designator(frothy_value_t value) {
  return (value & FROTHY_VALUE_TAG_MASK) == FROTHY_VALUE_TAG_SLOT;
}

static frothy_value_t frothy_object_value(size_t object_id) {
  return (frothy_value_t)(((uint32_t)object_id << 2) | FROTHY_VALUE_TAG_OBJECT);
}

size_t frothy_value_object_index(frothy_value_t value) { return value >> 2; }

static frothy_value_t frothy_slot_value(froth_cell_u_t slot_index) {
  return (frothy_value_t)(((uint32_t)slot_index << 2) | FROTHY_VALUE_TAG_SLOT);
}

static froth_cell_u_t frothy_value_slot_index(frothy_value_t value) {
  return (froth_cell_u_t)(value >> 2);
}

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

froth_error_t frothy_value_make_slot_designator(const char *slot_name,
                                                frothy_value_t *out) {
  froth_cell_u_t slot_index;

  if (slot_name == NULL || out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(froth_slot_find_name(slot_name, &slot_index));
  *out = frothy_slot_value(slot_index);
  return FROTH_OK;
}

froth_error_t frothy_value_get_slot_designator_name(frothy_value_t value,
                                                    const char **name_out) {
  if (!frothy_value_is_slot_designator(value) || name_out == NULL) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  return froth_slot_get_name(frothy_value_slot_index(value), name_out);
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

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} frothy_string_builder_t;

static void frothy_sb_init(frothy_string_builder_t *builder) {
  builder->data = NULL;
  builder->length = 0;
  builder->capacity = 0;
}

static froth_error_t frothy_sb_reserve(frothy_string_builder_t *builder,
                                       size_t extra) {
  size_t needed = builder->length + extra + 1;
  size_t capacity = builder->capacity == 0 ? 128 : builder->capacity;
  char *resized;

  while (capacity < needed) {
    capacity *= 2;
  }

  if (capacity == builder->capacity) {
    return FROTH_OK;
  }

  resized = (char *)realloc(builder->data, capacity);
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  builder->data = resized;
  builder->capacity = capacity;
  return FROTH_OK;
}

static froth_error_t frothy_sb_append_text(frothy_string_builder_t *builder,
                                           const char *text) {
  size_t length = strlen(text);

  FROTH_TRY(frothy_sb_reserve(builder, length));
  memcpy(builder->data + builder->length, text, length);
  builder->length += length;
  builder->data[builder->length] = '\0';
  return FROTH_OK;
}

static froth_error_t frothy_sb_append_char(frothy_string_builder_t *builder,
                                           char ch) {
  FROTH_TRY(frothy_sb_reserve(builder, 1));
  builder->data[builder->length++] = ch;
  builder->data[builder->length] = '\0';
  return FROTH_OK;
}

static froth_error_t frothy_sb_appendf(frothy_string_builder_t *builder,
                                       const char *format, ...) {
  va_list args;
  va_list copy;
  int needed;

  va_start(args, format);
  va_copy(copy, args);
  needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);

  if (needed < 0) {
    va_end(args);
    return FROTH_ERROR_IO;
  }

  FROTH_TRY(frothy_sb_reserve(builder, (size_t)needed));
  (void)vsnprintf(builder->data + builder->length,
                  builder->capacity - builder->length, format, args);
  va_end(args);
  builder->length += (size_t)needed;
  return FROTH_OK;
}

static froth_error_t frothy_sb_append_quoted(frothy_string_builder_t *builder,
                                             const char *text, size_t length) {
  char *quoted = NULL;
  froth_error_t err;

  err = frothy_quote_text(text, length, &quoted);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_sb_append_text(builder, quoted);
  free(quoted);
  return err;
}

static froth_error_t frothy_add_size(size_t lhs, size_t rhs, size_t *out) {
  if (lhs > SIZE_MAX - rhs) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  *out = lhs + rhs;
  return FROTH_OK;
}

static froth_error_t frothy_payload_align_up(size_t value, size_t *out) {
  const size_t alignment = _Alignof(max_align_t);
  size_t remainder;
  size_t padding;

  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  remainder = value % alignment;
  if (remainder == 0) {
    *out = value;
    return FROTH_OK;
  }

  padding = alignment - remainder;
  if (value > SIZE_MAX - padding) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  *out = value + padding;
  return FROTH_OK;
}

static void *frothy_runtime_payload_ptr(frothy_runtime_t *runtime,
                                        size_t offset) {
  return runtime->payload_storage.bytes + offset;
}

static const void *frothy_runtime_payload_const_ptr(
    const frothy_runtime_t *runtime, size_t offset) {
  return runtime->payload_storage.bytes + offset;
}

static froth_error_t frothy_record_def_next_string(const char **cursor_io,
                                                   const char *end,
                                                   const char **text_out) {
  const char *cursor = *cursor_io;
  const char *nul;

  if (cursor > end) {
    return FROTH_ERROR_BOUNDS;
  }

  nul = (const char *)memchr(cursor, '\0', (size_t)(end - cursor));
  if (nul == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  *text_out = cursor;
  *cursor_io = nul + 1;
  return FROTH_OK;
}

static froth_error_t frothy_record_def_field_name_from_ir(
    const frothy_ir_program_t *program, size_t first_field, size_t field_index,
    const char **field_name_out) {
  frothy_ir_literal_id_t literal_id;

  literal_id = (frothy_ir_literal_id_t)program->links[first_field + field_index];
  if (literal_id >= program->literal_count ||
      program->literals[literal_id].kind != FROTHY_IR_LITERAL_TEXT) {
    return FROTH_ERROR_SIGNATURE;
  }

  *field_name_out = program->literals[literal_id].as.text_value;
  return FROTH_OK;
}

static froth_error_t frothy_runtime_record_def_object_view(
    const frothy_runtime_t *runtime, const frothy_object_t *object,
    const char **name_out, size_t *field_count_out, const char **fields_out) {
  const char *cursor;
  const char *end;
  uint32_t stored_field_count = 0;

  if (object->kind != FROTHY_OBJECT_RECORD_DEF ||
      object->as.record_def.payload.length < sizeof(stored_field_count)) {
    return FROTH_ERROR_BOUNDS;
  }

  cursor = (const char *)frothy_runtime_payload_const_ptr(
      runtime, object->as.record_def.payload.offset);
  end = cursor + object->as.record_def.payload.length;
  memcpy(&stored_field_count, cursor, sizeof(stored_field_count));
  cursor += sizeof(stored_field_count);

  FROTH_TRY(frothy_record_def_next_string(&cursor, end, name_out));
  if (field_count_out != NULL) {
    *field_count_out = (size_t)stored_field_count;
  }
  if (fields_out != NULL) {
    *fields_out = cursor;
  }
  return FROTH_OK;
}

static froth_error_t frothy_runtime_record_field_values(
    frothy_runtime_t *runtime, frothy_object_t *object, frothy_value_t **fields_out) {
  if (object->kind != FROTHY_OBJECT_RECORD) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  *fields_out = (frothy_value_t *)frothy_runtime_payload_ptr(
      runtime, object->as.record.payload.offset);
  return FROTH_OK;
}

static froth_error_t frothy_runtime_record_field_values_const(
    const frothy_runtime_t *runtime, const frothy_object_t *object,
    const frothy_value_t **fields_out) {
  if (object->kind != FROTHY_OBJECT_RECORD) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  *fields_out = (const frothy_value_t *)frothy_runtime_payload_const_ptr(
      runtime, object->as.record.payload.offset);
  return FROTH_OK;
}

static void frothy_runtime_add_payload_free_span(frothy_runtime_t *runtime,
                                                 size_t offset,
                                                 size_t length) {
  size_t insert;
  size_t i;

  if (length == 0) {
    return;
  }

  insert = 0;
  while (insert < runtime->payload_free_span_count &&
         runtime->payload_free_spans[insert].offset < offset) {
    insert++;
  }

  memmove(runtime->payload_free_spans + insert + 1,
          runtime->payload_free_spans + insert,
          (runtime->payload_free_span_count - insert) *
              sizeof(*runtime->payload_free_spans));
  runtime->payload_free_spans[insert].offset = offset;
  runtime->payload_free_spans[insert].length = length;
  runtime->payload_free_span_count++;

  for (i = 1; i < runtime->payload_free_span_count; i++) {
    frothy_payload_span_t *prev = &runtime->payload_free_spans[i - 1];
    frothy_payload_span_t *current = &runtime->payload_free_spans[i];
    size_t prev_end = prev->offset + prev->length;
    size_t current_end = current->offset + current->length;

    if (prev_end < current->offset) {
      continue;
    }

    if (current_end > prev_end) {
      prev->length = current_end - prev->offset;
    }
    memmove(current, current + 1,
            (runtime->payload_free_span_count - i - 1) * sizeof(*current));
    runtime->payload_free_span_count--;
    i--;
  }

  while (runtime->payload_free_span_count > 0) {
    frothy_payload_span_t *tail =
        &runtime->payload_free_spans[runtime->payload_free_span_count - 1];

    if (tail->offset + tail->length != runtime->payload_extent) {
      break;
    }

    runtime->payload_extent = tail->offset;
    runtime->payload_free_span_count--;
  }
}

static bool frothy_runtime_take_payload_free_span(frothy_runtime_t *runtime,
                                                  size_t length,
                                                  size_t *offset_out) {
  size_t i;

  for (i = 0; i < runtime->payload_free_span_count; i++) {
    frothy_payload_span_t *span = &runtime->payload_free_spans[i];

    if (span->length < length) {
      continue;
    }

    *offset_out = span->offset;
    span->offset += length;
    span->length -= length;
    if (span->length == 0) {
      memmove(span, span + 1,
              (runtime->payload_free_span_count - i - 1) * sizeof(*span));
      runtime->payload_free_span_count--;
    }
    return true;
  }

  return false;
}

froth_error_t frothy_runtime_alloc_payload(frothy_runtime_t *runtime,
                                           size_t length,
                                           frothy_payload_span_t *span_out,
                                           void **data_out) {
  size_t reserved = 0;
  size_t offset = 0;

  if (runtime == NULL || span_out == NULL || data_out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(frothy_payload_align_up(length, &reserved));
  span_out->offset = 0;
  span_out->length = 0;
  *data_out = NULL;
  if (reserved == 0) {
    return FROTH_OK;
  }

  if (!frothy_runtime_take_payload_free_span(runtime, reserved, &offset)) {
    if (reserved > runtime->payload_capacity ||
        runtime->payload_extent > runtime->payload_capacity - reserved) {
      return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
    }
    offset = runtime->payload_extent;
    runtime->payload_extent += reserved;
  }

  runtime->payload_bytes_used += reserved;
  if (runtime->payload_bytes_used > runtime->payload_bytes_high_water) {
    runtime->payload_bytes_high_water = runtime->payload_bytes_used;
  }

  span_out->offset = offset;
  span_out->length = reserved;
  *data_out = frothy_runtime_payload_ptr(runtime, offset);
  return FROTH_OK;
}

void frothy_runtime_release_payload(frothy_runtime_t *runtime,
                                    frothy_payload_span_t span) {
  if (runtime == NULL) {
    return;
  }
  if (span.length == 0) {
    return;
  }

  runtime->payload_bytes_used -= span.length;
  frothy_runtime_add_payload_free_span(runtime, span.offset, span.length);
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
    frothy_runtime_release_payload(runtime, object->as.text.payload);
    break;
  case FROTHY_OBJECT_CELLS:
    break;
  case FROTHY_OBJECT_CODE:
    if (object->as.code.payload.length > 0) {
      frothy_runtime_release_payload(runtime, object->as.code.payload);
    } else {
      frothy_ir_program_free(&object->as.code.program);
    }
    break;
  case FROTHY_OBJECT_RECORD_DEF:
    frothy_runtime_release_payload(runtime, object->as.record_def.payload);
    break;
  case FROTHY_OBJECT_RECORD:
    frothy_runtime_release_payload(runtime, object->as.record.payload);
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
    frothy_runtime_release_payload(runtime, object->as.text.payload);
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
    if (object->as.code.payload.length > 0) {
      frothy_runtime_release_payload(runtime, object->as.code.payload);
    } else {
      frothy_ir_program_free(&object->as.code.program);
    }
    break;
  case FROTHY_OBJECT_RECORD_DEF:
    frothy_runtime_release_payload(runtime, object->as.record_def.payload);
    break;
  case FROTHY_OBJECT_RECORD: {
    frothy_value_t *fields = NULL;
    size_t i;

    FROTH_TRY(frothy_runtime_record_field_values(runtime, object, &fields));
    for (i = 0; i < object->as.record.field_count; i++) {
      FROTH_TRY(frothy_value_release(runtime, fields[i]));
    }
    FROTH_TRY(
        frothy_value_release(runtime, object->as.record.definition));
    frothy_runtime_release_payload(runtime, object->as.record.payload);
    break;
  }
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
  runtime->payload_free_spans = runtime->payload_free_span_storage;
  runtime->payload_free_span_capacity = FROTHY_OBJECT_CAPACITY;
  runtime->payload_capacity = FROTHY_PAYLOAD_CAPACITY;
  runtime->eval_values = runtime->eval_value_storage;
  runtime->eval_value_capacity = FROTHY_EVAL_VALUE_CAPACITY;
  runtime->eval_value_limit = FROTHY_EVAL_VALUE_CAPACITY;
  runtime->cellspace = cellspace;
  runtime->last_ffi_error_code = FROTH_OK;
  runtime->last_ffi_error_kind = NULL;
  runtime->last_ffi_error_origin = NULL;
  runtime->last_ffi_error_detail = NULL;
  runtime->last_ffi_error_kind_storage[0] = '\0';
  runtime->last_ffi_error_origin_storage[0] = '\0';
  runtime->last_ffi_error_detail_storage[0] = '\0';
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
  runtime->payload_free_span_count = 0;
  runtime->payload_extent = 0;
  runtime->payload_bytes_used = 0;
  runtime->payload_bytes_high_water = 0;
  runtime->live_object_count = 0;
  runtime->object_high_water = 0;
  runtime->test_fail_next_append = false;
  runtime->eval_value_used = 0;
  runtime->eval_value_high_water = 0;
  runtime->last_ffi_error_code = FROTH_OK;
  runtime->last_ffi_error_kind = NULL;
  runtime->last_ffi_error_origin = NULL;
  runtime->last_ffi_error_detail = NULL;
  runtime->last_ffi_error_kind_storage[0] = '\0';
  runtime->last_ffi_error_origin_storage[0] = '\0';
  runtime->last_ffi_error_detail_storage[0] = '\0';
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

size_t frothy_runtime_payload_used(const frothy_runtime_t *runtime) {
  return runtime->payload_bytes_used;
}

size_t frothy_runtime_payload_high_water(const frothy_runtime_t *runtime) {
  return runtime->payload_bytes_high_water;
}

void frothy_runtime_debug_reset_high_water(frothy_runtime_t *runtime) {
  runtime->object_high_water = runtime->live_object_count;
  runtime->eval_value_high_water = runtime->eval_value_used;
  runtime->payload_bytes_high_water = runtime->payload_bytes_used;
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
  if (out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
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
  if (frothy_value_is_slot_designator(value)) {
    return FROTH_ERROR_TYPE_MISMATCH;
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
    case FROTHY_OBJECT_RECORD_DEF:
      *out = FROTHY_VALUE_CLASS_RECORD_DEF;
      return FROTH_OK;
    case FROTHY_OBJECT_RECORD:
      *out = FROTHY_VALUE_CLASS_RECORD;
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

static froth_error_t frothy_record_value_allowed(const frothy_runtime_t *runtime,
                                                 frothy_value_t value) {
  frothy_value_class_t value_class;

  FROTH_TRY(frothy_value_class(runtime, value, &value_class));
  switch (value_class) {
  case FROTHY_VALUE_CLASS_INT:
  case FROTHY_VALUE_CLASS_BOOL:
  case FROTHY_VALUE_CLASS_NIL:
  case FROTHY_VALUE_CLASS_TEXT:
  case FROTHY_VALUE_CLASS_RECORD:
    return FROTH_OK;
  case FROTHY_VALUE_CLASS_CELLS:
  case FROTHY_VALUE_CLASS_CODE:
  case FROTHY_VALUE_CLASS_NATIVE:
  case FROTHY_VALUE_CLASS_RECORD_DEF:
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  return FROTH_ERROR_TYPE_MISMATCH;
}

static bool frothy_record_stack_contains(const size_t *stack, size_t depth,
                                         size_t object_id) {
  size_t i;

  for (i = 0; i < depth; i++) {
    if (stack[i] == object_id) {
      return true;
    }
  }
  return false;
}

static froth_error_t frothy_value_render_append(
    const frothy_runtime_t *runtime, frothy_value_t value,
    frothy_string_builder_t *builder, size_t *record_stack, size_t depth);

static froth_error_t frothy_record_def_render_append(
    const frothy_runtime_t *runtime, const frothy_object_t *object,
    frothy_string_builder_t *builder) {
  const char *name = NULL;
  const char *cursor = NULL;
  const char *end;
  size_t field_count = 0;
  size_t i;

  FROTH_TRY(frothy_runtime_record_def_object_view(runtime, object, &name,
                                                  &field_count, &cursor));
  end = (const char *)frothy_runtime_payload_const_ptr(
            runtime, object->as.record_def.payload.offset) +
        object->as.record_def.payload.length;
  FROTH_TRY(frothy_sb_append_text(builder, "record "));
  FROTH_TRY(frothy_sb_append_text(builder, name));
  FROTH_TRY(frothy_sb_append_text(builder, " [ "));
  for (i = 0; i < field_count; i++) {
    const char *field_name = NULL;

    FROTH_TRY(frothy_record_def_next_string(&cursor, end, &field_name));
    if (i != 0) {
      FROTH_TRY(frothy_sb_append_text(builder, ", "));
    }
    FROTH_TRY(frothy_sb_append_text(builder, field_name));
  }
  return frothy_sb_append_text(builder, " ]");
}

static froth_error_t frothy_record_render_append(
    const frothy_runtime_t *runtime, size_t object_id,
    const frothy_object_t *object, frothy_string_builder_t *builder,
    size_t *record_stack, size_t depth) {
  const frothy_value_t *fields = NULL;
  const char *name = NULL;
  size_t field_count = 0;
  size_t i;

  if (frothy_record_stack_contains(record_stack, depth, object_id)) {
    return frothy_sb_append_text(builder, "<cycle>");
  }

  FROTH_TRY(frothy_runtime_get_record_def(runtime, object->as.record.definition,
                                          &name, &field_count));
  FROTH_TRY(
      frothy_runtime_record_field_values_const(runtime, object, &fields));
  record_stack[depth] = object_id;

  FROTH_TRY(frothy_sb_append_text(builder, name));
  FROTH_TRY(frothy_sb_append_char(builder, ':'));
  if (field_count == 0) {
    return FROTH_OK;
  }
  FROTH_TRY(frothy_sb_append_char(builder, ' '));
  for (i = 0; i < field_count; i++) {
    if (i != 0) {
      FROTH_TRY(frothy_sb_append_text(builder, ", "));
    }
    FROTH_TRY(frothy_value_render_append(runtime, fields[i], builder,
                                         record_stack, depth + 1));
  }
  return FROTH_OK;
}

static froth_error_t frothy_value_render_append(
    const frothy_runtime_t *runtime, frothy_value_t value,
    frothy_string_builder_t *builder, size_t *record_stack, size_t depth) {
  if (frothy_value_is_int(value)) {
    return frothy_sb_appendf(builder, "%d", frothy_value_as_int(value));
  }
  if (frothy_value_is_bool(value)) {
    return frothy_sb_append_text(builder,
                                 frothy_value_as_bool(value) ? "true" : "false");
  }
  if (frothy_value_is_nil(value)) {
    return frothy_sb_append_text(builder, "nil");
  }
  if (frothy_value_is_slot_designator(value)) {
    const char *name = NULL;

    FROTH_TRY(frothy_value_get_slot_designator_name(value, &name));
    return frothy_sb_appendf(builder, "@%s", name);
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
      return frothy_sb_append_quoted(
          builder,
          (const char *)frothy_runtime_payload_const_ptr(
              runtime, object->as.text.payload.offset),
          object->as.text.length);
    case FROTHY_OBJECT_CELLS:
      return frothy_sb_appendf(builder, "<cells %d>",
                               object->as.cells.span.length);
    case FROTHY_OBJECT_CODE:
      return frothy_sb_appendf(builder, "<fn/%zu>", object->as.code.arity);
    case FROTHY_OBJECT_NATIVE:
      return frothy_sb_appendf(builder, "<native %s/%zu>",
                               object->as.native.name,
                               object->as.native.arity);
    case FROTHY_OBJECT_RECORD_DEF:
      return frothy_record_def_render_append(runtime, object, builder);
    case FROTHY_OBJECT_RECORD:
      return frothy_record_render_append(runtime, object_id, object, builder,
                                         record_stack, depth);
    case FROTHY_OBJECT_FREE:
      return FROTH_ERROR_BOUNDS;
    }
  }

  return FROTH_ERROR_TYPE_MISMATCH;
}

froth_error_t frothy_value_render(const frothy_runtime_t *runtime,
                                  frothy_value_t value, char **out_text) {
  frothy_string_builder_t builder;
  size_t record_stack[FROTHY_OBJECT_CAPACITY];
  froth_error_t err;

  *out_text = NULL;
  frothy_sb_init(&builder);
  err = frothy_value_render_append(runtime, value, &builder, record_stack, 0);
  if (err != FROTH_OK) {
    free(builder.data);
    return err;
  }
  *out_text = builder.data;
  return FROTH_OK;
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
  if (frothy_value_is_slot_designator(lhs) ||
      frothy_value_is_slot_designator(rhs)) {
    *equal_out = frothy_value_is_slot_designator(lhs) &&
                 frothy_value_is_slot_designator(rhs) &&
                 frothy_value_slot_index(lhs) == frothy_value_slot_index(rhs);
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
  void *bytes = NULL;
  froth_error_t err;

  if (runtime == NULL || out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  if (length > SIZE_MAX - 1) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }
  if (length > 0 && text == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  memset(&object, 0, sizeof(object));
  object.kind = FROTHY_OBJECT_TEXT;
  err = frothy_runtime_alloc_payload(runtime, length + 1, &object.as.text.payload,
                                     &bytes);
  if (err != FROTH_OK) {
    return err;
  }
  if (length > 0) {
    memcpy(bytes, text, length);
  }
  ((char *)bytes)[length] = '\0';
  object.as.text.length = length;

  err = frothy_runtime_append_object(runtime, &object, out);
  if (err != FROTH_OK) {
    frothy_runtime_release_payload(runtime, object.as.text.payload);
  }
  return err;
}

froth_error_t frothy_runtime_get_text(const frothy_runtime_t *runtime,
                                      frothy_value_t value, const char **text,
                                      size_t *length_out) {
  const frothy_object_t *object;

  if (runtime == NULL || text == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_TEXT,
                                      &object));
  *text = (const char *)frothy_runtime_payload_const_ptr(
      runtime, object->as.text.payload.offset);
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

  if (runtime == NULL || out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
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

  if (runtime == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

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

static froth_error_t frothy_runtime_code_payload_from_view(
    const frothy_runtime_t *runtime, const frothy_ir_program_t *program,
    frothy_payload_span_t *payload_out) {
  const uint8_t *base = (const uint8_t *)program->storage_base;
  const uint8_t *payload_base = runtime->payload_storage.bytes;
  size_t offset;

  if (program->storage_kind != FROTHY_IR_STORAGE_VIEW || payload_out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  if (program->storage_size == 0) {
    payload_out->offset = 0;
    payload_out->length = 0;
    return FROTH_OK;
  }
  if (base == NULL || base < payload_base ||
      base > payload_base + runtime->payload_capacity) {
    return FROTH_ERROR_BOUNDS;
  }

  offset = (size_t)(base - payload_base);
  if (offset > runtime->payload_capacity ||
      program->storage_size > runtime->payload_capacity - offset) {
    return FROTH_ERROR_BOUNDS;
  }

  payload_out->offset = offset;
  payload_out->length = program->storage_size;
  return FROTH_OK;
}

froth_error_t frothy_runtime_alloc_packed_code(frothy_runtime_t *runtime,
                                               const frothy_ir_program_t *program,
                                               frothy_ir_node_id_t body,
                                               size_t arity,
                                               size_t local_count,
                                               frothy_value_t *out) {
  frothy_object_t object;
  frothy_payload_span_t payload = {0};
  froth_error_t err;

  memset(&object, 0, sizeof(object));
  err = frothy_runtime_code_payload_from_view(runtime, program, &payload);
  if (err != FROTH_OK) {
    return err;
  }

  object.kind = FROTHY_OBJECT_CODE;
  object.as.code.payload = payload;
  object.as.code.arity = arity;
  object.as.code.local_count = local_count;
  object.as.code.body = body;
  object.as.code.program = *program;
  err = frothy_runtime_append_object(runtime, &object, out);
  if (err != FROTH_OK) {
    return err;
  }
  return FROTH_OK;
}

froth_error_t frothy_runtime_alloc_code(frothy_runtime_t *runtime,
                                        const frothy_ir_program_t *program,
                                        frothy_ir_node_id_t body, size_t arity,
                                        size_t local_count,
                                        frothy_value_t *out) {
  frothy_ir_program_t packed_program;
  frothy_payload_span_t payload;
  froth_error_t err;
  void *storage = NULL;

  err = frothy_ir_program_clone_packed_size(program, &payload.length);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_runtime_alloc_payload(runtime, payload.length, &payload, &storage);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_ir_program_clone_packed(program, storage, payload.length,
                                       &packed_program);
  if (err != FROTH_OK) {
    frothy_runtime_release_payload(runtime, payload);
    return err;
  }
  err = frothy_runtime_alloc_packed_code(runtime, &packed_program, body, arity,
                                         local_count, out);
  if (err != FROTH_OK) {
    frothy_runtime_release_payload(runtime, payload);
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

  if (runtime == NULL || out == NULL || fn == NULL || name == NULL) {
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

  if (runtime == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

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

froth_error_t frothy_runtime_alloc_record_def(frothy_runtime_t *runtime,
                                              const char *name,
                                              const char *const *field_names,
                                              size_t field_count,
                                              frothy_value_t *out) {
  frothy_object_t object;
  uint8_t *cursor = NULL;
  size_t payload_size = sizeof(uint32_t);
  size_t i;
  froth_error_t err;

  if (name == NULL || field_names == NULL || field_count == 0) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(frothy_add_size(payload_size, strlen(name) + 1, &payload_size));
  for (i = 0; i < field_count; i++) {
    if (field_names[i] == NULL || field_names[i][0] == '\0') {
      return FROTH_ERROR_BOUNDS;
    }
    FROTH_TRY(frothy_add_size(payload_size, strlen(field_names[i]) + 1,
                              &payload_size));
  }

  memset(&object, 0, sizeof(object));
  object.kind = FROTHY_OBJECT_RECORD_DEF;
  object.as.record_def.field_count = field_count;
  err = frothy_runtime_alloc_payload(runtime, payload_size,
                                     &object.as.record_def.payload,
                                     (void **)&cursor);
  if (err != FROTH_OK) {
    return err;
  }

  {
    uint32_t stored_field_count = (uint32_t)field_count;

    memcpy(cursor, &stored_field_count, sizeof(stored_field_count));
    cursor += sizeof(stored_field_count);
  }
  memcpy(cursor, name, strlen(name) + 1);
  cursor += strlen(name) + 1;
  for (i = 0; i < field_count; i++) {
    memcpy(cursor, field_names[i], strlen(field_names[i]) + 1);
    cursor += strlen(field_names[i]) + 1;
  }

  err = frothy_runtime_append_object(runtime, &object, out);
  if (err != FROTH_OK) {
    frothy_runtime_release_payload(runtime, object.as.record_def.payload);
  }
  return err;
}

froth_error_t frothy_runtime_alloc_record_def_from_ir(
    frothy_runtime_t *runtime, const char *name,
    const frothy_ir_program_t *program, size_t first_field, size_t field_count,
    frothy_value_t *out) {
  frothy_object_t object;
  uint8_t *cursor = NULL;
  size_t payload_size = sizeof(uint32_t);
  size_t i;
  froth_error_t err;

  if (runtime == NULL || name == NULL || program == NULL || out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  if (field_count == 0 || first_field > program->link_count ||
      field_count > program->link_count - first_field) {
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(frothy_add_size(payload_size, strlen(name) + 1, &payload_size));
  for (i = 0; i < field_count; i++) {
    const char *field_name = NULL;

    FROTH_TRY(frothy_record_def_field_name_from_ir(program, first_field, i,
                                                   &field_name));
    if (field_name == NULL || field_name[0] == '\0') {
      return FROTH_ERROR_BOUNDS;
    }
    FROTH_TRY(frothy_add_size(payload_size, strlen(field_name) + 1,
                              &payload_size));
  }

  memset(&object, 0, sizeof(object));
  object.kind = FROTHY_OBJECT_RECORD_DEF;
  object.as.record_def.field_count = field_count;
  err = frothy_runtime_alloc_payload(runtime, payload_size,
                                     &object.as.record_def.payload,
                                     (void **)&cursor);
  if (err != FROTH_OK) {
    return err;
  }

  {
    uint32_t stored_field_count = (uint32_t)field_count;

    memcpy(cursor, &stored_field_count, sizeof(stored_field_count));
    cursor += sizeof(stored_field_count);
  }
  memcpy(cursor, name, strlen(name) + 1);
  cursor += strlen(name) + 1;
  for (i = 0; i < field_count; i++) {
    const char *field_name = NULL;

    err = frothy_record_def_field_name_from_ir(program, first_field, i,
                                               &field_name);
    if (err != FROTH_OK) {
      frothy_runtime_release_payload(runtime, object.as.record_def.payload);
      return err;
    }
    memcpy(cursor, field_name, strlen(field_name) + 1);
    cursor += strlen(field_name) + 1;
  }

  err = frothy_runtime_append_object(runtime, &object, out);
  if (err != FROTH_OK) {
    frothy_runtime_release_payload(runtime, object.as.record_def.payload);
  }
  return err;
}

froth_error_t frothy_runtime_get_record_def(const frothy_runtime_t *runtime,
                                            frothy_value_t value,
                                            const char **name_out,
                                            size_t *field_count_out) {
  const frothy_object_t *object;

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_RECORD_DEF,
                                      &object));
  return frothy_runtime_record_def_object_view(runtime, object, name_out,
                                               field_count_out, NULL);
}

froth_error_t frothy_runtime_record_def_field_name(
    const frothy_runtime_t *runtime, frothy_value_t value, size_t field_index,
    const char **field_name_out) {
  const frothy_object_t *object;
  const char *name = NULL;
  const char *cursor = NULL;
  const char *end;
  size_t field_count = 0;
  size_t i;

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_RECORD_DEF,
                                      &object));
  FROTH_TRY(frothy_runtime_record_def_object_view(runtime, object, &name,
                                                  &field_count, &cursor));
  (void)name;
  if (field_index >= field_count) {
    return FROTH_ERROR_BOUNDS;
  }

  end = (const char *)frothy_runtime_payload_const_ptr(
            runtime, object->as.record_def.payload.offset) +
        object->as.record_def.payload.length;
  for (i = 0; i <= field_index; i++) {
    FROTH_TRY(frothy_record_def_next_string(&cursor, end, field_name_out));
  }
  return FROTH_OK;
}

froth_error_t frothy_runtime_record_def_field_index(
    const frothy_runtime_t *runtime, frothy_value_t value, const char *field_name,
    size_t *field_index_out) {
  const frothy_object_t *object;
  const char *name = NULL;
  const char *cursor = NULL;
  const char *end;
  size_t field_count = 0;
  size_t i;

  if (field_name == NULL || field_index_out == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_RECORD_DEF,
                                      &object));
  FROTH_TRY(frothy_runtime_record_def_object_view(runtime, object, &name,
                                                  &field_count, &cursor));
  (void)name;
  end = (const char *)frothy_runtime_payload_const_ptr(
            runtime, object->as.record_def.payload.offset) +
        object->as.record_def.payload.length;
  for (i = 0; i < field_count; i++) {
    const char *candidate = NULL;

    FROTH_TRY(frothy_record_def_next_string(&cursor, end, &candidate));
    if (strcmp(candidate, field_name) == 0) {
      *field_index_out = i;
      return FROTH_OK;
    }
  }

  return FROTH_ERROR_BOUNDS;
}

froth_error_t frothy_runtime_alloc_record(frothy_runtime_t *runtime,
                                          frothy_value_t definition,
                                          const frothy_value_t *fields,
                                          size_t field_count,
                                          frothy_value_t *out) {
  frothy_object_t object;
  const char *name = NULL;
  size_t expected_field_count = 0;
  frothy_value_t *storage = NULL;
  size_t i;
  froth_error_t err;

  if (fields == NULL || field_count == 0) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_TRY(frothy_runtime_get_record_def(runtime, definition, &name,
                                          &expected_field_count));
  (void)name;
  if (field_count != expected_field_count) {
    return FROTH_ERROR_SIGNATURE;
  }
  for (i = 0; i < field_count; i++) {
    FROTH_TRY(frothy_record_value_allowed(runtime, fields[i]));
  }

  memset(&object, 0, sizeof(object));
  object.kind = FROTHY_OBJECT_RECORD;
  object.as.record.definition = definition;
  object.as.record.field_count = field_count;
  err = frothy_runtime_alloc_payload(runtime,
                                     field_count * sizeof(*storage),
                                     &object.as.record.payload,
                                     (void **)&storage);
  if (err != FROTH_OK) {
    return err;
  }

  err = frothy_value_retain(runtime, definition);
  if (err != FROTH_OK) {
    frothy_runtime_release_payload(runtime, object.as.record.payload);
    return err;
  }
  for (i = 0; i < field_count; i++) {
    err = frothy_value_retain(runtime, fields[i]);
    if (err != FROTH_OK) {
      size_t j;

      for (j = 0; j < i; j++) {
        (void)frothy_value_release(runtime, fields[j]);
      }
      (void)frothy_value_release(runtime, definition);
      frothy_runtime_release_payload(runtime, object.as.record.payload);
      return err;
    }
    storage[i] = fields[i];
  }

  err = frothy_runtime_append_object(runtime, &object, out);
  if (err != FROTH_OK) {
    for (i = 0; i < field_count; i++) {
      (void)frothy_value_release(runtime, fields[i]);
    }
    (void)frothy_value_release(runtime, definition);
    frothy_runtime_release_payload(runtime, object.as.record.payload);
  }
  return err;
}

froth_error_t frothy_runtime_get_record(const frothy_runtime_t *runtime,
                                        frothy_value_t value,
                                        frothy_value_t *definition_out,
                                        size_t *field_count_out,
                                        const frothy_value_t **fields_out) {
  const frothy_object_t *object;

  FROTH_TRY(frothy_runtime_get_object(runtime, value, FROTHY_OBJECT_RECORD,
                                      &object));
  if (definition_out != NULL) {
    *definition_out = object->as.record.definition;
  }
  if (field_count_out != NULL) {
    *field_count_out = object->as.record.field_count;
  }
  if (fields_out != NULL) {
    FROTH_TRY(frothy_runtime_record_field_values_const(runtime, object,
                                                       fields_out));
  }
  return FROTH_OK;
}

froth_error_t frothy_runtime_record_read_field(const frothy_runtime_t *runtime,
                                               frothy_value_t value,
                                               const char *field_name,
                                               frothy_value_t *out) {
  frothy_value_t definition = frothy_value_make_nil();
  const frothy_value_t *fields = NULL;
  size_t field_count = 0;
  size_t field_index = 0;

  FROTH_TRY(frothy_runtime_get_record(runtime, value, &definition, &field_count,
                                      &fields));
  (void)field_count;
  FROTH_TRY(frothy_runtime_record_def_field_index(runtime, definition, field_name,
                                                  &field_index));
  *out = fields[field_index];
  return frothy_value_retain((frothy_runtime_t *)runtime, *out);
}

froth_error_t frothy_runtime_record_write_field(frothy_runtime_t *runtime,
                                                frothy_value_t value,
                                                const char *field_name,
                                                frothy_value_t stored_value) {
  froth_error_t err;
  size_t object_id;
  frothy_object_t *object;
  frothy_value_t *fields = NULL;
  size_t field_index = 0;

  FROTH_TRY(frothy_record_value_allowed(runtime, stored_value));
  if (!frothy_value_is_object_ref(value)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  object_id = frothy_value_object_index(value);
  if (object_id >= runtime->object_count || !runtime->objects[object_id].live) {
    return FROTH_ERROR_BOUNDS;
  }
  object = &runtime->objects[object_id];
  if (object->kind != FROTHY_OBJECT_RECORD) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  FROTH_TRY(frothy_runtime_record_def_field_index(
      runtime, object->as.record.definition, field_name, &field_index));
  FROTH_TRY(frothy_runtime_record_field_values(runtime, object, &fields));
  err = frothy_value_release(runtime, fields[field_index]);
  if (err != FROTH_OK) {
    return err;
  }
  fields[field_index] = stored_value;
  return FROTH_OK;
}
