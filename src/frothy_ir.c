#include "frothy_ir.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  (void)vsnprintf(builder->data + builder->length, builder->capacity - builder->length,
                  format, args);
  va_end(args);
  builder->length += (size_t)needed;
  return FROTH_OK;
}

static froth_error_t frothy_ir_render_node(const frothy_ir_program_t *program,
                                           frothy_ir_node_id_t node_id,
                                           frothy_string_builder_t *builder);

static froth_error_t
frothy_ir_render_quoted(const char *text, frothy_string_builder_t *builder) {
  const unsigned char *cursor = (const unsigned char *)text;

  FROTH_TRY(frothy_sb_append_char(builder, '"'));
  while (*cursor != '\0') {
    switch (*cursor) {
    case '\\':
      FROTH_TRY(frothy_sb_append_text(builder, "\\\\"));
      break;
    case '"':
      FROTH_TRY(frothy_sb_append_text(builder, "\\\""));
      break;
    case '\n':
      FROTH_TRY(frothy_sb_append_text(builder, "\\n"));
      break;
    case '\r':
      FROTH_TRY(frothy_sb_append_text(builder, "\\r"));
      break;
    case '\t':
      FROTH_TRY(frothy_sb_append_text(builder, "\\t"));
      break;
    default:
      FROTH_TRY(frothy_sb_append_char(builder, (char)*cursor));
      break;
    }
    cursor++;
  }
  return frothy_sb_append_char(builder, '"');
}

static froth_error_t
frothy_ir_render_literal(const frothy_ir_program_t *program,
                         frothy_ir_literal_id_t literal_id,
                         frothy_string_builder_t *builder) {
  const frothy_ir_literal_t *literal = &program->literals[literal_id];

  FROTH_TRY(frothy_sb_append_text(builder, "(lit "));
  switch (literal->kind) {
  case FROTHY_IR_LITERAL_INT:
    FROTH_TRY(frothy_sb_appendf(builder, "%" FROTH_CELL_FORMAT,
                                literal->as.int_value));
    break;
  case FROTHY_IR_LITERAL_BOOL:
    FROTH_TRY(frothy_sb_append_text(builder,
                                    literal->as.bool_value ? "true" : "false"));
    break;
  case FROTHY_IR_LITERAL_NIL:
    FROTH_TRY(frothy_sb_append_text(builder, "nil"));
    break;
  case FROTHY_IR_LITERAL_TEXT:
    FROTH_TRY(frothy_ir_render_quoted(literal->as.text_value, builder));
    break;
  }
  return frothy_sb_append_char(builder, ')');
}

static froth_error_t frothy_ir_render_node_list(const frothy_ir_program_t *program,
                                                size_t first,
                                                size_t count,
                                                frothy_string_builder_t *builder) {
  size_t i;

  for (i = 0; i < count; i++) {
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(
        frothy_ir_render_node(program, program->links[first + i], builder));
  }

  return FROTH_OK;
}

