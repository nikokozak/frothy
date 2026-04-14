#include "frothy_snapshot_codec.h"

#include "froth_slot_table.h"
#include "froth_snapshot.h"
#include "froth_vm.h"
#include "frothy_ir.h"
#include "frothy_ir_internal.h"
#include "frothy_value.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FROTHY_SNAPSHOT_MAGIC "FRTY"
#define FROTHY_SNAPSHOT_VERSION 1u
#define FROTHY_SNAPSHOT_IR_VERSION 2u
#define FROTHY_SNAPSHOT_INVALID_INDEX UINT32_MAX

typedef enum {
  FROTHY_SNAPSHOT_VALUE_NIL = 0,
  FROTHY_SNAPSHOT_VALUE_FALSE = 1,
  FROTHY_SNAPSHOT_VALUE_TRUE = 2,
  FROTHY_SNAPSHOT_VALUE_INT = 3,
  FROTHY_SNAPSHOT_VALUE_OBJECT = 4,
} frothy_snapshot_value_tag_t;

typedef struct {
  uint8_t *data;
  size_t length;
  size_t capacity;
} frothy_snapshot_writer_t;

typedef struct {
  const uint8_t *data;
  size_t length;
  size_t offset;
} frothy_snapshot_reader_t;

typedef struct {
  const char *name;
  uint16_t length;
} frothy_snapshot_symbol_t;

typedef struct {
  frothy_snapshot_symbol_t *items;
  size_t count;
  size_t capacity;
} frothy_snapshot_symbol_table_t;

typedef struct {
  size_t runtime_object_id;
} frothy_snapshot_object_t;

typedef struct {
  frothy_snapshot_object_t *items;
  size_t count;
  size_t capacity;
  size_t *index_by_runtime_id;
  size_t runtime_object_count;
} frothy_snapshot_object_table_t;

typedef struct {
  uint32_t symbol_count;
  uint32_t object_count;
  uint32_t binding_count;
  uint8_t *object_kinds;
} frothy_snapshot_layout_t;

typedef struct {
  uint8_t payload[FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES];
  frothy_snapshot_symbol_t symbol_items[FROTH_SLOT_TABLE_SIZE];
  frothy_snapshot_object_t object_items[FROTHY_OBJECT_CAPACITY];
  size_t object_index_by_runtime_id[FROTHY_OBJECT_CAPACITY];
  uint8_t layout_object_kinds[FROTHY_OBJECT_CAPACITY];
  frothy_snapshot_symbol_t decoded_symbols[FROTH_SLOT_TABLE_SIZE];
  frothy_value_t decoded_objects[FROTHY_OBJECT_CAPACITY];
} frothy_snapshot_codec_workspace_t;

static froth_error_t frothy_snapshot_test_error_after_objects = FROTH_OK;
static frothy_snapshot_codec_workspace_t frothy_snapshot_codec_workspace;

void frothy_snapshot_test_set_error_after_objects(froth_error_t err) {
  frothy_snapshot_test_error_after_objects = err;
}

static frothy_runtime_t *frothy_runtime(void) {
  return &froth_vm.frothy_runtime;
}

static frothy_snapshot_codec_workspace_t *frothy_snapshot_workspace(void) {
  return &frothy_snapshot_codec_workspace;
}

static void frothy_snapshot_workspace_reset(
    frothy_snapshot_codec_workspace_t *workspace) {
  memset(workspace->symbol_items, 0, sizeof(workspace->symbol_items));
  memset(workspace->object_items, 0, sizeof(workspace->object_items));
  memset(workspace->object_index_by_runtime_id, 0,
         sizeof(workspace->object_index_by_runtime_id));
  memset(workspace->layout_object_kinds, 0,
         sizeof(workspace->layout_object_kinds));
  memset(workspace->decoded_symbols, 0, sizeof(workspace->decoded_symbols));
  memset(workspace->decoded_objects, 0, sizeof(workspace->decoded_objects));
}

uint8_t *frothy_snapshot_codec_payload_buffer(size_t *capacity_out) {
  frothy_snapshot_codec_workspace_t *workspace = frothy_snapshot_workspace();

  if (capacity_out != NULL) {
    *capacity_out = sizeof(workspace->payload);
  }
  return workspace->payload;
}

