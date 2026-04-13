#pragma once

#include "froth_types.h"
#include <stdbool.h>
#include <stddef.h>

typedef size_t frothy_ir_node_id_t;
typedef size_t frothy_ir_literal_id_t;

#define FROTHY_IR_NODE_INVALID ((frothy_ir_node_id_t)SIZE_MAX)

typedef enum {
  FROTHY_IR_LITERAL_INT = 0,
  FROTHY_IR_LITERAL_BOOL = 1,
  FROTHY_IR_LITERAL_NIL = 2,
  FROTHY_IR_LITERAL_TEXT = 3,
} frothy_ir_literal_kind_t;

typedef struct {
  frothy_ir_literal_kind_t kind;
  union {
    froth_cell_t int_value;
    bool bool_value;
    char *text_value;
  } as;
} frothy_ir_literal_t;

typedef enum {
  FROTHY_IR_BUILTIN_NONE = 0,
  FROTHY_IR_BUILTIN_CELLS,
  FROTHY_IR_BUILTIN_NOT,
  FROTHY_IR_BUILTIN_NEGATE,
  FROTHY_IR_BUILTIN_ADD,
  FROTHY_IR_BUILTIN_SUB,
  FROTHY_IR_BUILTIN_MUL,
  FROTHY_IR_BUILTIN_DIV,
  FROTHY_IR_BUILTIN_REM,
  FROTHY_IR_BUILTIN_LT,
  FROTHY_IR_BUILTIN_LE,
  FROTHY_IR_BUILTIN_GT,
  FROTHY_IR_BUILTIN_GE,
  FROTHY_IR_BUILTIN_EQ,
  FROTHY_IR_BUILTIN_NEQ,
} frothy_ir_builtin_kind_t;

typedef enum {
  FROTHY_IR_NODE_LIT = 0,
  FROTHY_IR_NODE_READ_LOCAL,
  FROTHY_IR_NODE_WRITE_LOCAL,
  FROTHY_IR_NODE_READ_SLOT,
  FROTHY_IR_NODE_WRITE_SLOT,
  FROTHY_IR_NODE_READ_INDEX,
  FROTHY_IR_NODE_WRITE_INDEX,
  FROTHY_IR_NODE_FN,
  FROTHY_IR_NODE_CALL,
  FROTHY_IR_NODE_IF,
  FROTHY_IR_NODE_WHILE,
  FROTHY_IR_NODE_SEQ,
} frothy_ir_node_kind_t;

typedef struct {
  frothy_ir_node_kind_t kind;
  union {
    struct {
      frothy_ir_literal_id_t literal_id;
    } lit;
    struct {
      size_t local_index;
    } read_local;
    struct {
      size_t local_index;
      frothy_ir_node_id_t value;
    } write_local;
    struct {
      char *slot_name;
    } read_slot;
    struct {
      char *slot_name;
      frothy_ir_node_id_t value;
      bool require_existing;
    } write_slot;
    struct {
      frothy_ir_node_id_t base;
      frothy_ir_node_id_t index;
    } read_index;
    struct {
      frothy_ir_node_id_t base;
      frothy_ir_node_id_t index;
      frothy_ir_node_id_t value;
    } write_index;
    struct {
      size_t arity;
      size_t local_count;
      frothy_ir_node_id_t body;
    } fn;
    struct {
      frothy_ir_builtin_kind_t builtin;
      frothy_ir_node_id_t callee;
      size_t first_arg;
      size_t arg_count;
    } call;
    struct {
      frothy_ir_node_id_t condition;
      frothy_ir_node_id_t then_branch;
      frothy_ir_node_id_t else_branch;
      bool has_else_branch;
    } if_expr;
    struct {
      frothy_ir_node_id_t condition;
      frothy_ir_node_id_t body;
    } while_expr;
    struct {
      size_t first_item;
      size_t item_count;
    } seq;
  } as;
} frothy_ir_node_t;

typedef struct {
  frothy_ir_literal_t *literals;
  size_t literal_count;
  size_t literal_capacity;

  frothy_ir_node_t *nodes;
  size_t node_count;
  size_t node_capacity;

  frothy_ir_node_id_t *links;
  size_t link_count;
  size_t link_capacity;

  frothy_ir_node_id_t root;
  size_t root_local_count;
} frothy_ir_program_t;

void frothy_ir_program_init(frothy_ir_program_t *program);
void frothy_ir_program_free(frothy_ir_program_t *program);
froth_error_t frothy_ir_program_clone(const frothy_ir_program_t *source,
                                      frothy_ir_program_t *dest);

froth_error_t frothy_ir_render(const frothy_ir_program_t *program,
                               char **out_text);
froth_error_t frothy_ir_render_code(const frothy_ir_program_t *program,
                                    frothy_ir_node_id_t body, size_t arity,
                                    size_t local_count, char **out_text);
const char *frothy_ir_builtin_name(frothy_ir_builtin_kind_t builtin);