static froth_error_t frothy_ir_render_node(const frothy_ir_program_t *program,
                                           frothy_ir_node_id_t node_id,
                                           frothy_string_builder_t *builder) {
  const frothy_ir_node_t *node = &program->nodes[node_id];

  switch (node->kind) {
  case FROTHY_IR_NODE_LIT:
    return frothy_ir_render_literal(program, node->as.lit.literal_id, builder);
  case FROTHY_IR_NODE_READ_LOCAL:
    return frothy_sb_appendf(builder, "(read-local %zu)",
                             node->as.read_local.local_index);
  case FROTHY_IR_NODE_WRITE_LOCAL:
    FROTH_TRY(frothy_sb_appendf(builder, "(write-local %zu ",
                                node->as.write_local.local_index));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.write_local.value, builder));
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_READ_SLOT:
    FROTH_TRY(frothy_sb_append_text(builder, "(read-slot "));
    FROTH_TRY(frothy_ir_render_quoted(node->as.read_slot.slot_name, builder));
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_WRITE_SLOT:
    FROTH_TRY(frothy_sb_append_text(
        builder, node->as.write_slot.require_existing ? "(write-slot! "
                                                      : "(write-slot "));
    FROTH_TRY(frothy_ir_render_quoted(node->as.write_slot.slot_name, builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.write_slot.value, builder));
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_READ_INDEX:
    FROTH_TRY(frothy_sb_append_text(builder, "(read-index "));
    FROTH_TRY(frothy_ir_render_node(program, node->as.read_index.base, builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.read_index.index, builder));
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_WRITE_INDEX:
    FROTH_TRY(frothy_sb_append_text(builder, "(write-index "));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.write_index.base, builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.write_index.index, builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.write_index.value, builder));
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_FN:
    FROTH_TRY(frothy_sb_appendf(builder, "(fn arity=%zu locals=%zu ",
                                node->as.fn.arity, node->as.fn.local_count));
    FROTH_TRY(frothy_ir_render_node(program, node->as.fn.body, builder));
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_CALL:
    FROTH_TRY(frothy_sb_append_text(builder, "(call "));
    if (node->as.call.builtin != FROTHY_IR_BUILTIN_NONE) {
      FROTH_TRY(frothy_sb_append_text(builder, "(builtin "));
      FROTH_TRY(frothy_ir_render_quoted(
          frothy_ir_builtin_name(node->as.call.builtin), builder));
      FROTH_TRY(frothy_sb_append_char(builder, ')'));
    } else {
      FROTH_TRY(
          frothy_ir_render_node(program, node->as.call.callee, builder));
    }
    FROTH_TRY(frothy_ir_render_node_list(program, node->as.call.first_arg,
                                         node->as.call.arg_count, builder));
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_IF:
    FROTH_TRY(frothy_sb_append_text(builder, "(if "));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.if_expr.condition, builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.if_expr.then_branch, builder));
    if (node->as.if_expr.has_else_branch) {
      FROTH_TRY(frothy_sb_append_char(builder, ' '));
      FROTH_TRY(frothy_ir_render_node(program, node->as.if_expr.else_branch,
                                      builder));
    }
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_WHILE:
    FROTH_TRY(frothy_sb_append_text(builder, "(while "));
    FROTH_TRY(
        frothy_ir_render_node(program, node->as.while_expr.condition, builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(frothy_ir_render_node(program, node->as.while_expr.body, builder));
    return frothy_sb_append_char(builder, ')');
  case FROTHY_IR_NODE_SEQ:
    FROTH_TRY(frothy_sb_append_text(builder, "(seq"));
    FROTH_TRY(frothy_ir_render_node_list(program, node->as.seq.first_item,
                                         node->as.seq.item_count, builder));
    return frothy_sb_append_char(builder, ')');
  }

  return FROTH_ERROR_SIGNATURE;
}

typedef struct {
  const frothy_ir_program_t *program;
  size_t arity;
  size_t local_count;
} frothy_surface_render_t;

static froth_error_t
frothy_ir_render_surface_expr(const frothy_surface_render_t *render,
                              frothy_ir_node_id_t node_id,
                              frothy_string_builder_t *builder);

static froth_error_t
frothy_ir_render_surface_block(const frothy_surface_render_t *render,
                               frothy_ir_node_id_t node_id,
                               frothy_string_builder_t *builder);

static froth_error_t frothy_ir_render_surface_local_name(
    const frothy_surface_render_t *render, size_t local_index,
    frothy_string_builder_t *builder) {
  if (local_index < render->arity) {
    return frothy_sb_appendf(builder, "arg%zu", local_index);
  }
  return frothy_sb_appendf(builder, "local%zu", local_index - render->arity);
}

static const char *
frothy_ir_surface_infix_operator(frothy_ir_builtin_kind_t builtin) {
  switch (builtin) {
  case FROTHY_IR_BUILTIN_ADD:
    return "+";
  case FROTHY_IR_BUILTIN_SUB:
    return "-";
  case FROTHY_IR_BUILTIN_MUL:
    return "*";
  case FROTHY_IR_BUILTIN_DIV:
    return "/";
  case FROTHY_IR_BUILTIN_REM:
    return "%";
  case FROTHY_IR_BUILTIN_LT:
    return "<";
  case FROTHY_IR_BUILTIN_LE:
    return "<=";
  case FROTHY_IR_BUILTIN_GT:
    return ">";
  case FROTHY_IR_BUILTIN_GE:
    return ">=";
  case FROTHY_IR_BUILTIN_EQ:
    return "==";
  case FROTHY_IR_BUILTIN_NEQ:
    return "!=";
  default:
    return NULL;
  }
}

static froth_error_t
frothy_ir_render_surface_literal(const frothy_ir_program_t *program,
                                 frothy_ir_literal_id_t literal_id,
                                 frothy_string_builder_t *builder) {
  const frothy_ir_literal_t *literal = &program->literals[literal_id];

  switch (literal->kind) {
  case FROTHY_IR_LITERAL_INT:
    return frothy_sb_appendf(builder, "%" FROTH_CELL_FORMAT,
                             literal->as.int_value);
  case FROTHY_IR_LITERAL_BOOL:
    return frothy_sb_append_text(builder,
                                 literal->as.bool_value ? "true" : "false");
  case FROTHY_IR_LITERAL_NIL:
    return frothy_sb_append_text(builder, "nil");
  case FROTHY_IR_LITERAL_TEXT:
    return frothy_ir_render_quoted(literal->as.text_value, builder);
  }

  return FROTH_ERROR_SIGNATURE;
}

