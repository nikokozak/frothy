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
} frothy_value_class_t;

typedef enum {
  FROTHY_OBJECT_FREE = 0,
  FROTHY_OBJECT_TEXT = 1,
  FROTHY_OBJECT_CELLS = 2,
  FROTHY_OBJECT_CODE = 3,
  FROTHY_OBJECT_NATIVE = 4,
} frothy_object_kind_t;

typedef struct {
  froth_cell_t base;
  froth_cell_t length;
} frothy_cells_span_t;

typedef struct {
  frothy_object_kind_t kind;
  uint32_t refcount;
  bool live;
  union {
    struct {
      char *bytes;
      size_t length;
    } text;
    struct {
      frothy_cells_span_t span;
    } cells;
    struct {
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
  frothy_value_t eval_value_storage[FROTHY_EVAL_VALUE_CAPACITY];
};

void frothy_runtime_init(frothy_runtime_t *runtime, froth_cellspace_t *cellspace);
void frothy_runtime_free(frothy_runtime_t *runtime);
froth_error_t frothy_runtime_reset_overlay(frothy_runtime_t *runtime);
froth_error_t frothy_runtime_clear_overlay_state(frothy_runtime_t *runtime);
size_t frothy_runtime_live_object_count(const frothy_runtime_t *runtime);
size_t frothy_runtime_object_high_water(const frothy_runtime_t *runtime);
size_t frothy_runtime_eval_value_high_water(const frothy_runtime_t *runtime);
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

froth_error_t frothy_runtime_alloc_code(frothy_runtime_t *runtime,
                                        const frothy_ir_program_t *program,
                                        frothy_ir_node_id_t body, size_t arity,
                                        size_t local_count, frothy_value_t *out);
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