static froth_error_t frothy_snapshot_strdup(const char *text, size_t length,
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

static froth_error_t frothy_snapshot_writer_write_u8(
    frothy_snapshot_writer_t *writer, uint8_t value) {
  if (writer->length + 1 > writer->capacity) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  writer->data[writer->length++] = value;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_writer_write_u16(
    frothy_snapshot_writer_t *writer, uint16_t value) {
  if (writer->length + 2 > writer->capacity) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  writer->data[writer->length++] = (uint8_t)(value & 0xFFu);
  writer->data[writer->length++] = (uint8_t)((value >> 8) & 0xFFu);
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_writer_write_u32(
    frothy_snapshot_writer_t *writer, uint32_t value) {
  if (writer->length + 4 > writer->capacity) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  writer->data[writer->length++] = (uint8_t)(value & 0xFFu);
  writer->data[writer->length++] = (uint8_t)((value >> 8) & 0xFFu);
  writer->data[writer->length++] = (uint8_t)((value >> 16) & 0xFFu);
  writer->data[writer->length++] = (uint8_t)((value >> 24) & 0xFFu);
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_writer_write_i32(
    frothy_snapshot_writer_t *writer, int32_t value) {
  return frothy_snapshot_writer_write_u32(writer, (uint32_t)value);
}

static froth_error_t frothy_snapshot_writer_write_bytes(
    frothy_snapshot_writer_t *writer, const uint8_t *bytes, size_t length) {
  if (writer->length + length > writer->capacity) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  memcpy(writer->data + writer->length, bytes, length);
  writer->length += length;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_reader_read_u8(
    frothy_snapshot_reader_t *reader, uint8_t *out) {
  if (reader->offset + 1 > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  *out = reader->data[reader->offset++];
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_reader_read_u16(
    frothy_snapshot_reader_t *reader, uint16_t *out) {
  if (reader->offset + 2 > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  *out = (uint16_t)reader->data[reader->offset] |
         ((uint16_t)reader->data[reader->offset + 1] << 8);
  reader->offset += 2;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_reader_read_u32(
    frothy_snapshot_reader_t *reader, uint32_t *out) {
  if (reader->offset + 4 > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  *out = (uint32_t)reader->data[reader->offset] |
         ((uint32_t)reader->data[reader->offset + 1] << 8) |
         ((uint32_t)reader->data[reader->offset + 2] << 16) |
         ((uint32_t)reader->data[reader->offset + 3] << 24);
  reader->offset += 4;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_reader_read_i32(
    frothy_snapshot_reader_t *reader, int32_t *out) {
  uint32_t bits = 0;

  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &bits));
  *out = (int32_t)bits;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_reader_read_bytes(
    frothy_snapshot_reader_t *reader, size_t length, const uint8_t **out) {
  if (reader->offset + length > reader->length) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  *out = reader->data + reader->offset;
  reader->offset += length;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_symbols_reserve(
    frothy_snapshot_symbol_table_t *symbols, size_t needed) {
  if (needed <= symbols->capacity) {
    return FROTH_OK;
  }
  return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
}

static void frothy_snapshot_symbols_init(
    frothy_snapshot_symbol_table_t *symbols,
    frothy_snapshot_codec_workspace_t *workspace) {
  memset(symbols, 0, sizeof(*symbols));
  symbols->items = workspace->symbol_items;
  symbols->capacity = FROTH_SLOT_TABLE_SIZE;
}

static froth_error_t frothy_snapshot_symbols_find_or_add(
    frothy_snapshot_symbol_table_t *symbols, const char *name, uint32_t *index) {
  size_t i;
  size_t length;

  for (i = 0; i < symbols->count; i++) {
    if (strcmp(symbols->items[i].name, name) == 0) {
      *index = (uint32_t)i;
      return FROTH_OK;
    }
  }

  length = strlen(name);
  if (length > UINT16_MAX) {
    return FROTH_ERROR_SNAPSHOT_BAD_NAME;
  }

  FROTH_TRY(frothy_snapshot_symbols_reserve(symbols, symbols->count + 1));
  symbols->items[symbols->count].name = name;
  symbols->items[symbols->count].length = (uint16_t)length;
  *index = (uint32_t)symbols->count;
  symbols->count++;
  return FROTH_OK;
}

static void frothy_snapshot_symbols_free(
    frothy_snapshot_symbol_table_t *symbols) {
  memset(symbols, 0, sizeof(*symbols));
}

static froth_error_t frothy_snapshot_objects_init(
    frothy_snapshot_object_table_t *objects, size_t runtime_object_count,
    frothy_snapshot_codec_workspace_t *workspace) {
  size_t i;

  if (runtime_object_count > FROTHY_OBJECT_CAPACITY) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  memset(objects, 0, sizeof(*objects));
  objects->items = workspace->object_items;
  objects->capacity = FROTHY_OBJECT_CAPACITY;
  objects->index_by_runtime_id = workspace->object_index_by_runtime_id;

  for (i = 0; i < runtime_object_count; i++) {
    objects->index_by_runtime_id[i] = SIZE_MAX;
  }
  objects->runtime_object_count = runtime_object_count;
  return FROTH_OK;
}

static void frothy_snapshot_objects_free(frothy_snapshot_object_table_t *objects) {
  memset(objects, 0, sizeof(*objects));
}

static froth_error_t frothy_snapshot_objects_reserve(
    frothy_snapshot_object_table_t *objects, size_t needed) {
  if (needed <= objects->capacity) {
    return FROTH_OK;
  }
  return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
}

static bool frothy_snapshot_objects_lookup(
    const frothy_snapshot_object_table_t *objects, size_t runtime_object_id,
    uint32_t *index_out) {
  if (runtime_object_id >= objects->runtime_object_count ||
      objects->index_by_runtime_id[runtime_object_id] == SIZE_MAX) {
    return false;
  }

  *index_out = (uint32_t)objects->index_by_runtime_id[runtime_object_id];
  return true;
}

static froth_error_t frothy_snapshot_objects_add(
    frothy_snapshot_object_table_t *objects, size_t runtime_object_id,
    uint32_t *index_out) {
  FROTH_TRY(frothy_snapshot_objects_reserve(objects, objects->count + 1));
  objects->items[objects->count].runtime_object_id = runtime_object_id;
  objects->index_by_runtime_id[runtime_object_id] = objects->count;
  *index_out = (uint32_t)objects->count;
  objects->count++;
  return FROTH_OK;
}

static uint32_t frothy_snapshot_binding_count(void) {
  froth_cell_u_t slot_count = froth_slot_count();
  froth_cell_u_t slot_index;
  uint32_t count = 0;

  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    froth_cell_t impl;

    if (!froth_slot_is_overlay(slot_index)) {
      continue;
    }
    if (froth_slot_get_impl(slot_index, &impl) != FROTH_OK) {
      continue;
    }
    (void)impl;
    count++;
  }

  return count;
}

static froth_error_t frothy_snapshot_collect_program_symbols(
    const frothy_ir_program_t *program, frothy_snapshot_symbol_table_t *symbols) {
  size_t i;

  for (i = 0; i < program->node_count; i++) {
    const frothy_ir_node_t *node = &program->nodes[i];
    uint32_t ignored;

    switch (node->kind) {
    case FROTHY_IR_NODE_READ_SLOT:
      FROTH_TRY(frothy_snapshot_symbols_find_or_add(
          symbols, node->as.read_slot.slot_name, &ignored));
      break;
    case FROTHY_IR_NODE_READ_SLOT_FALLBACK:
      FROTH_TRY(frothy_snapshot_symbols_find_or_add(
          symbols, node->as.read_slot_fallback.primary_slot_name, &ignored));
      FROTH_TRY(frothy_snapshot_symbols_find_or_add(
          symbols, node->as.read_slot_fallback.fallback_slot_name, &ignored));
      break;
    case FROTHY_IR_NODE_WRITE_SLOT:
      FROTH_TRY(frothy_snapshot_symbols_find_or_add(
          symbols, node->as.write_slot.slot_name, &ignored));
      break;
    case FROTHY_IR_NODE_WRITE_SLOT_FALLBACK:
      FROTH_TRY(frothy_snapshot_symbols_find_or_add(
          symbols, node->as.write_slot_fallback.primary_slot_name, &ignored));
      FROTH_TRY(frothy_snapshot_symbols_find_or_add(
          symbols, node->as.write_slot_fallback.fallback_slot_name, &ignored));
      break;
    case FROTHY_IR_NODE_SLOT_DESIGNATOR:
      FROTH_TRY(frothy_snapshot_symbols_find_or_add(
          symbols, node->as.slot_designator.slot_name, &ignored));
      break;
    default:
      break;
    }
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_collect_value(
    const frothy_runtime_t *runtime, frothy_value_t value,
    frothy_snapshot_symbol_table_t *symbols,
    frothy_snapshot_object_table_t *objects) {
  const frothy_object_t *object;
  uint32_t ignored;
  size_t runtime_object_id;
  size_t i;
  frothy_value_class_t value_class;

  if (!frothy_value_is_object_ref(value)) {
    return FROTH_OK;
  }

  runtime_object_id = frothy_value_object_index(value);
  if (runtime_object_id >= runtime->object_count) {
    return FROTH_ERROR_BOUNDS;
  }
  if (frothy_snapshot_objects_lookup(objects, runtime_object_id, &ignored)) {
    return FROTH_OK;
  }

  object = &runtime->objects[runtime_object_id];
  if (!object->live) {
    return FROTH_ERROR_BOUNDS;
  }

  switch (object->kind) {
  case FROTHY_OBJECT_TEXT:
    return frothy_snapshot_objects_add(objects, runtime_object_id, &ignored);
  case FROTHY_OBJECT_CELLS:
    for (i = 0; i < (size_t)object->as.cells.span.length; i++) {
      frothy_value_t item = frothy_value_from_cell(
          runtime->cellspace->data[object->as.cells.span.base + (froth_cell_t)i]);

      FROTH_TRY(frothy_value_class(runtime, item, &value_class));
      switch (value_class) {
      case FROTHY_VALUE_CLASS_INT:
      case FROTHY_VALUE_CLASS_BOOL:
      case FROTHY_VALUE_CLASS_NIL:
        break;
      case FROTHY_VALUE_CLASS_TEXT:
        FROTH_TRY(
            frothy_snapshot_collect_value(runtime, item, symbols, objects));
        break;
      case FROTHY_VALUE_CLASS_CELLS:
      case FROTHY_VALUE_CLASS_CODE:
      case FROTHY_VALUE_CLASS_NATIVE:
        return FROTH_ERROR_NOT_PERSISTABLE;
      }
    }
    return frothy_snapshot_objects_add(objects, runtime_object_id, &ignored);
  case FROTHY_OBJECT_CODE:
    FROTH_TRY(
        frothy_snapshot_collect_program_symbols(&object->as.code.program, symbols));
    return frothy_snapshot_objects_add(objects, runtime_object_id, &ignored);
  case FROTHY_OBJECT_NATIVE:
    return FROTH_ERROR_NOT_PERSISTABLE;
  case FROTHY_OBJECT_FREE:
    return FROTH_ERROR_BOUNDS;
  }

  return FROTH_ERROR_BOUNDS;
}

static froth_error_t frothy_snapshot_collect_overlay(
    const frothy_runtime_t *runtime, frothy_snapshot_symbol_table_t *symbols,
    frothy_snapshot_object_table_t *objects) {
  froth_cell_u_t slot_count = froth_slot_count();
  froth_cell_u_t slot_index;

  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    const char *name = NULL;
    froth_cell_t impl = 0;
    uint32_t ignored;

    if (!froth_slot_is_overlay(slot_index)) {
      continue;
    }
    if (froth_slot_get_name(slot_index, &name) != FROTH_OK) {
      continue;
    }

    FROTH_TRY(frothy_snapshot_symbols_find_or_add(symbols, name, &ignored));
    if (froth_slot_get_impl(slot_index, &impl) != FROTH_OK) {
      continue;
    }

    FROTH_TRY(frothy_snapshot_collect_value(runtime, frothy_value_from_cell(impl),
                                           symbols, objects));
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_encode_object_ref(
    const frothy_runtime_t *runtime, const frothy_snapshot_object_table_t *objects,
    frothy_value_t value, uint32_t *index_out) {
  size_t runtime_object_id;
  uint32_t snapshot_object_id;

  if (!frothy_value_is_object_ref(value)) {
    return FROTH_ERROR_TYPE_MISMATCH;
  }

  runtime_object_id = frothy_value_object_index(value);
  if (!frothy_snapshot_objects_lookup(objects, runtime_object_id,
                                      &snapshot_object_id)) {
    return FROTH_ERROR_SNAPSHOT_UNRESOLVED;
  }
  if (runtime_object_id >= runtime->object_count ||
      !runtime->objects[runtime_object_id].live) {
    return FROTH_ERROR_BOUNDS;
  }

  *index_out = snapshot_object_id;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_write_value(
    frothy_snapshot_writer_t *writer, const frothy_runtime_t *runtime,
    const frothy_snapshot_object_table_t *objects, frothy_value_t value) {
  uint32_t object_index;

  if (frothy_value_is_int(value)) {
    FROTH_TRY(
        frothy_snapshot_writer_write_u8(writer, FROTHY_SNAPSHOT_VALUE_INT));
    return frothy_snapshot_writer_write_i32(writer, frothy_value_as_int(value));
  }
  if (frothy_value_is_nil(value)) {
    return frothy_snapshot_writer_write_u8(writer, FROTHY_SNAPSHOT_VALUE_NIL);
  }
  if (frothy_value_is_bool(value)) {
    return frothy_snapshot_writer_write_u8(
        writer, frothy_value_as_bool(value) ? FROTHY_SNAPSHOT_VALUE_TRUE
                                            : FROTHY_SNAPSHOT_VALUE_FALSE);
  }

  FROTH_TRY(frothy_snapshot_encode_object_ref(runtime, objects, value,
                                              &object_index));
  FROTH_TRY(
      frothy_snapshot_writer_write_u8(writer, FROTHY_SNAPSHOT_VALUE_OBJECT));
  return frothy_snapshot_writer_write_u32(writer, object_index);
}

static froth_error_t frothy_snapshot_write_cells_value(
    frothy_snapshot_writer_t *writer, const frothy_runtime_t *runtime,
    const frothy_snapshot_object_table_t *objects, frothy_value_t value) {
  frothy_value_class_t value_class;
  uint32_t object_index;

  FROTH_TRY(frothy_value_class(runtime, value, &value_class));
  switch (value_class) {
  case FROTHY_VALUE_CLASS_INT:
    FROTH_TRY(
        frothy_snapshot_writer_write_u8(writer, FROTHY_SNAPSHOT_VALUE_INT));
    return frothy_snapshot_writer_write_i32(writer, frothy_value_as_int(value));
  case FROTHY_VALUE_CLASS_NIL:
    return frothy_snapshot_writer_write_u8(writer, FROTHY_SNAPSHOT_VALUE_NIL);
  case FROTHY_VALUE_CLASS_BOOL:
    return frothy_snapshot_writer_write_u8(
        writer, frothy_value_as_bool(value) ? FROTHY_SNAPSHOT_VALUE_TRUE
                                            : FROTHY_SNAPSHOT_VALUE_FALSE);
  case FROTHY_VALUE_CLASS_TEXT:
    FROTH_TRY(frothy_snapshot_encode_object_ref(runtime, objects, value,
                                                &object_index));
    FROTH_TRY(
        frothy_snapshot_writer_write_u8(writer, FROTHY_SNAPSHOT_VALUE_OBJECT));
    return frothy_snapshot_writer_write_u32(writer, object_index);
  case FROTHY_VALUE_CLASS_CELLS:
  case FROTHY_VALUE_CLASS_CODE:
  case FROTHY_VALUE_CLASS_NATIVE:
    return FROTH_ERROR_NOT_PERSISTABLE;
  }

  return FROTH_ERROR_NOT_PERSISTABLE;
}

static uint32_t frothy_snapshot_encode_node_id(frothy_ir_node_id_t node_id) {
  return node_id == FROTHY_IR_NODE_INVALID ? FROTHY_SNAPSHOT_INVALID_INDEX
                                           : (uint32_t)node_id;
}

static froth_error_t frothy_snapshot_symbol_index(
    const frothy_snapshot_symbol_table_t *symbols, const char *name,
    uint32_t *index_out) {
  size_t i;

  for (i = 0; i < symbols->count; i++) {
    if (strcmp(symbols->items[i].name, name) == 0) {
      *index_out = (uint32_t)i;
      return FROTH_OK;
    }
  }

  return FROTH_ERROR_SNAPSHOT_UNRESOLVED;
}

static froth_error_t frothy_snapshot_write_literal(
    frothy_snapshot_writer_t *writer, const frothy_ir_literal_t *literal) {
  size_t length;

  FROTH_TRY(frothy_snapshot_writer_write_u8(writer, (uint8_t)literal->kind));
  switch (literal->kind) {
  case FROTHY_IR_LITERAL_INT:
    return frothy_snapshot_writer_write_i32(writer,
                                            (int32_t)literal->as.int_value);
  case FROTHY_IR_LITERAL_BOOL:
    return frothy_snapshot_writer_write_u8(writer,
                                           literal->as.bool_value ? 1u : 0u);
  case FROTHY_IR_LITERAL_NIL:
    return FROTH_OK;
  case FROTHY_IR_LITERAL_TEXT:
    length = strlen(literal->as.text_value);
    FROTH_TRY(frothy_snapshot_writer_write_u32(writer, (uint32_t)length));
    return frothy_snapshot_writer_write_bytes(
        writer, (const uint8_t *)literal->as.text_value, length);
  }

  return FROTH_ERROR_SNAPSHOT_FORMAT;
}

static froth_error_t frothy_snapshot_write_node(
    frothy_snapshot_writer_t *writer, const frothy_ir_node_t *node,
    const frothy_snapshot_symbol_table_t *symbols) {
  uint32_t symbol_index = 0;

  FROTH_TRY(frothy_snapshot_writer_write_u8(writer, (uint8_t)node->kind));
  switch (node->kind) {
  case FROTHY_IR_NODE_LIT:
    return frothy_snapshot_writer_write_u32(writer,
                                            (uint32_t)node->as.lit.literal_id);
  case FROTHY_IR_NODE_READ_LOCAL:
    return frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.read_local.local_index);
  case FROTHY_IR_NODE_WRITE_LOCAL:
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.write_local.local_index));
    return frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.write_local.value);
  case FROTHY_IR_NODE_READ_SLOT:
    FROTH_TRY(frothy_snapshot_symbol_index(symbols, node->as.read_slot.slot_name,
                                           &symbol_index));
    return frothy_snapshot_writer_write_u32(writer, symbol_index);
  case FROTHY_IR_NODE_READ_SLOT_FALLBACK: {
    uint32_t fallback_symbol_index = 0;

    FROTH_TRY(frothy_snapshot_symbol_index(
        symbols, node->as.read_slot_fallback.primary_slot_name, &symbol_index));
    FROTH_TRY(frothy_snapshot_symbol_index(
        symbols, node->as.read_slot_fallback.fallback_slot_name,
        &fallback_symbol_index));
    FROTH_TRY(frothy_snapshot_writer_write_u32(writer, symbol_index));
    return frothy_snapshot_writer_write_u32(writer, fallback_symbol_index);
  }
  case FROTHY_IR_NODE_WRITE_SLOT:
    FROTH_TRY(frothy_snapshot_symbol_index(symbols, node->as.write_slot.slot_name,
                                           &symbol_index));
    FROTH_TRY(frothy_snapshot_writer_write_u32(writer, symbol_index));
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.write_slot.value));
    return frothy_snapshot_writer_write_u8(
        writer, node->as.write_slot.require_existing ? 1u : 0u);
  case FROTHY_IR_NODE_WRITE_SLOT_FALLBACK: {
    uint32_t fallback_symbol_index = 0;

    FROTH_TRY(frothy_snapshot_symbol_index(
        symbols, node->as.write_slot_fallback.primary_slot_name, &symbol_index));
    FROTH_TRY(frothy_snapshot_symbol_index(
        symbols, node->as.write_slot_fallback.fallback_slot_name,
        &fallback_symbol_index));
    FROTH_TRY(frothy_snapshot_writer_write_u32(writer, symbol_index));
    FROTH_TRY(frothy_snapshot_writer_write_u32(writer, fallback_symbol_index));
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.write_slot_fallback.value));
    return frothy_snapshot_writer_write_u8(
        writer, node->as.write_slot_fallback.require_existing ? 1u : 0u);
  }
  case FROTHY_IR_NODE_SLOT_DESIGNATOR:
    FROTH_TRY(frothy_snapshot_symbol_index(symbols,
                                           node->as.slot_designator.slot_name,
                                           &symbol_index));
    return frothy_snapshot_writer_write_u32(writer, symbol_index);
  case FROTHY_IR_NODE_READ_INDEX:
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.read_index.base));
    return frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.read_index.index);
  case FROTHY_IR_NODE_WRITE_INDEX:
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.write_index.base));
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.write_index.index));
    return frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.write_index.value);
  case FROTHY_IR_NODE_FN:
    FROTH_TRY(frothy_snapshot_writer_write_u32(writer,
                                               (uint32_t)node->as.fn.arity));
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.fn.local_count));
    return frothy_snapshot_writer_write_u32(writer,
                                            (uint32_t)node->as.fn.body);
  case FROTHY_IR_NODE_CALL:
    FROTH_TRY(frothy_snapshot_writer_write_u8(
        writer, (uint8_t)node->as.call.builtin));
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, frothy_snapshot_encode_node_id(node->as.call.callee)));
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.call.first_arg));
    return frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.call.arg_count);
  case FROTHY_IR_NODE_IF:
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.if_expr.condition));
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.if_expr.then_branch));
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, frothy_snapshot_encode_node_id(node->as.if_expr.else_branch)));
    return frothy_snapshot_writer_write_u8(
        writer, node->as.if_expr.has_else_branch ? 1u : 0u);
  case FROTHY_IR_NODE_WHILE:
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.while_expr.condition));
    return frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.while_expr.body);
  case FROTHY_IR_NODE_SEQ:
    FROTH_TRY(frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.seq.first_item));
    return frothy_snapshot_writer_write_u32(
        writer, (uint32_t)node->as.seq.item_count);
  }

  return FROTH_ERROR_SNAPSHOT_FORMAT;
}

