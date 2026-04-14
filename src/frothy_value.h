#pragma once

#include "froth_cellspace.h"
#include "frothy_ir.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if FROTH_CELL_SIZE_BITS != 32
#error "Frothy currently requires 32-bit cells."
#endif

#ifndef FROTHY_EVAL_VALUE_CAPACITY
#define FROTHY_EVAL_VALUE_CAPACITY 256
#endif

#ifndef FROTHY_OBJECT_CAPACITY
#define FROTHY_OBJECT_CAPACITY 128
#endif

#ifndef FROTHY_PAYLOAD_CAPACITY
#define FROTHY_PAYLOAD_CAPACITY 16384
#endif

typedef uint32_t frothy_value_t;

typedef struct frothy_runtime_t frothy_runtime_t;

typedef froth_error_t (*frothy_native_fn_t)(frothy_runtime_t *runtime,
                                            const void *context,
                                            const frothy_value_t *args,
                                            size_t arg_count,
                                            frothy_value_t *out);

typedef enum {
  FROTHY_VALUE_CLASS_INT = 0,
  FROTHY_VALUE_CLASS_BOOL,
  FROTHY_VALUE_CLASS_NIL,
  FROTHY_VALUE_CLASS_TEXT,
  FROTHY_VALUE_CLASS_CELLS,
  FROTHY_VALUE_CLASS_CODE,
  FROTHY_VALUE_CLASS_NATIVE,
  FROTHY_VALUE_CLASS_RECORD_DEF,
  FROTHY_VALUE_CLASS_RECORD,
} frothy_value_class_t;

typedef enum {
  FROTHY_OBJECT_FREE = 0,
  FROTHY_OBJECT_TEXT = 1,
  FROTHY_OBJECT_CELLS = 2,
  FROTHY_OBJECT_CODE = 3,
  FROTHY_OBJECT_NATIVE = 4,
  FROTHY_OBJECT_RECORD_DEF = 5,
  FROTHY_OBJECT_RECORD = 6,
} frothy_object_kind_t;

typedef struct {
  froth_cell_t base;
  froth_cell_t length;
} frothy_cells_span_t;

typedef struct {
  size_t offset;
  size_t length;
} frothy_payload_span_t;

typedef struct {
  frothy_object_kind_t kind;
  uint32_t refcount;
  bool live;
  union {
    struct {
      frothy_payload_span_t payload;
      size_t length;
    } text;
    struct {
      frothy_cells_span_t span;
    } cells;
    struct {
      frothy_payload_span_t payload;
      size_t arity;
      size_t local_count;
      frothy_ir_node_id_t body;
      frothy_ir_program_t program;
    } code;
    struct {
      frothy_native_fn_t fn;
      const void *context;
      const char *name;
      size_t arity;
    } native;
    struct {
      frothy_payload_span_t payload;
      size_t field_count;
    } record_def;
    struct {
      frothy_payload_span_t payload;
      frothy_value_t definition;
      size_t field_count;
    } record;
  } as;
} frothy_object_t;

struct frothy_runtime_t {
  frothy_object_t *objects;
  size_t object_count;
  size_t object_capacity;
  size_t live_object_count;
  size_t object_high_water;

  frothy_cells_span_t *free_spans;
  size_t free_span_count;
  size_t free_span_capacity;

  frothy_payload_span_t *payload_free_spans;
  size_t payload_free_span_count;
  size_t payload_free_span_capacity;
  size_t payload_capacity;
  size_t payload_extent;
  size_t payload_bytes_used;
  size_t payload_bytes_high_water;

  frothy_value_t *eval_values;
  size_t eval_value_capacity;
  size_t eval_value_limit;
  size_t eval_value_used;
  size_t eval_value_high_water;

  froth_cellspace_t *cellspace;

  size_t test_object_limit;
  bool test_fail_next_append;
  uint32_t reset_epoch;

  frothy_object_t object_storage[FROTHY_OBJECT_CAPACITY];
  frothy_cells_span_t free_span_storage[FROTHY_OBJECT_CAPACITY];
  frothy_payload_span_t payload_free_span_storage[FROTHY_OBJECT_CAPACITY];
  union {
    max_align_t align;
    uint8_t bytes[FROTHY_PAYLOAD_CAPACITY];
  } payload_storage;
  frothy_value_t eval_value_storage[FROTHY_EVAL_VALUE_CAPACITY];
};

void frothy_runtime_init(frothy_runtime_t *runtime, froth_cellspace_t *cellspace);
void frothy_runtime_free(frothy_runtime_t *runtime);
froth_error_t frothy_runtime_reset_overlay(frothy_runtime_t *runtime);
froth_error_t frothy_runtime_clear_overlay_state(frothy_runtime_t *runtime);
size_t frothy_runtime_live_object_count(const frothy_runtime_t *runtime);
size_t frothy_runtime_object_high_water(const frothy_runtime_t *runtime);
size_t frothy_runtime_eval_value_high_water(const frothy_runtime_t *runtime);
size_t frothy_runtime_payload_used(const frothy_runtime_t *runtime);
size_t frothy_runtime_payload_high_water(const frothy_runtime_t *runtime);
void frothy_runtime_debug_reset_high_water(frothy_runtime_t *runtime);
void frothy_runtime_test_set_object_limit(frothy_runtime_t *runtime, size_t limit);
void frothy_runtime_test_set_eval_value_limit(frothy_runtime_t *runtime,
                                              size_t limit);