static bool frothy_ir_surface_bool_literal(const frothy_ir_program_t *program,
                                           frothy_ir_node_id_t node_id,
                                           bool expected) {
  const frothy_ir_node_t *node;
  const frothy_ir_literal_t *literal;

  if (node_id >= program->node_count) {
    return false;
  }
  node = &program->nodes[node_id];
  if (node->kind != FROTHY_IR_NODE_LIT) {
    return false;
  }
  literal = &program->literals[node->as.lit.literal_id];
  return literal->kind == FROTHY_IR_LITERAL_BOOL &&
         literal->as.bool_value == expected;
}

static bool frothy_ir_surface_int_literal(const frothy_ir_program_t *program,
                                          frothy_ir_node_id_t node_id,
                                          froth_cell_t expected) {
  const frothy_ir_node_t *node;
  const frothy_ir_literal_t *literal;

  if (node_id >= program->node_count) {
    return false;
  }
  node = &program->nodes[node_id];
  if (node->kind != FROTHY_IR_NODE_LIT) {
    return false;
  }
  literal = &program->literals[node->as.lit.literal_id];
  return literal->kind == FROTHY_IR_LITERAL_INT &&
         literal->as.int_value == expected;
}

static bool frothy_ir_surface_match_bool_condition(
    const frothy_ir_program_t *program, frothy_ir_node_id_t node_id,
    frothy_ir_node_id_t *condition_out) {
  const frothy_ir_node_t *node;

  if (node_id >= program->node_count) {
    return false;
  }
  node = &program->nodes[node_id];
  if (node->kind != FROTHY_IR_NODE_IF || !node->as.if_expr.has_else_branch) {
    return false;
  }
  if (!frothy_ir_surface_bool_literal(program, node->as.if_expr.then_branch,
                                      true) ||
      !frothy_ir_surface_bool_literal(program, node->as.if_expr.else_branch,
                                      false)) {
    return false;
  }
  *condition_out = node->as.if_expr.condition;
  return true;
}

static bool frothy_ir_surface_match_short_circuit(
    const frothy_ir_program_t *program, const frothy_ir_node_t *node,
    bool *is_or_out, frothy_ir_node_id_t *lhs_out, frothy_ir_node_id_t *rhs_out) {
  frothy_ir_node_id_t rhs_condition;

  if (node->kind != FROTHY_IR_NODE_IF || !node->as.if_expr.has_else_branch) {
    return false;
  }

  if (frothy_ir_surface_bool_literal(program, node->as.if_expr.then_branch,
                                     true) &&
      frothy_ir_surface_match_bool_condition(program,
                                             node->as.if_expr.else_branch,
                                             &rhs_condition)) {
    *is_or_out = true;
    *lhs_out = node->as.if_expr.condition;
    *rhs_out = rhs_condition;
    return true;
  }

  if (frothy_ir_surface_bool_literal(program, node->as.if_expr.else_branch,
                                     false) &&
      frothy_ir_surface_match_bool_condition(program,
                                             node->as.if_expr.then_branch,
                                             &rhs_condition)) {
    *is_or_out = false;
    *lhs_out = node->as.if_expr.condition;
    *rhs_out = rhs_condition;
    return true;
  }

  return false;
}

static bool frothy_ir_surface_match_repeat_increment(
    const frothy_ir_program_t *program, frothy_ir_node_id_t node_id,
    size_t counter_local) {
  const frothy_ir_node_t *node;
  const frothy_ir_node_t *value_node;

  if (node_id >= program->node_count) {
    return false;
  }
  node = &program->nodes[node_id];
  if (node->kind != FROTHY_IR_NODE_WRITE_LOCAL ||
      node->as.write_local.local_index != counter_local) {
    return false;
  }

  value_node = &program->nodes[node->as.write_local.value];
  if (value_node->kind != FROTHY_IR_NODE_CALL ||
      value_node->as.call.builtin != FROTHY_IR_BUILTIN_ADD ||
      value_node->as.call.arg_count != 2) {
    return false;
  }
  if (!frothy_ir_surface_int_literal(program,
                                     program->links[value_node->as.call.first_arg + 1],
                                     1)) {
    return false;
  }

  node = &program->nodes[program->links[value_node->as.call.first_arg]];
  return node->kind == FROTHY_IR_NODE_READ_LOCAL &&
         node->as.read_local.local_index == counter_local;
}