static froth_error_t frothy_snapshot_write_code_object(
    frothy_snapshot_writer_t *writer, const frothy_object_t *object,
    const frothy_snapshot_symbol_table_t *symbols) {
  const frothy_ir_program_t *program = &object->as.code.program;
  size_t i;

  FROTH_TRY(
      frothy_snapshot_writer_write_u32(writer, (uint32_t)object->as.code.arity));
  FROTH_TRY(frothy_snapshot_writer_write_u32(
      writer, (uint32_t)object->as.code.local_count));
  FROTH_TRY(frothy_snapshot_writer_write_u32(
      writer, (uint32_t)object->as.code.body));
  FROTH_TRY(
      frothy_snapshot_writer_write_u32(writer, (uint32_t)program->root));
  FROTH_TRY(frothy_snapshot_writer_write_u32(
      writer, (uint32_t)program->root_local_count));
  FROTH_TRY(frothy_snapshot_writer_write_u32(
      writer, (uint32_t)program->literal_count));
  FROTH_TRY(
      frothy_snapshot_writer_write_u32(writer, (uint32_t)program->node_count));
  FROTH_TRY(
      frothy_snapshot_writer_write_u32(writer, (uint32_t)program->link_count));

  for (i = 0; i < program->literal_count; i++) {
    FROTH_TRY(frothy_snapshot_write_literal(writer, &program->literals[i]));
  }
  for (i = 0; i < program->node_count; i++) {
    FROTH_TRY(frothy_snapshot_write_node(writer, &program->nodes[i], symbols));
  }
  for (i = 0; i < program->link_count; i++) {
    FROTH_TRY(
        frothy_snapshot_writer_write_u32(writer, (uint32_t)program->links[i]));
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_write_object_table(
    frothy_snapshot_writer_t *writer, const frothy_runtime_t *runtime,
    const frothy_snapshot_symbol_table_t *symbols,
    const frothy_snapshot_object_table_t *objects) {
  size_t i;

  FROTH_TRY(
      frothy_snapshot_writer_write_u32(writer, (uint32_t)objects->count));
  for (i = 0; i < objects->count; i++) {
    const frothy_object_t *object =
        &runtime->objects[objects->items[i].runtime_object_id];

    FROTH_TRY(frothy_snapshot_writer_write_u8(writer, (uint8_t)object->kind));
    switch (object->kind) {
    case FROTHY_OBJECT_TEXT:
      FROTH_TRY(frothy_snapshot_writer_write_u32(
          writer, (uint32_t)object->as.text.length));
      FROTH_TRY(frothy_snapshot_writer_write_bytes(
          writer,
          runtime->payload_storage.bytes + object->as.text.payload.offset,
          object->as.text.length));
      break;
    case FROTHY_OBJECT_CELLS: {
      size_t cell_index;

      FROTH_TRY(frothy_snapshot_writer_write_u32(
          writer, (uint32_t)object->as.cells.span.length));
      for (cell_index = 0; cell_index < (size_t)object->as.cells.span.length;
           cell_index++) {
        frothy_value_t item = frothy_value_from_cell(
            runtime->cellspace->data[object->as.cells.span.base +
                                     (froth_cell_t)cell_index]);

        FROTH_TRY(
            frothy_snapshot_write_cells_value(writer, runtime, objects, item));
      }
      break;
    }
    case FROTHY_OBJECT_CODE:
      FROTH_TRY(frothy_snapshot_write_code_object(writer, object, symbols));
      break;
    case FROTHY_OBJECT_NATIVE:
      return FROTH_ERROR_NOT_PERSISTABLE;
    case FROTHY_OBJECT_FREE:
      return FROTH_ERROR_BOUNDS;
    }
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_write_symbols(
    frothy_snapshot_writer_t *writer,
    const frothy_snapshot_symbol_table_t *symbols) {
  size_t i;

  FROTH_TRY(
      frothy_snapshot_writer_write_u32(writer, (uint32_t)symbols->count));
  for (i = 0; i < symbols->count; i++) {
    FROTH_TRY(frothy_snapshot_writer_write_u16(writer, symbols->items[i].length));
    FROTH_TRY(frothy_snapshot_writer_write_bytes(
        writer, (const uint8_t *)symbols->items[i].name, symbols->items[i].length));
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_write_bindings(
    frothy_snapshot_writer_t *writer, const frothy_runtime_t *runtime,
    const frothy_snapshot_symbol_table_t *symbols,
    const frothy_snapshot_object_table_t *objects) {
  froth_cell_u_t slot_count = froth_slot_count();
  froth_cell_u_t slot_index;
  uint32_t binding_count = frothy_snapshot_binding_count();

  FROTH_TRY(frothy_snapshot_writer_write_u32(writer, binding_count));
  for (slot_index = 0; slot_index < slot_count; slot_index++) {
    const char *name = NULL;
    froth_cell_t impl = 0;
    uint32_t symbol_index = 0;

    if (!froth_slot_is_overlay(slot_index)) {
      continue;
    }
    if (froth_slot_get_name(slot_index, &name) != FROTH_OK) {
      continue;
    }
    if (froth_slot_get_impl(slot_index, &impl) != FROTH_OK) {
      continue;
    }

    FROTH_TRY(frothy_snapshot_symbol_index(symbols, name, &symbol_index));
    FROTH_TRY(frothy_snapshot_writer_write_u32(writer, symbol_index));
    FROTH_TRY(frothy_snapshot_write_value(writer, runtime, objects,
                                          frothy_value_from_cell(impl)));
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_write_payload(
    frothy_snapshot_writer_t *writer, const frothy_runtime_t *runtime,
    const frothy_snapshot_symbol_table_t *symbols,
    const frothy_snapshot_object_table_t *objects) {
  FROTH_TRY(frothy_snapshot_writer_write_bytes(
      writer, (const uint8_t *)FROTHY_SNAPSHOT_MAGIC, 4));
  FROTH_TRY(
      frothy_snapshot_writer_write_u16(writer, FROTHY_SNAPSHOT_VERSION));
  FROTH_TRY(
      frothy_snapshot_writer_write_u16(writer, FROTHY_SNAPSHOT_IR_VERSION));
  FROTH_TRY(frothy_snapshot_write_symbols(writer, symbols));
  FROTH_TRY(frothy_snapshot_write_object_table(writer, runtime, symbols, objects));
  return frothy_snapshot_write_bindings(writer, runtime, symbols, objects);
}

static froth_error_t frothy_snapshot_validate_node_id(uint32_t node_id,
                                                      uint32_t node_count,
                                                      bool allow_invalid) {
  if (allow_invalid && node_id == FROTHY_SNAPSHOT_INVALID_INDEX) {
    return FROTH_OK;
  }
  if (node_id >= node_count) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_validate_value(
    frothy_snapshot_reader_t *reader, const uint8_t *object_kinds,
    uint32_t object_count, bool text_only) {
  uint8_t tag = 0;
  uint32_t object_index = 0;
  int32_t ignored_int = 0;

  FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &tag));
  switch ((frothy_snapshot_value_tag_t)tag) {
  case FROTHY_SNAPSHOT_VALUE_NIL:
  case FROTHY_SNAPSHOT_VALUE_FALSE:
  case FROTHY_SNAPSHOT_VALUE_TRUE:
    return FROTH_OK;
  case FROTHY_SNAPSHOT_VALUE_INT:
    return frothy_snapshot_reader_read_i32(reader, &ignored_int);
  case FROTHY_SNAPSHOT_VALUE_OBJECT:
    FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &object_index));
    if (object_index >= object_count) {
      return FROTH_ERROR_SNAPSHOT_FORMAT;
    }
    if (text_only && object_kinds[object_index] != FROTHY_OBJECT_TEXT) {
      return FROTH_ERROR_NOT_PERSISTABLE;
    }
    if (!text_only && object_kinds[object_index] == FROTHY_OBJECT_NATIVE) {
      return FROTH_ERROR_NOT_PERSISTABLE;
    }
    return FROTH_OK;
  }

  return FROTH_ERROR_SNAPSHOT_FORMAT;
}

static froth_error_t frothy_snapshot_validate_literal(
    frothy_snapshot_reader_t *reader) {
  uint8_t kind = 0;
  uint32_t length = 0;
  const uint8_t *ignored = NULL;
  int32_t ignored_int = 0;
  uint8_t ignored_bool = 0;

  FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &kind));
  switch ((frothy_ir_literal_kind_t)kind) {
  case FROTHY_IR_LITERAL_INT:
    return frothy_snapshot_reader_read_i32(reader, &ignored_int);
  case FROTHY_IR_LITERAL_BOOL:
    return frothy_snapshot_reader_read_u8(reader, &ignored_bool);
  case FROTHY_IR_LITERAL_NIL:
    return FROTH_OK;
  case FROTHY_IR_LITERAL_TEXT:
    FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &length));
    return frothy_snapshot_reader_read_bytes(reader, length, &ignored);
  }

  return FROTH_ERROR_SNAPSHOT_FORMAT;
}

static froth_error_t frothy_snapshot_validate_code_object(
    frothy_snapshot_reader_t *reader, uint32_t symbol_count) {
  uint32_t arity = 0;
  uint32_t local_count = 0;
  uint32_t body = 0;
  uint32_t root = 0;
  uint32_t root_local_count = 0;
  uint32_t literal_count = 0;
  uint32_t node_count = 0;
  uint32_t link_count = 0;
  uint32_t i;

  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &arity));
  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &local_count));
  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &body));
  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &root));
  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &root_local_count));
  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &literal_count));
  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &node_count));
  FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &link_count));
  if (local_count < arity || body >= node_count || root >= node_count) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
  if (literal_count > FROTHY_IR_LITERAL_CAPACITY ||
      node_count > FROTHY_IR_NODE_CAPACITY ||
      link_count > FROTHY_IR_LINK_CAPACITY) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }
  if (root_local_count > UINT32_MAX) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  for (i = 0; i < literal_count; i++) {
    FROTH_TRY(frothy_snapshot_validate_literal(reader));
  }

  for (i = 0; i < node_count; i++) {
    uint8_t kind = 0;
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint8_t flag = 0;

    FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &kind));
    switch ((frothy_ir_node_kind_t)kind) {
    case FROTHY_IR_NODE_LIT:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      if (a >= literal_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      break;
    case FROTHY_IR_NODE_READ_LOCAL:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      break;
    case FROTHY_IR_NODE_WRITE_LOCAL:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_validate_node_id(b, node_count, false));
      break;
    case FROTHY_IR_NODE_READ_SLOT:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      if (a >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      break;
    case FROTHY_IR_NODE_READ_SLOT_FALLBACK:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      if (a >= symbol_count || b >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      break;
    case FROTHY_IR_NODE_WRITE_SLOT:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &flag));
      if (a >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      FROTH_TRY(frothy_snapshot_validate_node_id(b, node_count, false));
      (void)flag;
      break;
    case FROTHY_IR_NODE_WRITE_SLOT_FALLBACK:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &c));
      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &flag));
      if (a >= symbol_count || b >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      FROTH_TRY(frothy_snapshot_validate_node_id(c, node_count, false));
      (void)flag;
      break;
    case FROTHY_IR_NODE_SLOT_DESIGNATOR:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      if (a >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      break;
    case FROTHY_IR_NODE_READ_INDEX:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_validate_node_id(a, node_count, false));
      FROTH_TRY(frothy_snapshot_validate_node_id(b, node_count, false));
      break;
    case FROTHY_IR_NODE_WRITE_INDEX:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &c));
      FROTH_TRY(frothy_snapshot_validate_node_id(a, node_count, false));
      FROTH_TRY(frothy_snapshot_validate_node_id(b, node_count, false));
      FROTH_TRY(frothy_snapshot_validate_node_id(c, node_count, false));
      break;
    case FROTHY_IR_NODE_FN:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &c));
      if (b < a || c >= node_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      break;
    case FROTHY_IR_NODE_CALL:
      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &flag));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &c));
      if (flag > FROTHY_IR_BUILTIN_NEQ || b + c > link_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      if (flag == FROTHY_IR_BUILTIN_NONE) {
        FROTH_TRY(frothy_snapshot_validate_node_id(a, node_count, false));
      } else {
        FROTH_TRY(frothy_snapshot_validate_node_id(a, node_count, true));
      }
      break;
    case FROTHY_IR_NODE_IF:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &c));
      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &flag));
      FROTH_TRY(frothy_snapshot_validate_node_id(a, node_count, false));
      FROTH_TRY(frothy_snapshot_validate_node_id(b, node_count, false));
      FROTH_TRY(frothy_snapshot_validate_node_id(
          c, node_count, flag == 0u));
      break;
    case FROTHY_IR_NODE_WHILE:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      FROTH_TRY(frothy_snapshot_validate_node_id(a, node_count, false));
      FROTH_TRY(frothy_snapshot_validate_node_id(b, node_count, false));
      break;
    case FROTHY_IR_NODE_SEQ:
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &a));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &b));
      if (a + b > link_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      break;
    default:
      return FROTH_ERROR_SNAPSHOT_FORMAT;
    }
  }

  for (i = 0; i < link_count; i++) {
    FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &arity));
    if (arity >= node_count) {
      return FROTH_ERROR_SNAPSHOT_FORMAT;
    }
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_validate(
    const uint8_t *payload, size_t payload_length,
    frothy_snapshot_layout_t *layout_out) {
  frothy_snapshot_reader_t reader = {
      .data = payload, .length = payload_length, .offset = 0};
  uint16_t snapshot_version = 0;
  uint16_t ir_version = 0;
  uint32_t count = 0;
  uint32_t i;
  const uint8_t *magic = NULL;
  const uint8_t *ignored = NULL;
  uint8_t *object_kinds = layout_out->object_kinds;

  memset(layout_out, 0, sizeof(*layout_out));
  layout_out->object_kinds = object_kinds;
  if (layout_out->object_kinds == NULL) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_TRY(frothy_snapshot_reader_read_bytes(&reader, 4, &magic));
  if (memcmp(magic, FROTHY_SNAPSHOT_MAGIC, 4) != 0) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
  FROTH_TRY(frothy_snapshot_reader_read_u16(&reader, &snapshot_version));
  FROTH_TRY(frothy_snapshot_reader_read_u16(&reader, &ir_version));
  if (snapshot_version != FROTHY_SNAPSHOT_VERSION ||
      ir_version != FROTHY_SNAPSHOT_IR_VERSION) {
    return FROTH_ERROR_SNAPSHOT_INCOMPAT;
  }

  FROTH_TRY(frothy_snapshot_reader_read_u32(&reader, &layout_out->symbol_count));
  if (layout_out->symbol_count > FROTH_SLOT_TABLE_SIZE) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }
  for (i = 0; i < layout_out->symbol_count; i++) {
    uint16_t length = 0;
    const uint8_t *name_bytes = NULL;
    size_t name_index;

    FROTH_TRY(frothy_snapshot_reader_read_u16(&reader, &length));
    if (length == 0) {
      return FROTH_ERROR_SNAPSHOT_BAD_NAME;
    }
    FROTH_TRY(frothy_snapshot_reader_read_bytes(&reader, length, &name_bytes));
    for (name_index = 0; name_index < length; name_index++) {
      if (name_bytes[name_index] == '\0') {
        return FROTH_ERROR_SNAPSHOT_BAD_NAME;
      }
    }
  }

  FROTH_TRY(frothy_snapshot_reader_read_u32(&reader, &layout_out->object_count));
  if (layout_out->object_count > FROTHY_OBJECT_CAPACITY) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }
  if (layout_out->object_count > 0) {
    memset(layout_out->object_kinds, 0,
           layout_out->object_count * sizeof(*layout_out->object_kinds));
  }

  for (i = 0; i < layout_out->object_count; i++) {
    uint8_t kind = 0;

    FROTH_TRY(frothy_snapshot_reader_read_u8(&reader, &kind));
    layout_out->object_kinds[i] = kind;
    switch ((frothy_object_kind_t)kind) {
    case FROTHY_OBJECT_TEXT:
      FROTH_TRY(frothy_snapshot_reader_read_u32(&reader, &count));
      FROTH_TRY(frothy_snapshot_reader_read_bytes(&reader, count, &ignored));
      break;
    case FROTHY_OBJECT_CELLS:
      FROTH_TRY(frothy_snapshot_reader_read_u32(&reader, &count));
      if (count == 0) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      while (count-- > 0) {
        FROTH_TRY(frothy_snapshot_validate_value(&reader, layout_out->object_kinds,
                                                 layout_out->object_count, true));
      }
      break;
    case FROTHY_OBJECT_CODE:
      FROTH_TRY(
          frothy_snapshot_validate_code_object(&reader, layout_out->symbol_count));
      break;
    case FROTHY_OBJECT_NATIVE:
    case FROTHY_OBJECT_FREE:
    default:
      return FROTH_ERROR_NOT_PERSISTABLE;
    }
  }

  FROTH_TRY(frothy_snapshot_reader_read_u32(&reader, &layout_out->binding_count));
  for (i = 0; i < layout_out->binding_count; i++) {
    FROTH_TRY(frothy_snapshot_reader_read_u32(&reader, &count));
    if (count >= layout_out->symbol_count) {
      return FROTH_ERROR_SNAPSHOT_FORMAT;
    }
    FROTH_TRY(frothy_snapshot_validate_value(&reader, layout_out->object_kinds,
                                             layout_out->object_count, false));
  }

  if (reader.offset != reader.length) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  return FROTH_OK;
}