void frothy_runtime_test_fail_next_append(frothy_runtime_t *runtime);

froth_error_t frothy_value_make_int(int32_t value, frothy_value_t *out);
frothy_value_t frothy_value_make_bool(bool value);
frothy_value_t frothy_value_make_nil(void);

bool frothy_value_is_int(frothy_value_t value);
bool frothy_value_is_bool(frothy_value_t value);
bool frothy_value_is_nil(frothy_value_t value);
bool frothy_value_is_object_ref(frothy_value_t value);
int32_t frothy_value_as_int(frothy_value_t value);
bool frothy_value_as_bool(frothy_value_t value);
size_t frothy_value_object_index(frothy_value_t value);
froth_cell_t frothy_value_to_cell(frothy_value_t value);
frothy_value_t frothy_value_from_cell(froth_cell_t cell);

froth_error_t frothy_value_class(const frothy_runtime_t *runtime,
                                 frothy_value_t value,
                                 frothy_value_class_t *out);
froth_error_t frothy_value_from_literal(frothy_runtime_t *runtime,
                                        const frothy_ir_literal_t *literal,
                                        frothy_value_t *out);
froth_error_t frothy_value_render(const frothy_runtime_t *runtime,
                                  frothy_value_t value, char **out_text);
froth_error_t frothy_value_equals(const frothy_runtime_t *runtime,
                                  frothy_value_t lhs, frothy_value_t rhs,
                                  bool *equal_out);
froth_error_t frothy_value_retain(frothy_runtime_t *runtime, frothy_value_t value);
froth_error_t frothy_value_release(frothy_runtime_t *runtime, frothy_value_t value);

froth_error_t frothy_runtime_alloc_text(frothy_runtime_t *runtime,
                                        const char *text, size_t length,
                                        frothy_value_t *out);
froth_error_t frothy_runtime_get_text(const frothy_runtime_t *runtime,
                                      frothy_value_t value, const char **text,
                                      size_t *length_out);

froth_error_t frothy_runtime_alloc_cells(frothy_runtime_t *runtime,
                                         size_t length, frothy_value_t *out);
froth_error_t frothy_runtime_get_cells(const frothy_runtime_t *runtime,
                                       frothy_value_t value, size_t *length_out,
                                       froth_cell_t *base_out);

/* Internal persistent-payload helpers for packed text/code ownership. */
froth_error_t frothy_runtime_alloc_payload(frothy_runtime_t *runtime,
                                           size_t length,
                                           frothy_payload_span_t *span_out,
                                           void **data_out);
void frothy_runtime_release_payload(frothy_runtime_t *runtime,
                                    frothy_payload_span_t span);

froth_error_t frothy_runtime_alloc_code(frothy_runtime_t *runtime,
                                        const frothy_ir_program_t *program,
                                        frothy_ir_node_id_t body, size_t arity,
                                        size_t local_count, frothy_value_t *out);
froth_error_t frothy_runtime_alloc_packed_code(frothy_runtime_t *runtime,
                                               const frothy_ir_program_t *program,
                                               frothy_ir_node_id_t body,
                                               size_t arity,
                                               size_t local_count,
                                               frothy_value_t *out);
froth_error_t frothy_runtime_get_code(const frothy_runtime_t *runtime,
                                      frothy_value_t value,
                                      const frothy_ir_program_t **program_out,
                                      frothy_ir_node_id_t *body_out,
                                      size_t *arity_out,
                                      size_t *local_count_out);
froth_error_t frothy_runtime_alloc_native(frothy_runtime_t *runtime,
                                          frothy_native_fn_t fn,
                                          const char *name, size_t arity,
                                          const void *context,
                                          frothy_value_t *out);
froth_error_t frothy_runtime_get_native(const frothy_runtime_t *runtime,
                                        frothy_value_t value,
                                        frothy_native_fn_t *fn_out,
                                        const void **context_out,
                                        const char **name_out,
                                        size_t *arity_out);

froth_error_t frothy_runtime_alloc_record_def(frothy_runtime_t *runtime,
                                              const char *name,
                                              const char *const *field_names,
                                              size_t field_count,
                                              frothy_value_t *out);
froth_error_t frothy_runtime_get_record_def(const frothy_runtime_t *runtime,
                                            frothy_value_t value,
                                            const char **name_out,
                                            size_t *field_count_out);
froth_error_t frothy_runtime_record_def_field_name(
    const frothy_runtime_t *runtime, frothy_value_t value, size_t field_index,
    const char **field_name_out);
froth_error_t frothy_runtime_record_def_field_index(
    const frothy_runtime_t *runtime, frothy_value_t value, const char *field_name,
    size_t *field_index_out);

froth_error_t frothy_runtime_alloc_record(frothy_runtime_t *runtime,
                                          frothy_value_t definition,
                                          const frothy_value_t *fields,
                                          size_t field_count,
                                          frothy_value_t *out);
froth_error_t frothy_runtime_get_record(const frothy_runtime_t *runtime,
                                        frothy_value_t value,
                                        frothy_value_t *definition_out,
                                        size_t *field_count_out,
                                        const frothy_value_t **fields_out);
froth_error_t frothy_runtime_record_read_field(const frothy_runtime_t *runtime,
                                               frothy_value_t value,
                                               const char *field_name,
                                               frothy_value_t *out);
froth_error_t frothy_runtime_record_write_field(frothy_runtime_t *runtime,
                                                frothy_value_t value,
                                                const char *field_name,
                                                frothy_value_t stored_value);