static bool frothy_ir_surface_match_repeat(
    const frothy_ir_program_t *program, frothy_ir_node_id_t node_id,
    frothy_ir_node_id_t *count_expr_out, bool *has_index_out,
    size_t *index_local_out, frothy_ir_node_id_t *body_out) {
  const frothy_ir_node_t *root;
  const frothy_ir_node_t *limit_init;
  const frothy_ir_node_t *counter_init;
  const frothy_ir_node_t *while_node;
  const frothy_ir_node_t *condition;
  const frothy_ir_node_t *while_body;
  size_t limit_local;
  size_t counter_local;
  size_t loop_offset = 0;

  if (node_id >= program->node_count) {
    return false;
  }
  root = &program->nodes[node_id];
  if (root->kind != FROTHY_IR_NODE_SEQ || root->as.seq.item_count != 3) {
    return false;
  }

  limit_init = &program->nodes[program->links[root->as.seq.first_item]];
  counter_init =
      &program->nodes[program->links[root->as.seq.first_item + 1]];
  while_node = &program->nodes[program->links[root->as.seq.first_item + 2]];
  if (limit_init->kind != FROTHY_IR_NODE_WRITE_LOCAL ||
      counter_init->kind != FROTHY_IR_NODE_WRITE_LOCAL ||
      while_node->kind != FROTHY_IR_NODE_WHILE) {
    return false;
  }
  if (!frothy_ir_surface_int_literal(program, counter_init->as.write_local.value,
                                     0)) {
    return false;
  }

  limit_local = limit_init->as.write_local.local_index;
  counter_local = counter_init->as.write_local.local_index;

  condition = &program->nodes[while_node->as.while_expr.condition];
  if (condition->kind != FROTHY_IR_NODE_CALL ||
      condition->as.call.builtin != FROTHY_IR_BUILTIN_LT ||
      condition->as.call.arg_count != 2) {
    return false;
  }
  {
    const frothy_ir_node_t *lhs =
        &program->nodes[program->links[condition->as.call.first_arg]];
    const frothy_ir_node_t *rhs =
        &program->nodes[program->links[condition->as.call.first_arg + 1]];
    if (lhs->kind != FROTHY_IR_NODE_READ_LOCAL ||
        lhs->as.read_local.local_index != counter_local ||
        rhs->kind != FROTHY_IR_NODE_READ_LOCAL ||
        rhs->as.read_local.local_index != limit_local) {
      return false;
    }
  }

  while_body = &program->nodes[while_node->as.while_expr.body];
  if (while_body->kind != FROTHY_IR_NODE_SEQ ||
      while_body->as.seq.item_count < 2 ||
      while_body->as.seq.item_count > 3) {
    return false;
  }

  *has_index_out = false;
  if (while_body->as.seq.item_count == 3) {
    const frothy_ir_node_t *prelude =
        &program->nodes[program->links[while_body->as.seq.first_item]];
    const frothy_ir_node_t *prelude_value;

    if (prelude->kind != FROTHY_IR_NODE_WRITE_LOCAL) {
      return false;
    }
    prelude_value = &program->nodes[prelude->as.write_local.value];
    if (prelude_value->kind != FROTHY_IR_NODE_READ_LOCAL ||
        prelude_value->as.read_local.local_index != counter_local) {
      return false;
    }
    *has_index_out = true;
    *index_local_out = prelude->as.write_local.local_index;
    loop_offset = 1;
  }

  *body_out = program->links[while_body->as.seq.first_item + loop_offset];
  if (!frothy_ir_surface_match_repeat_increment(
          program,
          program->links[while_body->as.seq.first_item + loop_offset + 1],
          counter_local)) {
    return false;
  }

  *count_expr_out = limit_init->as.write_local.value;
  return true;
}

static froth_error_t frothy_ir_render_surface_arg_list(
    const frothy_surface_render_t *render, size_t first_arg, size_t arg_count,
    frothy_string_builder_t *builder) {
  size_t i;

  for (i = 0; i < arg_count; i++) {
    if (i != 0) {
      FROTH_TRY(frothy_sb_append_text(builder, ", "));
    }
    FROTH_TRY(frothy_ir_render_surface_expr(
        render, render->program->links[first_arg + i], builder));
  }

  return FROTH_OK;
}