static void frothy_snapshot_layout_init(
    frothy_snapshot_layout_t *layout,
    frothy_snapshot_codec_workspace_t *workspace) {
  memset(layout, 0, sizeof(*layout));
  layout->object_kinds = workspace->layout_object_kinds;
}

static void frothy_snapshot_layout_free(frothy_snapshot_layout_t *layout) {
  memset(layout, 0, sizeof(*layout));
}

static void frothy_snapshot_release_anchors(frothy_runtime_t *runtime,
                                            frothy_value_t *objects,
                                            uint32_t object_count) {
  uint32_t i;

  if (objects == NULL) {
    return;
  }

  for (i = 0; i < object_count; i++) {
    (void)frothy_value_release(runtime, objects[i]);
  }
}

static froth_error_t frothy_snapshot_decode_symbols(
    frothy_snapshot_reader_t *reader, uint32_t symbol_count,
    frothy_snapshot_symbol_t *symbols_out) {
  uint32_t i;

  for (i = 0; i < symbol_count; i++) {
    uint16_t length = 0;
    const uint8_t *bytes = NULL;

    FROTH_TRY(frothy_snapshot_reader_read_u16(reader, &length));
    FROTH_TRY(frothy_snapshot_reader_read_bytes(reader, length, &bytes));
    symbols_out[i].name = (const char *)bytes;
    symbols_out[i].length = length;
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_decode_value(
    frothy_snapshot_reader_t *reader, frothy_value_t *objects,
    uint32_t object_count, bool retain_object, frothy_value_t *out) {
  uint8_t tag = 0;
  uint32_t object_index = 0;
  int32_t int_value = 0;

  FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &tag));
  switch ((frothy_snapshot_value_tag_t)tag) {
  case FROTHY_SNAPSHOT_VALUE_NIL:
    *out = frothy_value_make_nil();
    return FROTH_OK;
  case FROTHY_SNAPSHOT_VALUE_FALSE:
    *out = frothy_value_make_bool(false);
    return FROTH_OK;
  case FROTHY_SNAPSHOT_VALUE_TRUE:
    *out = frothy_value_make_bool(true);
    return FROTH_OK;
  case FROTHY_SNAPSHOT_VALUE_INT:
    FROTH_TRY(frothy_snapshot_reader_read_i32(reader, &int_value));
    return frothy_value_make_int(int_value, out);
  case FROTHY_SNAPSHOT_VALUE_OBJECT:
    FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &object_index));
    if (object_index >= object_count) {
      return FROTH_ERROR_SNAPSHOT_FORMAT;
    }
    *out = objects[object_index];
    if (retain_object) {
      return frothy_value_retain(frothy_runtime(), *out);
    }
    return FROTH_OK;
  }

  return FROTH_ERROR_SNAPSHOT_FORMAT;
}

