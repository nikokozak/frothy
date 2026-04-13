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