static froth_error_t
frothy_ir_render_surface_call(const frothy_surface_render_t *render,
                              const frothy_ir_node_t *node,
                              frothy_string_builder_t *builder) {
  const char *infix = frothy_ir_surface_infix_operator(node->as.call.builtin);

  if (node->as.call.builtin == FROTHY_IR_BUILTIN_CELLS &&
      node->as.call.arg_count == 1) {
    FROTH_TRY(frothy_sb_append_text(builder, "cells("));
    FROTH_TRY(frothy_ir_render_surface_expr(
        render, render->program->links[node->as.call.first_arg], builder));
    return frothy_sb_append_char(builder, ')');
  }
  if (node->as.call.builtin == FROTHY_IR_BUILTIN_NOT &&
      node->as.call.arg_count == 1) {
    FROTH_TRY(frothy_sb_append_text(builder, "not "));
    return frothy_ir_render_surface_expr(
        render, render->program->links[node->as.call.first_arg], builder);
  }
  if (node->as.call.builtin == FROTHY_IR_BUILTIN_NEGATE &&
      node->as.call.arg_count == 1) {
    FROTH_TRY(frothy_sb_append_char(builder, '-'));
    return frothy_ir_render_surface_expr(
        render, render->program->links[node->as.call.first_arg], builder);
  }
  if (infix != NULL && node->as.call.arg_count == 2) {
    FROTH_TRY(frothy_ir_render_surface_expr(
        render, render->program->links[node->as.call.first_arg], builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(frothy_sb_append_text(builder, infix));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    return frothy_ir_render_surface_expr(
        render, render->program->links[node->as.call.first_arg + 1], builder);
  }
  if (node->as.call.builtin == FROTHY_IR_BUILTIN_NONE) {
    const frothy_ir_node_t *callee = &render->program->nodes[node->as.call.callee];

    if (callee->kind == FROTHY_IR_NODE_READ_SLOT) {
      FROTH_TRY(
          frothy_sb_append_text(builder, callee->as.read_slot.slot_name));
      FROTH_TRY(frothy_sb_append_char(builder, ':'));
      if (node->as.call.arg_count != 0) {
        FROTH_TRY(frothy_sb_append_char(builder, ' '));
        FROTH_TRY(frothy_ir_render_surface_arg_list(
            render, node->as.call.first_arg, node->as.call.arg_count, builder));
      }
      return FROTH_OK;
    }

    FROTH_TRY(frothy_sb_append_text(builder, "call "));
    FROTH_TRY(
        frothy_ir_render_surface_expr(render, node->as.call.callee, builder));
    if (node->as.call.arg_count != 0) {
      FROTH_TRY(frothy_sb_append_text(builder, " with "));
      FROTH_TRY(frothy_ir_render_surface_arg_list(
          render, node->as.call.first_arg, node->as.call.arg_count, builder));
    }
    return FROTH_OK;
  }

  FROTH_TRY(frothy_sb_append_text(builder, "call "));
  FROTH_TRY(
      frothy_sb_append_text(builder, frothy_ir_builtin_name(node->as.call.builtin)));
  if (node->as.call.arg_count != 0) {
    FROTH_TRY(frothy_sb_append_text(builder, " with "));
    FROTH_TRY(frothy_ir_render_surface_arg_list(
        render, node->as.call.first_arg, node->as.call.arg_count, builder));
  }
  return FROTH_OK;
}

static froth_error_t
frothy_ir_render_surface_expr(const frothy_surface_render_t *render,
                              frothy_ir_node_id_t node_id,
                              frothy_string_builder_t *builder) {
  const frothy_ir_node_t *node = &render->program->nodes[node_id];
  bool is_or;
  frothy_ir_node_id_t lhs;
  frothy_ir_node_id_t rhs;
  frothy_ir_node_id_t count_expr;
  frothy_ir_node_id_t repeat_body;
  bool has_index;
  size_t index_local;

  switch (node->kind) {
  case FROTHY_IR_NODE_LIT:
    return frothy_ir_render_surface_literal(render->program,
                                            node->as.lit.literal_id, builder);
  case FROTHY_IR_NODE_READ_LOCAL:
    return frothy_ir_render_surface_local_name(
        render, node->as.read_local.local_index, builder);
  case FROTHY_IR_NODE_WRITE_LOCAL:
    FROTH_TRY(frothy_sb_append_text(builder, "here "));
    FROTH_TRY(frothy_ir_render_surface_local_name(
        render, node->as.write_local.local_index, builder));
    FROTH_TRY(frothy_sb_append_text(builder, " is "));
    return frothy_ir_render_surface_expr(render, node->as.write_local.value,
                                         builder);
  case FROTHY_IR_NODE_READ_SLOT:
    return frothy_sb_append_text(builder, node->as.read_slot.slot_name);
  case FROTHY_IR_NODE_WRITE_SLOT:
    if (node->as.write_slot.require_existing) {
      FROTH_TRY(frothy_sb_append_text(builder, "set "));
      FROTH_TRY(
          frothy_sb_append_text(builder, node->as.write_slot.slot_name));
      FROTH_TRY(frothy_sb_append_text(builder, " to "));
    } else {
      FROTH_TRY(
          frothy_sb_append_text(builder, node->as.write_slot.slot_name));
      FROTH_TRY(frothy_sb_append_text(builder, " is "));
    }
    return frothy_ir_render_surface_expr(render, node->as.write_slot.value,
                                         builder);
  case FROTHY_IR_NODE_READ_INDEX:
    FROTH_TRY(
        frothy_ir_render_surface_expr(render, node->as.read_index.base, builder));
    FROTH_TRY(frothy_sb_append_char(builder, '['));
    FROTH_TRY(frothy_ir_render_surface_expr(render, node->as.read_index.index,
                                            builder));
    return frothy_sb_append_char(builder, ']');
  case FROTHY_IR_NODE_WRITE_INDEX:
    FROTH_TRY(frothy_sb_append_text(builder, "set "));
    FROTH_TRY(frothy_ir_render_surface_expr(render, node->as.write_index.base,
                                            builder));
    FROTH_TRY(frothy_sb_append_char(builder, '['));
    FROTH_TRY(frothy_ir_render_surface_expr(render, node->as.write_index.index,
                                            builder));
    FROTH_TRY(frothy_sb_append_text(builder, "] to "));
    return frothy_ir_render_surface_expr(render, node->as.write_index.value,
                                         builder);
  case FROTHY_IR_NODE_FN: {
    frothy_surface_render_t child = {
        .program = render->program,
        .arity = node->as.fn.arity,
        .local_count = node->as.fn.local_count,
    };
    size_t i;

    FROTH_TRY(frothy_sb_append_text(builder, "fn"));
    if (node->as.fn.arity != 0) {
      FROTH_TRY(frothy_sb_append_text(builder, " with "));
      for (i = 0; i < node->as.fn.arity; i++) {
        if (i != 0) {
          FROTH_TRY(frothy_sb_append_text(builder, ", "));
        }
        FROTH_TRY(frothy_sb_appendf(builder, "arg%zu", i));
      }
    }
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    return frothy_ir_render_surface_block(&child, node->as.fn.body, builder);
  }
  case FROTHY_IR_NODE_CALL:
    return frothy_ir_render_surface_call(render, node, builder);
  case FROTHY_IR_NODE_IF:
    if (frothy_ir_surface_match_short_circuit(render->program, node, &is_or,
                                              &lhs, &rhs)) {
      FROTH_TRY(frothy_ir_render_surface_expr(render, lhs, builder));
      FROTH_TRY(frothy_sb_append_text(builder, is_or ? " or " : " and "));
      return frothy_ir_render_surface_expr(render, rhs, builder);
    }
    if (!node->as.if_expr.has_else_branch) {
      const frothy_ir_node_t *condition =
          &render->program->nodes[node->as.if_expr.condition];

      if (condition->kind == FROTHY_IR_NODE_CALL &&
          condition->as.call.builtin == FROTHY_IR_BUILTIN_NOT &&
          condition->as.call.arg_count == 1) {
        FROTH_TRY(frothy_sb_append_text(builder, "unless "));
        FROTH_TRY(frothy_ir_render_surface_expr(
            render, render->program->links[condition->as.call.first_arg],
            builder));
      } else {
        FROTH_TRY(frothy_sb_append_text(builder, "when "));
        FROTH_TRY(frothy_ir_render_surface_expr(
            render, node->as.if_expr.condition, builder));
      }
      FROTH_TRY(frothy_sb_append_char(builder, ' '));
      return frothy_ir_render_surface_block(render, node->as.if_expr.then_branch,
                                            builder);
    }

    FROTH_TRY(frothy_sb_append_text(builder, "if "));
    FROTH_TRY(
        frothy_ir_render_surface_expr(render, node->as.if_expr.condition, builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(frothy_ir_render_surface_block(render, node->as.if_expr.then_branch,
                                             builder));
    FROTH_TRY(frothy_sb_append_text(builder, " else "));
    return frothy_ir_render_surface_block(render, node->as.if_expr.else_branch,
                                          builder);
  case FROTHY_IR_NODE_WHILE:
    FROTH_TRY(frothy_sb_append_text(builder, "while "));
    FROTH_TRY(frothy_ir_render_surface_expr(render, node->as.while_expr.condition,
                                            builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    return frothy_ir_render_surface_block(render, node->as.while_expr.body,
                                          builder);
  case FROTHY_IR_NODE_SEQ:
    if (frothy_ir_surface_match_repeat(render->program, node_id, &count_expr,
                                       &has_index, &index_local,
                                       &repeat_body)) {
      FROTH_TRY(frothy_sb_append_text(builder, "repeat "));
      FROTH_TRY(frothy_ir_render_surface_expr(render, count_expr, builder));
      if (has_index) {
        FROTH_TRY(frothy_sb_append_text(builder, " as "));
        FROTH_TRY(frothy_ir_render_surface_local_name(render, index_local,
                                                      builder));
      }
      FROTH_TRY(frothy_sb_append_char(builder, ' '));
      return frothy_ir_render_surface_block(render, repeat_body, builder);
    }
    return frothy_ir_render_surface_block(render, node_id, builder);
  }

  return FROTH_ERROR_SIGNATURE;
}

static froth_error_t
frothy_ir_render_surface_block(const frothy_surface_render_t *render,
                               frothy_ir_node_id_t node_id,
                               frothy_string_builder_t *builder) {
  const frothy_ir_node_t *node = &render->program->nodes[node_id];
  size_t i;

  FROTH_TRY(frothy_sb_append_char(builder, '['));
  if (node->kind == FROTHY_IR_NODE_SEQ) {
    for (i = 0; i < node->as.seq.item_count; i++) {
      if (i == 0) {
        FROTH_TRY(frothy_sb_append_char(builder, ' '));
      } else {
        FROTH_TRY(frothy_sb_append_text(builder, "; "));
      }
      FROTH_TRY(frothy_ir_render_surface_expr(
          render, render->program->links[node->as.seq.first_item + i], builder));
    }
    if (node->as.seq.item_count != 0) {
      FROTH_TRY(frothy_sb_append_char(builder, ' '));
    }
  } else {
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
    FROTH_TRY(frothy_ir_render_surface_expr(render, node_id, builder));
    FROTH_TRY(frothy_sb_append_char(builder, ' '));
  }
  return frothy_sb_append_char(builder, ']');
}

void frothy_ir_program_init(frothy_ir_program_t *program) {
  memset(program, 0, sizeof(*program));
  program->root = FROTHY_IR_NODE_INVALID;
}

void frothy_ir_program_free(frothy_ir_program_t *program) {
  size_t i;

  if (program == NULL) {
    return;
  }

  for (i = 0; i < program->literal_count; i++) {
    if (program->literals[i].kind == FROTHY_IR_LITERAL_TEXT) {
      free(program->literals[i].as.text_value);
    }
  }

  for (i = 0; i < program->node_count; i++) {
    if (program->nodes[i].kind == FROTHY_IR_NODE_READ_SLOT) {
      free(program->nodes[i].as.read_slot.slot_name);
    } else if (program->nodes[i].kind == FROTHY_IR_NODE_WRITE_SLOT) {
      free(program->nodes[i].as.write_slot.slot_name);
    }
  }

  free(program->literals);
  free(program->nodes);
  free(program->links);
  frothy_ir_program_init(program);
}

froth_error_t frothy_ir_program_clone(const frothy_ir_program_t *source,
                                      frothy_ir_program_t *dest) {
  size_t i;

  frothy_ir_program_init(dest);
  dest->root = source->root;
  dest->root_local_count = source->root_local_count;

  if (source->literal_count > 0) {
    dest->literals = (frothy_ir_literal_t *)calloc(
        source->literal_count, sizeof(*dest->literals));
    if (dest->literals == NULL) {
      frothy_ir_program_free(dest);
      return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
    }
    dest->literal_count = source->literal_count;
    dest->literal_capacity = source->literal_count;

    for (i = 0; i < source->literal_count; i++) {
      dest->literals[i] = source->literals[i];
      if (source->literals[i].kind == FROTHY_IR_LITERAL_TEXT) {
        dest->literals[i].as.text_value = strdup(source->literals[i].as.text_value);
        if (dest->literals[i].as.text_value == NULL) {
          frothy_ir_program_free(dest);
          return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
        }
      }
    }
  }

  if (source->node_count > 0) {
    dest->nodes =
        (frothy_ir_node_t *)calloc(source->node_count, sizeof(*dest->nodes));
    if (dest->nodes == NULL) {
      frothy_ir_program_free(dest);
      return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
    }
    dest->node_count = source->node_count;
    dest->node_capacity = source->node_count;

    for (i = 0; i < source->node_count; i++) {
      dest->nodes[i] = source->nodes[i];
      if (source->nodes[i].kind == FROTHY_IR_NODE_READ_SLOT) {
        dest->nodes[i].as.read_slot.slot_name =
            strdup(source->nodes[i].as.read_slot.slot_name);
        if (dest->nodes[i].as.read_slot.slot_name == NULL) {
          frothy_ir_program_free(dest);
          return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
        }
      } else if (source->nodes[i].kind == FROTHY_IR_NODE_WRITE_SLOT) {
        dest->nodes[i].as.write_slot.slot_name =
            strdup(source->nodes[i].as.write_slot.slot_name);
        if (dest->nodes[i].as.write_slot.slot_name == NULL) {
          frothy_ir_program_free(dest);
          return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
        }
      }
    }
  }

  if (source->link_count > 0) {
    dest->links = (frothy_ir_node_id_t *)calloc(source->link_count,
                                                sizeof(*dest->links));
    if (dest->links == NULL) {
      frothy_ir_program_free(dest);
      return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
    }
    memcpy(dest->links, source->links, source->link_count * sizeof(*dest->links));
    dest->link_count = source->link_count;
    dest->link_capacity = source->link_count;
  }

  return FROTH_OK;
}

froth_error_t frothy_ir_render(const frothy_ir_program_t *program,
                               char **out_text) {
  frothy_string_builder_t builder;
  froth_error_t err;

  *out_text = NULL;
  if (program->root == FROTHY_IR_NODE_INVALID) {
    return FROTH_ERROR_SIGNATURE;
  }

  frothy_sb_init(&builder);
  err = frothy_ir_render_node(program, program->root, &builder);
  if (err != FROTH_OK) {
    free(builder.data);
    return err;
  }
  *out_text = builder.data;
  return FROTH_OK;
}

froth_error_t frothy_ir_render_code(const frothy_ir_program_t *program,
                                    frothy_ir_node_id_t body, size_t arity,
                                    size_t local_count, char **out_text) {
  frothy_string_builder_t builder;
  froth_error_t err;

  *out_text = NULL;
  if (body >= program->node_count) {
    return FROTH_ERROR_BOUNDS;
  }

  frothy_sb_init(&builder);
  err = frothy_sb_appendf(&builder, "(fn arity=%zu locals=%zu ", arity,
                          local_count);
  if (err != FROTH_OK) {
    free(builder.data);
    return err;
  }
  err = frothy_ir_render_node(program, body, &builder);
  if (err != FROTH_OK) {
    free(builder.data);
    return err;
  }
  err = frothy_sb_append_char(&builder, ')');
  if (err != FROTH_OK) {
    free(builder.data);
    return err;
  }
  *out_text = builder.data;
  return FROTH_OK;
}

froth_error_t frothy_ir_render_surface_code(const frothy_ir_program_t *program,
                                            frothy_ir_node_id_t body,
                                            size_t arity, size_t local_count,
                                            const char *name, char **out_text) {
  frothy_string_builder_t builder;
  frothy_surface_render_t render = {
      .program = program,
      .arity = arity,
      .local_count = local_count,
  };
  size_t i;
  froth_error_t err;

  *out_text = NULL;
  if (body >= program->node_count) {
    return FROTH_ERROR_BOUNDS;
  }

  frothy_sb_init(&builder);
  if (name != NULL && *name != '\0') {
    err = frothy_sb_appendf(&builder, "to %s", name);
  } else {
    err = frothy_sb_append_text(&builder, "fn");
  }
  if (err != FROTH_OK) {
    free(builder.data);
    return err;
  }
  if (arity != 0) {
    err = frothy_sb_append_text(&builder, " with ");
    if (err != FROTH_OK) {
      free(builder.data);
      return err;
    }
    for (i = 0; i < arity; i++) {
      if (i != 0) {
        err = frothy_sb_append_text(&builder, ", ");
        if (err != FROTH_OK) {
          free(builder.data);
          return err;
        }
      }
      err = frothy_sb_appendf(&builder, "arg%zu", i);
      if (err != FROTH_OK) {
        free(builder.data);
        return err;
      }
    }
  }
  err = frothy_sb_append_char(&builder, ' ');
  if (err != FROTH_OK) {
    free(builder.data);
    return err;
  }
  err = frothy_ir_render_surface_block(&render, body, &builder);
  if (err != FROTH_OK) {
    free(builder.data);
    return err;
  }
  *out_text = builder.data;
  return FROTH_OK;
}

const char *frothy_ir_builtin_name(frothy_ir_builtin_kind_t builtin) {
  switch (builtin) {
  case FROTHY_IR_BUILTIN_NONE:
    return "";
  case FROTHY_IR_BUILTIN_CELLS:
    return "cells";
  case FROTHY_IR_BUILTIN_NOT:
    return "not";
  case FROTHY_IR_BUILTIN_NEGATE:
    return "neg";
  case FROTHY_IR_BUILTIN_ADD:
    return "+";
  case FROTHY_IR_BUILTIN_SUB:
    return "-";
  case FROTHY_IR_BUILTIN_MUL:
    return "*";
  case FROTHY_IR_BUILTIN_DIV:
    return "/";
  case FROTHY_IR_BUILTIN_REM:
    return "%";
  case FROTHY_IR_BUILTIN_LT:
    return "<";
  case FROTHY_IR_BUILTIN_LE:
    return "<=";
  case FROTHY_IR_BUILTIN_GT:
    return ">";
  case FROTHY_IR_BUILTIN_GE:
    return ">=";
  case FROTHY_IR_BUILTIN_EQ:
    return "==";
  case FROTHY_IR_BUILTIN_NEQ:
    return "!=";
  }

  return "";
}