static froth_error_t frothy_snapshot_decode_literal(
    frothy_snapshot_reader_t *reader, frothy_ir_literal_t *literal_out) {
  uint8_t kind = 0;
  uint32_t length = 0;
  const uint8_t *bytes = NULL;

  memset(literal_out, 0, sizeof(*literal_out));
  FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &kind));
  literal_out->kind = (frothy_ir_literal_kind_t)kind;
  switch (literal_out->kind) {
  case FROTHY_IR_LITERAL_INT:
      {
        int32_t value = 0;
        FROTH_TRY(frothy_snapshot_reader_read_i32(reader, &value));
        literal_out->as.int_value = (froth_cell_t)value;
        return FROTH_OK;
      }
  case FROTHY_IR_LITERAL_BOOL:
      {
        uint8_t value = 0;
        FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &value));
        literal_out->as.bool_value = value != 0;
        return FROTH_OK;
      }
  case FROTHY_IR_LITERAL_NIL:
    return FROTH_OK;
  case FROTHY_IR_LITERAL_TEXT:
    FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &length));
    FROTH_TRY(frothy_snapshot_reader_read_bytes(reader, length, &bytes));
    return frothy_snapshot_strdup((const char *)bytes, length,
                                  &literal_out->as.text_value);
  }

  return FROTH_ERROR_SNAPSHOT_FORMAT;
}

static froth_error_t frothy_snapshot_dup_symbol(
    const frothy_snapshot_symbol_t *symbol, char **out) {
  return frothy_snapshot_strdup(symbol->name, symbol->length, out);
}

static froth_error_t frothy_snapshot_add_size(size_t lhs, size_t rhs,
                                              size_t *out) {
  if (lhs > SIZE_MAX - rhs) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  *out = lhs + rhs;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_copy_string(const uint8_t *bytes,
                                                 size_t length,
                                                 uint8_t **cursor,
                                                 const uint8_t *end,
                                                 char **out) {
  if ((size_t)(end - *cursor) < length + 1) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  memcpy(*cursor, bytes, length);
  (*cursor)[length] = '\0';
  *out = (char *)*cursor;
  *cursor += length + 1;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_copy_symbol_string(
    const frothy_snapshot_symbol_t *symbol, uint8_t **cursor,
    const uint8_t *end, char **out) {
  return frothy_snapshot_copy_string((const uint8_t *)symbol->name,
                                     symbol->length, cursor, end, out);
}

static froth_error_t frothy_snapshot_measure_code_object(
    frothy_snapshot_reader_t *reader, const frothy_snapshot_symbol_t *symbols,
    uint32_t symbol_count, uint32_t literal_count, uint32_t node_count,
    uint32_t link_count, size_t *string_bytes_out) {
  size_t string_bytes = 0;
  uint32_t i;

  for (i = 0; i < literal_count; i++) {
    uint8_t kind = 0;

    FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &kind));
    switch ((frothy_ir_literal_kind_t)kind) {
    case FROTHY_IR_LITERAL_INT: {
      int32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_i32(reader, &ignored));
      break;
    }
    case FROTHY_IR_LITERAL_BOOL: {
      uint8_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &ignored));
      break;
    }
    case FROTHY_IR_LITERAL_NIL:
      break;
    case FROTHY_IR_LITERAL_TEXT: {
      uint32_t length = 0;
      const uint8_t *bytes = NULL;

      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &length));
      FROTH_TRY(frothy_snapshot_reader_read_bytes(reader, length, &bytes));
      (void)bytes;
      FROTH_TRY(
          frothy_snapshot_add_size(string_bytes, (size_t)length + 1,
                                   &string_bytes));
      break;
    }
    }
  }

  for (i = 0; i < node_count; i++) {
    uint8_t kind = 0;

    FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &kind));
    switch ((frothy_ir_node_kind_t)kind) {
    case FROTHY_IR_NODE_LIT: {
      uint32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      break;
    }
    case FROTHY_IR_NODE_READ_LOCAL: {
      uint32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      break;
    }
    case FROTHY_IR_NODE_WRITE_LOCAL: {
      uint32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      break;
    }
    case FROTHY_IR_NODE_READ_SLOT: {
      uint32_t symbol_index = 0;

      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &symbol_index));
      if (symbol_index >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      FROTH_TRY(frothy_snapshot_add_size(string_bytes,
                                         symbols[symbol_index].length + 1,
                                         &string_bytes));
      break;
    }
    case FROTHY_IR_NODE_READ_SLOT_FALLBACK: {
      uint32_t primary_symbol_index = 0;
      uint32_t fallback_symbol_index = 0;

      FROTH_TRY(
          frothy_snapshot_reader_read_u32(reader, &primary_symbol_index));
      FROTH_TRY(
          frothy_snapshot_reader_read_u32(reader, &fallback_symbol_index));
      if (primary_symbol_index >= symbol_count ||
          fallback_symbol_index >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      FROTH_TRY(frothy_snapshot_add_size(
          string_bytes, symbols[primary_symbol_index].length + 1,
          &string_bytes));
      FROTH_TRY(frothy_snapshot_add_size(
          string_bytes, symbols[fallback_symbol_index].length + 1,
          &string_bytes));
      break;
    }
    case FROTHY_IR_NODE_WRITE_SLOT: {
      uint32_t symbol_index = 0;
      uint32_t ignored = 0;
      uint8_t require_existing = 0;

      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &symbol_index));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &require_existing));
      (void)require_existing;
      if (symbol_index >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      FROTH_TRY(frothy_snapshot_add_size(string_bytes,
                                         symbols[symbol_index].length + 1,
                                         &string_bytes));
      break;
    }
    case FROTHY_IR_NODE_WRITE_SLOT_FALLBACK: {
      uint32_t primary_symbol_index = 0;
      uint32_t fallback_symbol_index = 0;
      uint32_t ignored = 0;
      uint8_t require_existing = 0;

      FROTH_TRY(
          frothy_snapshot_reader_read_u32(reader, &primary_symbol_index));
      FROTH_TRY(
          frothy_snapshot_reader_read_u32(reader, &fallback_symbol_index));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &require_existing));
      (void)require_existing;
      if (primary_symbol_index >= symbol_count ||
          fallback_symbol_index >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      FROTH_TRY(frothy_snapshot_add_size(
          string_bytes, symbols[primary_symbol_index].length + 1,
          &string_bytes));
      FROTH_TRY(frothy_snapshot_add_size(
          string_bytes, symbols[fallback_symbol_index].length + 1,
          &string_bytes));
      break;
    }
    case FROTHY_IR_NODE_SLOT_DESIGNATOR: {
      uint32_t symbol_index = 0;

      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &symbol_index));
      if (symbol_index >= symbol_count) {
        return FROTH_ERROR_SNAPSHOT_FORMAT;
      }
      FROTH_TRY(frothy_snapshot_add_size(string_bytes,
                                         symbols[symbol_index].length + 1,
                                         &string_bytes));
      break;
    }
    case FROTHY_IR_NODE_READ_INDEX: {
      uint32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      break;
    }
    case FROTHY_IR_NODE_WRITE_INDEX: {
      uint32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      break;
    }
    case FROTHY_IR_NODE_FN: {
      uint32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      break;
    }
    case FROTHY_IR_NODE_CALL: {
      uint8_t builtin = 0;
      uint32_t ignored = 0;

      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &builtin));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      (void)builtin;
      break;
    }
    case FROTHY_IR_NODE_IF: {
      uint32_t ignored = 0;
      uint8_t has_else = 0;

      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u8(reader, &has_else));
      (void)has_else;
      break;
    }
    case FROTHY_IR_NODE_WHILE: {
      uint32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      break;
    }
    case FROTHY_IR_NODE_SEQ: {
      uint32_t ignored = 0;
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
      break;
    }
    }
  }

  for (i = 0; i < link_count; i++) {
    uint32_t ignored = 0;
    FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &ignored));
  }

  *string_bytes_out = string_bytes;
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_decode_code_object(
    frothy_snapshot_reader_t *reader, const frothy_snapshot_symbol_t *symbols,
    uint32_t symbol_count, frothy_value_t *out) {
  frothy_ir_program_t program;
  frothy_payload_span_t payload = {0};
  uint8_t *string_cursor = NULL;
  uint8_t *storage_end = NULL;
  void *storage = NULL;
  frothy_snapshot_reader_t measure;
  uint32_t arity = 0;
  uint32_t local_count = 0;
  uint32_t body = 0;
  uint32_t root = 0;
  uint32_t root_local_count = 0;
  uint32_t literal_count = 0;
  uint32_t node_count = 0;
  uint32_t link_count = 0;
  size_t string_bytes = 0;
  size_t packed_size = 0;
  uint32_t i;
  froth_error_t err = FROTH_OK;

  frothy_ir_program_init(&program);
  err = frothy_snapshot_reader_read_u32(reader, &arity);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_snapshot_reader_read_u32(reader, &local_count);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_snapshot_reader_read_u32(reader, &body);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_snapshot_reader_read_u32(reader, &root);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_snapshot_reader_read_u32(reader, &root_local_count);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_snapshot_reader_read_u32(reader, &literal_count);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_snapshot_reader_read_u32(reader, &node_count);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_snapshot_reader_read_u32(reader, &link_count);
  if (err != FROTH_OK) {
    goto fail;
  }

  if (literal_count > FROTHY_IR_LITERAL_CAPACITY ||
      node_count > FROTHY_IR_NODE_CAPACITY ||
      link_count > FROTHY_IR_LINK_CAPACITY) {
    err = FROTH_ERROR_SNAPSHOT_OVERFLOW;
    goto fail;
  }

  measure = *reader;
  err = frothy_snapshot_measure_code_object(&measure, symbols, symbol_count,
                                            literal_count, node_count,
                                            link_count, &string_bytes);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_ir_program_packed_size(literal_count, node_count, link_count,
                                      string_bytes, &packed_size);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_runtime_alloc_payload(frothy_runtime(), packed_size, &payload,
                                     &storage);
  if (err != FROTH_OK) {
    goto fail;
  }
  err = frothy_ir_program_init_packed_view(&program, storage, payload.length,
                                           literal_count, node_count,
                                           link_count, &string_cursor,
                                           &storage_end);
  if (err != FROTH_OK) {
    goto fail;
  }

  program.root = root;
  program.root_local_count = root_local_count;

  for (i = 0; i < literal_count; i++) {
    frothy_ir_literal_t *literal = &program.literals[i];
    uint8_t kind = 0;

    err = frothy_snapshot_reader_read_u8(reader, &kind);
    if (err != FROTH_OK) {
      goto fail;
    }
    literal->kind = (frothy_ir_literal_kind_t)kind;
    switch (literal->kind) {
    case FROTHY_IR_LITERAL_INT: {
      int32_t value = 0;
      err = frothy_snapshot_reader_read_i32(reader, &value);
      if (err == FROTH_OK) {
        literal->as.int_value = (froth_cell_t)value;
      }
      break;
    }
    case FROTHY_IR_LITERAL_BOOL: {
      uint8_t value = 0;
      err = frothy_snapshot_reader_read_u8(reader, &value);
      if (err == FROTH_OK) {
        literal->as.bool_value = value != 0;
      }
      break;
    }
    case FROTHY_IR_LITERAL_NIL:
      err = FROTH_OK;
      break;
    case FROTHY_IR_LITERAL_TEXT: {
      uint32_t length = 0;
      const uint8_t *bytes = NULL;

      err = frothy_snapshot_reader_read_u32(reader, &length);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_bytes(reader, length, &bytes);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_copy_string(bytes, length, &string_cursor,
                                        storage_end,
                                        &literal->as.text_value);
      break;
    }
    }
    if (err != FROTH_OK) {
      goto fail;
    }
  }

  for (i = 0; i < node_count; i++) {
    frothy_ir_node_t *node = &program.nodes[i];
    uint8_t kind = 0;

    err = frothy_snapshot_reader_read_u8(reader, &kind);
    if (err != FROTH_OK) {
      goto fail;
    }
    node->kind = (frothy_ir_node_kind_t)kind;
    switch (node->kind) {
    case FROTHY_IR_NODE_LIT:
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.lit.literal_id);
      break;
    case FROTHY_IR_NODE_READ_LOCAL:
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.read_local.local_index);
      break;
    case FROTHY_IR_NODE_WRITE_LOCAL:
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.write_local.local_index);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.write_local.value);
      break;
    case FROTHY_IR_NODE_READ_SLOT: {
      uint32_t symbol_index = 0;

      err = frothy_snapshot_reader_read_u32(reader, &symbol_index);
      if (err != FROTH_OK) {
        break;
      }
      if (symbol_index >= symbol_count) {
        err = FROTH_ERROR_SNAPSHOT_FORMAT;
        break;
      }
      err = frothy_snapshot_copy_symbol_string(
          &symbols[symbol_index], &string_cursor, storage_end,
          &node->as.read_slot.slot_name);
      break;
    }
    case FROTHY_IR_NODE_READ_SLOT_FALLBACK: {
      uint32_t primary_symbol_index = 0;
      uint32_t fallback_symbol_index = 0;

      err = frothy_snapshot_reader_read_u32(reader, &primary_symbol_index);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(reader, &fallback_symbol_index);
      if (err != FROTH_OK) {
        break;
      }
      if (primary_symbol_index >= symbol_count ||
          fallback_symbol_index >= symbol_count) {
        err = FROTH_ERROR_SNAPSHOT_FORMAT;
        break;
      }
      err = frothy_snapshot_copy_symbol_string(
          &symbols[primary_symbol_index], &string_cursor, storage_end,
          &node->as.read_slot_fallback.primary_slot_name);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_copy_symbol_string(
          &symbols[fallback_symbol_index], &string_cursor, storage_end,
          &node->as.read_slot_fallback.fallback_slot_name);
      break;
    }
    case FROTHY_IR_NODE_WRITE_SLOT: {
      uint32_t symbol_index = 0;
      uint8_t require_existing = 0;

      err = frothy_snapshot_reader_read_u32(reader, &symbol_index);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.write_slot.value);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u8(reader, &require_existing);
      if (err != FROTH_OK) {
        break;
      }
      if (symbol_index >= symbol_count) {
        err = FROTH_ERROR_SNAPSHOT_FORMAT;
        break;
      }
      err = frothy_snapshot_copy_symbol_string(
          &symbols[symbol_index], &string_cursor, storage_end,
          &node->as.write_slot.slot_name);
      if (err == FROTH_OK) {
        node->as.write_slot.require_existing = require_existing != 0;
      }
      break;
    }
    case FROTHY_IR_NODE_WRITE_SLOT_FALLBACK: {
      uint32_t primary_symbol_index = 0;
      uint32_t fallback_symbol_index = 0;
      uint8_t require_existing = 0;

      err = frothy_snapshot_reader_read_u32(reader, &primary_symbol_index);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(reader, &fallback_symbol_index);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.write_slot_fallback.value);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u8(reader, &require_existing);
      if (err != FROTH_OK) {
        break;
      }
      if (primary_symbol_index >= symbol_count ||
          fallback_symbol_index >= symbol_count) {
        err = FROTH_ERROR_SNAPSHOT_FORMAT;
        break;
      }
      err = frothy_snapshot_copy_symbol_string(
          &symbols[primary_symbol_index], &string_cursor, storage_end,
          &node->as.write_slot_fallback.primary_slot_name);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_copy_symbol_string(
          &symbols[fallback_symbol_index], &string_cursor, storage_end,
          &node->as.write_slot_fallback.fallback_slot_name);
      if (err == FROTH_OK) {
        node->as.write_slot_fallback.require_existing = require_existing != 0;
      }
      break;
    }
    case FROTHY_IR_NODE_SLOT_DESIGNATOR: {
      uint32_t symbol_index = 0;

      err = frothy_snapshot_reader_read_u32(reader, &symbol_index);
      if (err != FROTH_OK) {
        break;
      }
      if (symbol_index >= symbol_count) {
        err = FROTH_ERROR_SNAPSHOT_FORMAT;
        break;
      }
      err = frothy_snapshot_copy_symbol_string(
          &symbols[symbol_index], &string_cursor, storage_end,
          &node->as.slot_designator.slot_name);
      break;
    }
    case FROTHY_IR_NODE_READ_INDEX:
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.read_index.base);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.read_index.index);
      break;
    case FROTHY_IR_NODE_WRITE_INDEX:
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.write_index.base);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.write_index.index);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.write_index.value);
      break;
    case FROTHY_IR_NODE_FN:
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.fn.arity);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.fn.local_count);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.fn.body);
      break;
    case FROTHY_IR_NODE_CALL: {
      uint8_t builtin = 0;
      uint32_t callee = 0;

      err = frothy_snapshot_reader_read_u8(reader, &builtin);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(reader, &callee);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.call.first_arg);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.call.arg_count);
      if (err == FROTH_OK) {
        node->as.call.builtin = (frothy_ir_builtin_kind_t)builtin;
        node->as.call.callee =
            callee == FROTHY_SNAPSHOT_INVALID_INDEX ? FROTHY_IR_NODE_INVALID
                                                    : (frothy_ir_node_id_t)callee;
      }
      break;
    }
    case FROTHY_IR_NODE_IF: {
      uint32_t else_branch = 0;
      uint8_t has_else = 0;

      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.if_expr.condition);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.if_expr.then_branch);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(reader, &else_branch);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u8(reader, &has_else);
      if (err == FROTH_OK) {
        node->as.if_expr.has_else_branch = has_else != 0;
        node->as.if_expr.else_branch =
            else_branch == FROTHY_SNAPSHOT_INVALID_INDEX
                ? FROTHY_IR_NODE_INVALID
                : (frothy_ir_node_id_t)else_branch;
      }
      break;
    }
    case FROTHY_IR_NODE_WHILE:
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.while_expr.condition);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.while_expr.body);
      break;
    case FROTHY_IR_NODE_SEQ:
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.seq.first_item);
      if (err != FROTH_OK) {
        break;
      }
      err = frothy_snapshot_reader_read_u32(
          reader, (uint32_t *)&node->as.seq.item_count);
      break;
    }
    if (err != FROTH_OK) {
      goto fail;
    }
  }

  for (i = 0; i < link_count; i++) {
    err = frothy_snapshot_reader_read_u32(reader, (uint32_t *)&program.links[i]);
    if (err != FROTH_OK) {
      goto fail;
    }
  }

  err = frothy_runtime_alloc_packed_code(frothy_runtime(), &program, body,
                                         arity, local_count, out);
  if (err == FROTH_OK) {
    return FROTH_OK;
  }

fail:
  if (payload.length > 0) {
    frothy_runtime_release_payload(frothy_runtime(), payload);
  }
  frothy_ir_program_free(&program);
  return err;
}

static froth_error_t frothy_snapshot_bind_slot_value(const char *name,
                                                     size_t length,
                                                     frothy_value_t value) {
  froth_cell_u_t slot_index;
  froth_cell_t old_impl = 0;
  uint8_t old_in_arity = FROTH_SLOT_ARITY_UNKNOWN;
  uint8_t old_out_arity = FROTH_SLOT_ARITY_UNKNOWN;
  froth_error_t err;
  size_t arity = 0;
  bool had_old_impl = false;

  FROTH_TRY(froth_slot_find_name_or_create_n(&froth_vm.heap, name, length,
                                             &slot_index));
  FROTH_TRY(froth_slot_get_arity(slot_index, &old_in_arity, &old_out_arity));
  err = froth_slot_get_impl(slot_index, &old_impl);
  if (err != FROTH_OK && err != FROTH_ERROR_UNDEFINED_WORD) {
    return err;
  }
  had_old_impl = err == FROTH_OK;
  FROTH_TRY(froth_slot_set_overlay(slot_index, 1));
  FROTH_TRY(froth_slot_set_impl(slot_index, frothy_value_to_cell(value)));
  if (frothy_runtime_get_code(frothy_runtime(), value, NULL, NULL, &arity,
                              NULL) == FROTH_OK &&
      arity < FROTH_SLOT_ARITY_UNKNOWN) {
    err = froth_slot_set_arity(slot_index, (uint8_t)arity, 1);
  } else if (frothy_runtime_get_native(frothy_runtime(), value, NULL, NULL,
                                       NULL, &arity) == FROTH_OK &&
             arity < FROTH_SLOT_ARITY_UNKNOWN) {
    err = froth_slot_set_arity(slot_index, (uint8_t)arity, 1);
  } else {
    err = froth_slot_clear_arity(slot_index);
  }
  if (err != FROTH_OK) {
    if (had_old_impl) {
      (void)froth_slot_set_impl(slot_index, old_impl);
      if (old_in_arity < FROTH_SLOT_ARITY_UNKNOWN &&
          old_out_arity < FROTH_SLOT_ARITY_UNKNOWN) {
        (void)froth_slot_set_arity(slot_index, old_in_arity, old_out_arity);
      } else {
        (void)froth_slot_clear_arity(slot_index);
      }
    } else {
      (void)froth_slot_clear_binding(slot_index);
    }
    return err;
  }
  if (had_old_impl) {
    FROTH_TRY(
        frothy_value_release(frothy_runtime(), frothy_value_from_cell(old_impl)));
  }
  return FROTH_OK;
}

static froth_error_t frothy_snapshot_decode_objects(
    frothy_snapshot_reader_t *reader, const frothy_snapshot_symbol_t *symbols,
    uint32_t symbol_count, uint32_t object_count, frothy_value_t *objects_out) {
  uint32_t i;
  size_t j;

  for (i = 0; i < object_count; i++) {
    objects_out[i] = frothy_value_make_nil();
  }

  for (i = 0; i < object_count; i++) {
    uint8_t kind = 0;
    froth_error_t err;

    err = frothy_snapshot_reader_read_u8(reader, &kind);
    if (err != FROTH_OK) {
      return err;
    }
    switch ((frothy_object_kind_t)kind) {
    case FROTHY_OBJECT_TEXT: {
      uint32_t length = 0;
      const uint8_t *bytes = NULL;

      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &length));
      FROTH_TRY(frothy_snapshot_reader_read_bytes(reader, length, &bytes));
      FROTH_TRY(frothy_runtime_alloc_text(frothy_runtime(), (const char *)bytes,
                                          length, &objects_out[i]));
      break;
    }
    case FROTHY_OBJECT_CELLS: {
      uint32_t length = 0;
      froth_cell_t base = 0;

      FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &length));
      FROTH_TRY(
          frothy_runtime_alloc_cells(frothy_runtime(), length, &objects_out[i]));
      FROTH_TRY(
          frothy_runtime_get_cells(frothy_runtime(), objects_out[i], NULL, &base));
      for (j = 0; j < length; j++) {
        frothy_value_t item = frothy_value_make_nil();

        err = frothy_snapshot_decode_value(reader, objects_out, object_count,
                                           true, &item);
        if (err != FROTH_OK) {
          return err;
        }
        froth_vm.cellspace.data[base + (froth_cell_t)j] = frothy_value_to_cell(item);
      }
      break;
    }
    case FROTHY_OBJECT_CODE:
      FROTH_TRY(frothy_snapshot_decode_code_object(
          reader, symbols, symbol_count, &objects_out[i]));
      break;
    case FROTHY_OBJECT_NATIVE:
    case FROTHY_OBJECT_FREE:
    default:
      return FROTH_ERROR_NOT_PERSISTABLE;
    }
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_decode_bindings(
    frothy_snapshot_reader_t *reader,
    const frothy_snapshot_symbol_t *symbols,
    uint32_t symbol_count, frothy_value_t *objects, uint32_t object_count,
    uint32_t binding_count) {
  uint32_t i;

  for (i = 0; i < binding_count; i++) {
    uint32_t symbol_index = 0;
    frothy_value_t value = frothy_value_make_nil();
    froth_error_t err;

    FROTH_TRY(frothy_snapshot_reader_read_u32(reader, &symbol_index));
    if (symbol_index >= symbol_count) {
      return FROTH_ERROR_SNAPSHOT_FORMAT;
    }
    FROTH_TRY(frothy_snapshot_decode_value(reader, objects, object_count, true,
                                           &value));
    err = frothy_snapshot_bind_slot_value(symbols[symbol_index].name,
                                          symbols[symbol_index].length, value);
    if (err != FROTH_OK) {
      (void)frothy_value_release(frothy_runtime(), value);
      return err;
    }
  }

  return FROTH_OK;
}

static froth_error_t frothy_snapshot_decode_payload(
    const uint8_t *payload, uint32_t payload_length,
    const frothy_snapshot_layout_t *layout,
    frothy_snapshot_codec_workspace_t *workspace) {
  frothy_snapshot_reader_t reader;
  const uint8_t *magic = NULL;
  uint16_t snapshot_version = 0;
  uint16_t ir_version = 0;
  uint32_t section_count = 0;
  froth_error_t err;

  reader.data = payload;
  reader.length = payload_length;
  reader.offset = 0;

  err = frothy_snapshot_reader_read_bytes(&reader, 4, &magic);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_snapshot_reader_read_u16(&reader, &snapshot_version);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_snapshot_reader_read_u16(&reader, &ir_version);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_snapshot_reader_read_u32(&reader, &section_count);
  if (err != FROTH_OK) {
    return err;
  }
  if (section_count != layout->symbol_count) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  err = frothy_snapshot_decode_symbols(&reader, layout->symbol_count,
                                       workspace->decoded_symbols);
  if (err != FROTH_OK) {
    return err;
  }
  err = frothy_snapshot_reader_read_u32(&reader, &section_count);
  if (err != FROTH_OK) {
    return err;
  }
  if (section_count != layout->object_count) {
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }
  err = frothy_snapshot_decode_objects(&reader, workspace->decoded_symbols,
                                       layout->symbol_count,
                                       layout->object_count,
                                       workspace->decoded_objects);
  if (err != FROTH_OK) {
    goto fail;
  }
  if (frothy_snapshot_test_error_after_objects != FROTH_OK) {
    err = frothy_snapshot_test_error_after_objects;
    frothy_snapshot_test_error_after_objects = FROTH_OK;
    goto fail;
  }
  err = frothy_snapshot_reader_read_u32(&reader, &section_count);
  if (err != FROTH_OK) {
    goto fail;
  }
  if (section_count != layout->binding_count) {
    err = FROTH_ERROR_SNAPSHOT_FORMAT;
    goto fail;
  }
  err = frothy_snapshot_decode_bindings(&reader, workspace->decoded_symbols,
                                        layout->symbol_count,
                                        workspace->decoded_objects,
                                        layout->object_count,
                                        layout->binding_count);
  if (err != FROTH_OK) {
    goto fail;
  }

  (void)magic;
  (void)snapshot_version;
  (void)ir_version;
  frothy_snapshot_release_anchors(frothy_runtime(), workspace->decoded_objects,
                                  layout->object_count);
  return FROTH_OK;

fail:
  frothy_snapshot_release_anchors(frothy_runtime(), workspace->decoded_objects,
                                  layout->object_count);
  return err;
}

froth_error_t frothy_snapshot_codec_write_payload(
    const frothy_runtime_t *runtime, const uint8_t **payload_out,
    uint32_t *payload_length_out) {
  frothy_snapshot_symbol_table_t symbols;
  frothy_snapshot_object_table_t objects;
  frothy_snapshot_writer_t writer;
  frothy_snapshot_codec_workspace_t *workspace = frothy_snapshot_workspace();
  froth_error_t err = FROTH_OK;

  frothy_snapshot_workspace_reset(workspace);
  memset(&symbols, 0, sizeof(symbols));
  memset(&objects, 0, sizeof(objects));
  frothy_snapshot_symbols_init(&symbols, workspace);

  err = frothy_snapshot_objects_init(&objects, runtime->object_count, workspace);
  if (err != FROTH_OK) {
    return err;
  }

  writer.data = workspace->payload;
  writer.length = 0;
  writer.capacity = sizeof(workspace->payload);

  err = frothy_snapshot_collect_overlay(runtime, &symbols, &objects);
  if (err == FROTH_OK) {
    err = frothy_snapshot_write_payload(&writer, runtime, &symbols, &objects);
  }

  frothy_snapshot_symbols_free(&symbols);
  frothy_snapshot_objects_free(&objects);
  if (err != FROTH_OK) {
    return err;
  }

  *payload_out = workspace->payload;
  *payload_length_out = (uint32_t)writer.length;
  return FROTH_OK;
}

froth_error_t frothy_snapshot_codec_validate_payload(const uint8_t *payload,
                                                     size_t payload_length) {
  frothy_snapshot_layout_t layout;
  frothy_snapshot_codec_workspace_t *workspace = frothy_snapshot_workspace();
  froth_error_t err;

  frothy_snapshot_workspace_reset(workspace);
  frothy_snapshot_layout_init(&layout, workspace);
  err = frothy_snapshot_validate(payload, payload_length, &layout);
  frothy_snapshot_layout_free(&layout);
  return err;
}

froth_error_t frothy_snapshot_codec_restore_payload(const uint8_t *payload,
                                                    uint32_t payload_length) {
  frothy_snapshot_layout_t layout;
  frothy_snapshot_codec_workspace_t *workspace = frothy_snapshot_workspace();
  froth_error_t err;

  frothy_snapshot_workspace_reset(workspace);
  frothy_snapshot_layout_init(&layout, workspace);
  err = frothy_snapshot_validate(payload, payload_length, &layout);
  if (err != FROTH_OK) {
    frothy_snapshot_layout_free(&layout);
    return err;
  }

  err = frothy_snapshot_decode_payload(payload, payload_length, &layout,
                                       workspace);
  frothy_snapshot_layout_free(&layout);
  return err;
}
