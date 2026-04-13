#include "frothy_parser.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  FROTHY_TOKEN_EOF = 0,
  FROTHY_TOKEN_NAME,
  FROTHY_TOKEN_INT,
  FROTHY_TOKEN_TEXT,
  FROTHY_TOKEN_LPAREN,
  FROTHY_TOKEN_RPAREN,
  FROTHY_TOKEN_LBRACE,
  FROTHY_TOKEN_RBRACE,
  FROTHY_TOKEN_LBRACKET,
  FROTHY_TOKEN_RBRACKET,
  FROTHY_TOKEN_COMMA,
  FROTHY_TOKEN_SEMICOLON,
  FROTHY_TOKEN_COLON,
  FROTHY_TOKEN_EQUAL,
  FROTHY_TOKEN_PLUS,
  FROTHY_TOKEN_MINUS,
  FROTHY_TOKEN_STAR,
  FROTHY_TOKEN_SLASH,
  FROTHY_TOKEN_PERCENT,
  FROTHY_TOKEN_LT,
  FROTHY_TOKEN_LE,
  FROTHY_TOKEN_GT,
  FROTHY_TOKEN_GE,
  FROTHY_TOKEN_EQEQ,
  FROTHY_TOKEN_NEQ,
  FROTHY_TOKEN_KW_HERE,
  FROTHY_TOKEN_KW_IS,
  FROTHY_TOKEN_KW_SET,
  FROTHY_TOKEN_KW_TO,
  FROTHY_TOKEN_KW_WITH,
  FROTHY_TOKEN_KW_AS,
  FROTHY_TOKEN_KW_FN,
  FROTHY_TOKEN_KW_IF,
  FROTHY_TOKEN_KW_ELSE,
  FROTHY_TOKEN_KW_WHILE,
  FROTHY_TOKEN_KW_REPEAT,
  FROTHY_TOKEN_KW_WHEN,
  FROTHY_TOKEN_KW_UNLESS,
  FROTHY_TOKEN_KW_TRUE,
  FROTHY_TOKEN_KW_FALSE,
  FROTHY_TOKEN_KW_NIL,
  FROTHY_TOKEN_KW_NOT,
  FROTHY_TOKEN_KW_AND,
  FROTHY_TOKEN_KW_OR,
  FROTHY_TOKEN_KW_CALL,
  FROTHY_TOKEN_KW_CELLS,
} frothy_token_kind_t;

typedef struct {
  frothy_token_kind_t kind;
  const char *start;
  size_t length;
  froth_cell_t int_value;
  char *owned_text;
} frothy_token_t;

typedef struct {
  char *name;
  size_t local_index;
  size_t scope_index;
} frothy_binding_t;

typedef struct {
  size_t binding_start;
} frothy_scope_t;

typedef struct {
  size_t scope_base;
  size_t next_local_index;
  bool reject_outer_capture;
} frothy_frame_t;

typedef struct {
  const char *source;
  const char *cursor;
  const char *prev_token_end;
  bool prefer_block_on_spaced_bracket;
  frothy_token_t current;
  frothy_token_t next;
  frothy_ir_program_t *program;

  frothy_binding_t *bindings;
  size_t binding_count;
  size_t binding_capacity;

  frothy_scope_t *scopes;
  size_t scope_count;
  size_t scope_capacity;

  frothy_frame_t *frames;
  size_t frame_count;
  size_t frame_capacity;
} frothy_parser_t;

typedef enum {
  FROTHY_PLACE_LOCAL = 0,
  FROTHY_PLACE_SLOT,
  FROTHY_PLACE_INDEX,
} frothy_place_kind_t;

typedef struct {
  frothy_place_kind_t kind;
  union {
    size_t local_index;
    char *slot_name;
    struct {
      frothy_ir_node_id_t base;
      frothy_ir_node_id_t index;
    } index;
  } as;
} frothy_place_t;

typedef struct {
  frothy_ir_node_id_t *items;
  size_t count;
  size_t capacity;
} frothy_node_list_t;

typedef enum {
  FROTHY_FN_PARAMS_NONE = 0,
  FROTHY_FN_PARAMS_LIST,
  FROTHY_FN_PARAMS_WITH,
} frothy_fn_params_kind_t;

typedef enum {
  FROTHY_FN_BODY_BLOCK = 0,
  FROTHY_FN_BODY_EXPR,
} frothy_fn_body_kind_t;

typedef enum {
  FROTHY_TOP_LEVEL_NAMED_FN_NONE = 0,
  FROTHY_TOP_LEVEL_NAMED_FN_EXPR,
  FROTHY_TOP_LEVEL_NAMED_FN_BLOCK,
} frothy_top_level_named_fn_kind_t;

static void frothy_token_clear(frothy_token_t *token) {
  free(token->owned_text);
  memset(token, 0, sizeof(*token));
}

static char *frothy_strdup_range(const char *start, size_t length) {
  char *copy = (char *)malloc(length + 1);

  if (copy == NULL) {
    return NULL;
  }

  memcpy(copy, start, length);
  copy[length] = '\0';
  return copy;
}

static bool frothy_token_matches_name(const frothy_token_t *token,
                                      const char *text) {
  size_t length = strlen(text);

  return token->kind == FROTHY_TOKEN_NAME && token->length == length &&
         strncmp(token->start, text, length) == 0;
}

static froth_error_t frothy_parser_reserve_bindings(frothy_parser_t *parser,
                                                    size_t extra) {
  size_t needed = parser->binding_count + extra;
  size_t capacity = parser->binding_capacity == 0 ? 8 : parser->binding_capacity;
  frothy_binding_t *resized;

  while (capacity < needed) {
    capacity *= 2;
  }
  if (capacity == parser->binding_capacity) {
    return FROTH_OK;
  }

  resized = (frothy_binding_t *)realloc(parser->bindings,
                                        capacity * sizeof(*parser->bindings));
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  parser->bindings = resized;
  parser->binding_capacity = capacity;
  return FROTH_OK;
}

static froth_error_t frothy_parser_reserve_scopes(frothy_parser_t *parser,
                                                  size_t extra) {
  size_t needed = parser->scope_count + extra;
  size_t capacity = parser->scope_capacity == 0 ? 4 : parser->scope_capacity;
  frothy_scope_t *resized;

  while (capacity < needed) {
    capacity *= 2;
  }
  if (capacity == parser->scope_capacity) {
    return FROTH_OK;
  }

  resized = (frothy_scope_t *)realloc(parser->scopes,
                                      capacity * sizeof(*parser->scopes));
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  parser->scopes = resized;
  parser->scope_capacity = capacity;
  return FROTH_OK;
}

static froth_error_t frothy_parser_reserve_frames(frothy_parser_t *parser,
                                                  size_t extra) {
  size_t needed = parser->frame_count + extra;
  size_t capacity = parser->frame_capacity == 0 ? 4 : parser->frame_capacity;
  frothy_frame_t *resized;

  while (capacity < needed) {
    capacity *= 2;
  }
  if (capacity == parser->frame_capacity) {
    return FROTH_OK;
  }

  resized = (frothy_frame_t *)realloc(parser->frames,
                                      capacity * sizeof(*parser->frames));
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  parser->frames = resized;
  parser->frame_capacity = capacity;
  return FROTH_OK;
}

static froth_error_t frothy_program_reserve_literals(frothy_ir_program_t *program,
                                                     size_t extra) {
  size_t needed = program->literal_count + extra;
  size_t capacity = program->literal_capacity == 0 ? 8 : program->literal_capacity;
  frothy_ir_literal_t *resized;

  while (capacity < needed) {
    capacity *= 2;
  }
  if (capacity == program->literal_capacity) {
    return FROTH_OK;
  }

  resized = (frothy_ir_literal_t *)realloc(program->literals,
                                           capacity * sizeof(*program->literals));
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  program->literals = resized;
  program->literal_capacity = capacity;
  return FROTH_OK;
}

static froth_error_t frothy_program_reserve_nodes(frothy_ir_program_t *program,
                                                  size_t extra) {
  size_t needed = program->node_count + extra;
  size_t capacity = program->node_capacity == 0 ? 16 : program->node_capacity;
  frothy_ir_node_t *resized;

  while (capacity < needed) {
    capacity *= 2;
  }
  if (capacity == program->node_capacity) {
    return FROTH_OK;
  }

  resized = (frothy_ir_node_t *)realloc(program->nodes,
                                        capacity * sizeof(*program->nodes));
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  program->nodes = resized;
  program->node_capacity = capacity;
  return FROTH_OK;
}

static froth_error_t frothy_program_reserve_links(frothy_ir_program_t *program,
                                                  size_t extra) {
  size_t needed = program->link_count + extra;
  size_t capacity = program->link_capacity == 0 ? 16 : program->link_capacity;
  frothy_ir_node_id_t *resized;

  while (capacity < needed) {
    capacity *= 2;
  }
  if (capacity == program->link_capacity) {
    return FROTH_OK;
  }

  resized = (frothy_ir_node_id_t *)realloc(program->links,
                                           capacity * sizeof(*program->links));
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  program->links = resized;
  program->link_capacity = capacity;
  return FROTH_OK;
}

static froth_error_t
frothy_program_add_literal(frothy_ir_program_t *program,
                           const frothy_ir_literal_t *literal,
                           frothy_ir_literal_id_t *literal_id_out) {
  FROTH_TRY(frothy_program_reserve_literals(program, 1));
  *literal_id_out = program->literal_count;
  program->literals[program->literal_count++] = *literal;
  return FROTH_OK;
}

static froth_error_t frothy_program_add_node(frothy_ir_program_t *program,
                                             const frothy_ir_node_t *node,
                                             frothy_ir_node_id_t *node_id_out) {
  FROTH_TRY(frothy_program_reserve_nodes(program, 1));
  *node_id_out = program->node_count;
  program->nodes[program->node_count++] = *node;
  return FROTH_OK;
}

static froth_error_t frothy_program_append_link(frothy_ir_program_t *program,
                                                frothy_ir_node_id_t node_id) {
  FROTH_TRY(frothy_program_reserve_links(program, 1));
  program->links[program->link_count++] = node_id;
  return FROTH_OK;
}

static void frothy_node_list_free(frothy_node_list_t *list) {
  free(list->items);
  memset(list, 0, sizeof(*list));
}

static froth_error_t frothy_node_list_push(frothy_node_list_t *list,
                                           frothy_ir_node_id_t node_id) {
  size_t needed = list->count + 1;
  size_t capacity = list->capacity == 0 ? 4 : list->capacity;
  frothy_ir_node_id_t *resized;

  while (capacity < needed) {
    capacity *= 2;
  }
  if (capacity != list->capacity) {
    resized = (frothy_ir_node_id_t *)realloc(list->items,
                                             capacity * sizeof(*list->items));
    if (resized == NULL) {
      return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
    }
    list->items = resized;
    list->capacity = capacity;
  }

  list->items[list->count++] = node_id;
  return FROTH_OK;
}

static froth_error_t frothy_node_list_flush(frothy_parser_t *parser,
                                            const frothy_node_list_t *list,
                                            size_t *first_out) {
  size_t i;

  *first_out = parser->program->link_count;
  for (i = 0; i < list->count; i++) {
    FROTH_TRY(frothy_program_append_link(parser->program, list->items[i]));
  }
  return FROTH_OK;
}

static froth_error_t frothy_make_literal_node(
    frothy_parser_t *parser, const frothy_ir_literal_t *literal,
    frothy_ir_node_id_t *node_id_out) {
  frothy_ir_literal_id_t literal_id;
  frothy_ir_node_t node;

  FROTH_TRY(frothy_program_add_literal(parser->program, literal, &literal_id));
  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_LIT;
  node.as.lit.literal_id = literal_id;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_int_literal_node(frothy_parser_t *parser,
                                                  froth_cell_t value,
                                                  frothy_ir_node_id_t *node_id_out) {
  frothy_ir_literal_t literal;

  memset(&literal, 0, sizeof(literal));
  literal.kind = FROTHY_IR_LITERAL_INT;
  literal.as.int_value = value;
  return frothy_make_literal_node(parser, &literal, node_id_out);
}

static froth_error_t frothy_make_bool_literal_node(
    frothy_parser_t *parser, bool value, frothy_ir_node_id_t *node_id_out) {
  frothy_ir_literal_t literal;

  memset(&literal, 0, sizeof(literal));
  literal.kind = FROTHY_IR_LITERAL_BOOL;
  literal.as.bool_value = value;
  return frothy_make_literal_node(parser, &literal, node_id_out);
}

static froth_error_t frothy_make_read_local_node(frothy_parser_t *parser,
                                                 size_t local_index,
                                                 frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_READ_LOCAL;
  node.as.read_local.local_index = local_index;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_write_local_node(
    frothy_parser_t *parser, size_t local_index, frothy_ir_node_id_t value,
    frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_WRITE_LOCAL;
  node.as.write_local.local_index = local_index;
  node.as.write_local.value = value;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_read_slot_node(frothy_parser_t *parser,
                                                char *slot_name,
                                                frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_READ_SLOT;
  node.as.read_slot.slot_name = slot_name;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_write_slot_node(
    frothy_parser_t *parser, char *slot_name, frothy_ir_node_id_t value,
    bool require_existing, frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_WRITE_SLOT;
  node.as.write_slot.slot_name = slot_name;
  node.as.write_slot.value = value;
  node.as.write_slot.require_existing = require_existing;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_read_index_node(
    frothy_parser_t *parser, frothy_ir_node_id_t base,
    frothy_ir_node_id_t index, frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_READ_INDEX;
  node.as.read_index.base = base;
  node.as.read_index.index = index;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_write_index_node(
    frothy_parser_t *parser, frothy_ir_node_id_t base,
    frothy_ir_node_id_t index, frothy_ir_node_id_t value,
    frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_WRITE_INDEX;
  node.as.write_index.base = base;
  node.as.write_index.index = index;
  node.as.write_index.value = value;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_fn_node(frothy_parser_t *parser, size_t arity,
                                         size_t local_count,
                                         frothy_ir_node_id_t body,
                                         frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_FN;
  node.as.fn.arity = arity;
  node.as.fn.local_count = local_count;
  node.as.fn.body = body;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_call_node(
    frothy_parser_t *parser, frothy_ir_builtin_kind_t builtin,
    frothy_ir_node_id_t callee, size_t first_arg, size_t arg_count,
    frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_CALL;
  node.as.call.builtin = builtin;
  node.as.call.callee = callee;
  node.as.call.first_arg = first_arg;
  node.as.call.arg_count = arg_count;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_if_node(
    frothy_parser_t *parser, frothy_ir_node_id_t condition,
    frothy_ir_node_id_t then_branch, bool has_else_branch,
    frothy_ir_node_id_t else_branch, frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_IF;
  node.as.if_expr.condition = condition;
  node.as.if_expr.then_branch = then_branch;
  node.as.if_expr.has_else_branch = has_else_branch;
  node.as.if_expr.else_branch = else_branch;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_while_node(
    frothy_parser_t *parser, frothy_ir_node_id_t condition,
    frothy_ir_node_id_t body, frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_WHILE;
  node.as.while_expr.condition = condition;
  node.as.while_expr.body = body;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_seq_node(frothy_parser_t *parser, size_t first,
                                          size_t count,
                                          frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_t node;

  memset(&node, 0, sizeof(node));
  node.kind = FROTHY_IR_NODE_SEQ;
  node.as.seq.first_item = first;
  node.as.seq.item_count = count;
  return frothy_program_add_node(parser->program, &node, node_id_out);
}

static froth_error_t frothy_make_single_item_seq_node(
    frothy_parser_t *parser, frothy_ir_node_id_t item,
    frothy_ir_node_id_t *node_id_out) {
  size_t first_item = parser->program->link_count;

  FROTH_TRY(frothy_program_append_link(parser->program, item));
  return frothy_make_seq_node(parser, first_item, 1, node_id_out);
}

static froth_error_t frothy_make_top_level_write_slot_range(
    frothy_parser_t *parser, const char *name_start, size_t name_length,
    frothy_ir_node_id_t value, frothy_ir_node_id_t *node_id_out) {
  char *slot_name = frothy_strdup_range(name_start, name_length);

  if (slot_name == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }
  return frothy_make_write_slot_node(parser, slot_name, value, false,
                                     node_id_out);
}

static froth_error_t frothy_make_top_level_write_slot_token(
    frothy_parser_t *parser, const frothy_token_t *name_token,
    frothy_ir_node_id_t value, frothy_ir_node_id_t *node_id_out) {
  return frothy_make_top_level_write_slot_range(parser, name_token->start,
                                                name_token->length, value,
                                                node_id_out);
}

static bool frothy_is_name_start(unsigned char byte) {
  return isalpha(byte) || byte == '_';
}

static bool frothy_is_name_continue(unsigned char byte) {
  return isalnum(byte) || byte == '_' || byte == '.';
}

static void frothy_lex_skip_space(frothy_parser_t *parser) {
  while (*parser->cursor != '\0' &&
         isspace((unsigned char)*parser->cursor)) {
    parser->cursor++;
  }
}

static froth_error_t frothy_lex_text(frothy_parser_t *parser,
                                     frothy_token_t *token) {
  char buffer[FROTH_STRING_MAX_LEN + 1];
  size_t length = 0;
  const char *start = parser->cursor;

  parser->cursor++;
  while (*parser->cursor != '\0' && *parser->cursor != '"') {
    unsigned char byte = (unsigned char)*parser->cursor++;

    if (byte == '\\') {
      unsigned char escape = (unsigned char)*parser->cursor++;

      if (escape == '\0') {
        return FROTH_ERROR_UNTERMINATED_STRING;
      }

      switch (escape) {
      case 'n':
        byte = '\n';
        break;
      case 'r':
        byte = '\r';
        break;
      case 't':
        byte = '\t';
        break;
      case '\\':
        byte = '\\';
        break;
      case '"':
        byte = '"';
        break;
      default:
        return FROTH_ERROR_INVALID_ESCAPE;
      }
    }

    if (length >= FROTH_STRING_MAX_LEN) {
      return FROTH_ERROR_BSTRING_TOO_LONG;
    }

    buffer[length++] = (char)byte;
  }

  if (*parser->cursor != '"') {
    return FROTH_ERROR_UNTERMINATED_STRING;
  }

  parser->cursor++;
  buffer[length] = '\0';

  token->kind = FROTHY_TOKEN_TEXT;
  token->start = start;
  token->length = (size_t)(parser->cursor - start);
  token->owned_text = frothy_strdup_range(buffer, length);
  if (token->owned_text == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }
  return FROTH_OK;
}

static froth_error_t frothy_lex_number(frothy_parser_t *parser,
                                       frothy_token_t *token) {
  uint64_t value = 0;
  const char *start = parser->cursor;

  while (isdigit((unsigned char)*parser->cursor)) {
    value = value * 10u + (uint64_t)(*parser->cursor - '0');
    if (value > (uint64_t)FROTH_MAX_CELL_VALUE) {
      return FROTH_ERROR_VALUE_OVERFLOW;
    }
    parser->cursor++;
  }

  token->kind = FROTHY_TOKEN_INT;
  token->start = start;
  token->length = (size_t)(parser->cursor - start);
  token->int_value = (froth_cell_t)value;
  return FROTH_OK;
}

static froth_error_t frothy_lex_name(frothy_parser_t *parser,
                                     frothy_token_t *token) {
  const char *start = parser->cursor;
  size_t length;

  parser->cursor++;
  while (frothy_is_name_continue((unsigned char)*parser->cursor)) {
    parser->cursor++;
  }

  length = (size_t)(parser->cursor - start);
  token->kind = FROTHY_TOKEN_NAME;
  token->start = start;
  token->length = length;

  if (length == 4 && strncmp(start, "here", 4) == 0) {
    token->kind = FROTHY_TOKEN_KW_HERE;
  } else if (length == 2 && strncmp(start, "is", 2) == 0) {
    token->kind = FROTHY_TOKEN_KW_IS;
  } else if (length == 3 && strncmp(start, "set", 3) == 0) {
    token->kind = FROTHY_TOKEN_KW_SET;
  } else if (length == 2 && strncmp(start, "to", 2) == 0) {
    token->kind = FROTHY_TOKEN_KW_TO;
  } else if (length == 4 && strncmp(start, "with", 4) == 0) {
    token->kind = FROTHY_TOKEN_KW_WITH;
  } else if (length == 2 && strncmp(start, "as", 2) == 0) {
    token->kind = FROTHY_TOKEN_KW_AS;
  } else if (length == 2 && strncmp(start, "fn", 2) == 0) {
    token->kind = FROTHY_TOKEN_KW_FN;
  } else if (length == 2 && strncmp(start, "if", 2) == 0) {
    token->kind = FROTHY_TOKEN_KW_IF;
  } else if (length == 4 && strncmp(start, "else", 4) == 0) {
    token->kind = FROTHY_TOKEN_KW_ELSE;
  } else if (length == 5 && strncmp(start, "while", 5) == 0) {
    token->kind = FROTHY_TOKEN_KW_WHILE;
  } else if (length == 6 && strncmp(start, "repeat", 6) == 0) {
    token->kind = FROTHY_TOKEN_KW_REPEAT;
  } else if (length == 4 && strncmp(start, "when", 4) == 0) {
    token->kind = FROTHY_TOKEN_KW_WHEN;
  } else if (length == 6 && strncmp(start, "unless", 6) == 0) {
    token->kind = FROTHY_TOKEN_KW_UNLESS;
  } else if (length == 4 && strncmp(start, "true", 4) == 0) {
    token->kind = FROTHY_TOKEN_KW_TRUE;
  } else if (length == 5 && strncmp(start, "false", 5) == 0) {
    token->kind = FROTHY_TOKEN_KW_FALSE;
  } else if (length == 3 && strncmp(start, "nil", 3) == 0) {
    token->kind = FROTHY_TOKEN_KW_NIL;
  } else if (length == 3 && strncmp(start, "not", 3) == 0) {
    token->kind = FROTHY_TOKEN_KW_NOT;
  } else if (length == 3 && strncmp(start, "and", 3) == 0) {
    token->kind = FROTHY_TOKEN_KW_AND;
  } else if (length == 2 && strncmp(start, "or", 2) == 0) {
    token->kind = FROTHY_TOKEN_KW_OR;
  } else if (length == 4 && strncmp(start, "call", 4) == 0) {
    token->kind = FROTHY_TOKEN_KW_CALL;
  } else if (length == 5 && strncmp(start, "cells", 5) == 0) {
    token->kind = FROTHY_TOKEN_KW_CELLS;
  }

  return FROTH_OK;
}

static froth_error_t frothy_lex_next(frothy_parser_t *parser,
                                     frothy_token_t *token) {
  memset(token, 0, sizeof(*token));
  frothy_lex_skip_space(parser);

  token->start = parser->cursor;
  if (*parser->cursor == '\0') {
    token->kind = FROTHY_TOKEN_EOF;
    return FROTH_OK;
  }

  if (frothy_is_name_start((unsigned char)*parser->cursor)) {
    return frothy_lex_name(parser, token);
  }
  if (isdigit((unsigned char)*parser->cursor)) {
    return frothy_lex_number(parser, token);
  }
  if (*parser->cursor == '"') {
    return frothy_lex_text(parser, token);
  }

  switch (*parser->cursor++) {
  case '(':
    token->kind = FROTHY_TOKEN_LPAREN;
    return FROTH_OK;
  case ')':
    token->kind = FROTHY_TOKEN_RPAREN;
    return FROTH_OK;
  case '{':
    token->kind = FROTHY_TOKEN_LBRACE;
    return FROTH_OK;
  case '}':
    token->kind = FROTHY_TOKEN_RBRACE;
    return FROTH_OK;
  case '[':
    token->kind = FROTHY_TOKEN_LBRACKET;
    return FROTH_OK;
  case ']':
    token->kind = FROTHY_TOKEN_RBRACKET;
    return FROTH_OK;
  case ',':
    token->kind = FROTHY_TOKEN_COMMA;
    return FROTH_OK;
  case ';':
    token->kind = FROTHY_TOKEN_SEMICOLON;
    return FROTH_OK;
  case ':':
    token->kind = FROTHY_TOKEN_COLON;
    return FROTH_OK;
  case '+':
    token->kind = FROTHY_TOKEN_PLUS;
    return FROTH_OK;
  case '-':
    token->kind = FROTHY_TOKEN_MINUS;
    return FROTH_OK;
  case '*':
    token->kind = FROTHY_TOKEN_STAR;
    return FROTH_OK;
  case '/':
    token->kind = FROTHY_TOKEN_SLASH;
    return FROTH_OK;
  case '%':
    token->kind = FROTHY_TOKEN_PERCENT;
    return FROTH_OK;
  case '=':
    if (*parser->cursor == '=') {
      parser->cursor++;
      token->kind = FROTHY_TOKEN_EQEQ;
    } else {
      token->kind = FROTHY_TOKEN_EQUAL;
    }
    return FROTH_OK;
  case '!':
    if (*parser->cursor == '=') {
      parser->cursor++;
      token->kind = FROTHY_TOKEN_NEQ;
      return FROTH_OK;
    }
    return FROTH_ERROR_SIGNATURE;
  case '<':
    if (*parser->cursor == '=') {
      parser->cursor++;
      token->kind = FROTHY_TOKEN_LE;
    } else {
      token->kind = FROTHY_TOKEN_LT;
    }
    return FROTH_OK;
  case '>':
    if (*parser->cursor == '=') {
      parser->cursor++;
      token->kind = FROTHY_TOKEN_GE;
    } else {
      token->kind = FROTHY_TOKEN_GT;
    }
    return FROTH_OK;
  default:
    return FROTH_ERROR_SIGNATURE;
  }
}

static froth_error_t frothy_parser_advance(frothy_parser_t *parser) {
  parser->prev_token_end =
      parser->current.start == NULL ? NULL
                                    : parser->current.start + parser->current.length;
  frothy_token_clear(&parser->current);
  parser->current = parser->next;
  memset(&parser->next, 0, sizeof(parser->next));
  return frothy_lex_next(parser, &parser->next);
}

static bool frothy_parser_match(frothy_parser_t *parser,
                                frothy_token_kind_t kind) {
  if (parser->current.kind != kind) {
    return false;
  }
  return true;
}

static froth_error_t frothy_parser_expect(frothy_parser_t *parser,
                                          frothy_token_kind_t kind) {
  if (!frothy_parser_match(parser, kind)) {
    return FROTH_ERROR_SIGNATURE;
  }
  return frothy_parser_advance(parser);
}

static froth_error_t frothy_push_scope(frothy_parser_t *parser) {
  FROTH_TRY(frothy_parser_reserve_scopes(parser, 1));
  parser->scopes[parser->scope_count].binding_start = parser->binding_count;
  parser->scope_count++;
  return FROTH_OK;
}

static void frothy_pop_scope(frothy_parser_t *parser) {
  size_t start;
  size_t i;

  if (parser->scope_count == 0) {
    return;
  }

  start = parser->scopes[parser->scope_count - 1].binding_start;
  for (i = start; i < parser->binding_count; i++) {
    free(parser->bindings[i].name);
  }
  parser->binding_count = start;
  parser->scope_count--;
}

static froth_error_t frothy_push_frame(frothy_parser_t *parser,
                                       bool reject_outer_capture) {
  FROTH_TRY(frothy_parser_reserve_frames(parser, 1));
  parser->frames[parser->frame_count].scope_base = parser->scope_count;
  parser->frames[parser->frame_count].next_local_index = 0;
  parser->frames[parser->frame_count].reject_outer_capture =
      reject_outer_capture;
  parser->frame_count++;
  return FROTH_OK;
}

static frothy_frame_t *frothy_current_frame(frothy_parser_t *parser) {
  if (parser->frame_count == 0) {
    return NULL;
  }
  return &parser->frames[parser->frame_count - 1];
}

static void frothy_pop_frame(frothy_parser_t *parser) {
  if (parser->frame_count > 0) {
    parser->frame_count--;
  }
}

static froth_error_t frothy_declare_local(frothy_parser_t *parser,
                                          const frothy_token_t *name_token,
                                          size_t *local_index_out) {
  frothy_frame_t *frame = frothy_current_frame(parser);
  size_t scope_index;
  size_t i;
  char *name_copy;

  if (frame == NULL || parser->scope_count == 0 ||
      name_token->kind != FROTHY_TOKEN_NAME) {
    return FROTH_ERROR_SIGNATURE;
  }

  scope_index = parser->scope_count - 1;
  for (i = parser->binding_count; i > 0; i--) {
    const frothy_binding_t *binding = &parser->bindings[i - 1];

    if (binding->scope_index != scope_index) {
      break;
    }
    if (strlen(binding->name) == name_token->length &&
        strncmp(binding->name, name_token->start, name_token->length) == 0) {
      return FROTH_ERROR_SIGNATURE;
    }
  }

  name_copy = frothy_strdup_range(name_token->start, name_token->length);
  if (name_copy == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  FROTH_TRY(frothy_parser_reserve_bindings(parser, 1));
  parser->bindings[parser->binding_count].name = name_copy;
  parser->bindings[parser->binding_count].local_index = frame->next_local_index;
  parser->bindings[parser->binding_count].scope_index = scope_index;
  *local_index_out = frame->next_local_index;
  frame->next_local_index++;
  parser->binding_count++;
  return FROTH_OK;
}

static froth_error_t frothy_reserve_hidden_local(frothy_parser_t *parser,
                                                 size_t *local_index_out) {
  frothy_frame_t *frame = frothy_current_frame(parser);

  if (frame == NULL) {
    return FROTH_ERROR_SIGNATURE;
  }

  *local_index_out = frame->next_local_index;
  frame->next_local_index++;
  return FROTH_OK;
}

static froth_error_t frothy_resolve_name(frothy_parser_t *parser,
                                         const frothy_token_t *name_token,
                                         bool *is_local_out,
                                         size_t *local_index_out,
                                         char **slot_name_out) {
  const frothy_frame_t *frame = frothy_current_frame(parser);
  size_t i;

  *is_local_out = false;
  *slot_name_out = NULL;

  if (frame != NULL) {
    for (i = parser->binding_count; i > 0; i--) {
      const frothy_binding_t *binding = &parser->bindings[i - 1];

      if (binding->scope_index < frame->scope_base) {
        break;
      }

      if (strlen(binding->name) == name_token->length &&
          strncmp(binding->name, name_token->start, name_token->length) == 0) {
        *is_local_out = true;
        *local_index_out = binding->local_index;
        return FROTH_OK;
      }
    }

    if (frame->reject_outer_capture) {
      for (; i > 0; i--) {
        const frothy_binding_t *binding = &parser->bindings[i - 1];

        if (strlen(binding->name) == name_token->length &&
            strncmp(binding->name, name_token->start, name_token->length) == 0) {
          return FROTH_ERROR_SIGNATURE;
        }
      }
    }
  }

  *slot_name_out = frothy_strdup_range(name_token->start, name_token->length);
  if (*slot_name_out == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }
  return FROTH_OK;
}

static froth_error_t
frothy_parse_expr(frothy_parser_t *parser, bool allow_top_level_cells,
                  frothy_ir_node_id_t *node_id_out);

static froth_error_t
frothy_parse_expr_before_block(frothy_parser_t *parser,
                               bool allow_top_level_cells,
                               frothy_ir_node_id_t *node_id_out) {
  bool saved = parser->prefer_block_on_spaced_bracket;
  froth_error_t err;

  parser->prefer_block_on_spaced_bracket = true;
  err = frothy_parse_expr(parser, allow_top_level_cells, node_id_out);
  parser->prefer_block_on_spaced_bracket = saved;
  return err;
}

static bool frothy_token_is_block_start(frothy_token_kind_t kind) {
  return kind == FROTHY_TOKEN_LBRACE || kind == FROTHY_TOKEN_LBRACKET;
}

static frothy_token_kind_t
frothy_block_close_kind(frothy_token_kind_t open_kind) {
  if (open_kind == FROTHY_TOKEN_LBRACE) {
    return FROTHY_TOKEN_RBRACE;
  }
  return FROTHY_TOKEN_RBRACKET;
}

static froth_error_t
frothy_parser_expect_block_start(frothy_parser_t *parser,
                                 frothy_token_kind_t *close_kind_out) {
  if (!frothy_token_is_block_start(parser->current.kind)) {
    return FROTH_ERROR_SIGNATURE;
  }

  *close_kind_out = frothy_block_close_kind(parser->current.kind);
  return frothy_parser_advance(parser);
}

static froth_error_t frothy_parse_block(frothy_parser_t *parser,
                                        frothy_ir_node_id_t *node_id_out);
static froth_error_t
frothy_parse_block_contents_until(frothy_parser_t *parser,
                                  frothy_token_kind_t close_kind,
                                  frothy_ir_node_id_t *node_id_out);

static froth_error_t frothy_parse_parameter_list(frothy_parser_t *parser,
                                                 size_t *arity_out) {
  size_t arity = 0;
  size_t local_index = 0;

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_LPAREN));
  while (!frothy_parser_match(parser, FROTHY_TOKEN_RPAREN)) {
    frothy_token_t param_token;

    if (parser->current.kind != FROTHY_TOKEN_NAME) {
      return FROTH_ERROR_SIGNATURE;
    }

    param_token = parser->current;
    memset(&parser->current, 0, sizeof(parser->current));
    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_declare_local(parser, &param_token, &local_index));
    arity++;

    if (!frothy_parser_match(parser, FROTHY_TOKEN_COMMA)) {
      break;
    }
    FROTH_TRY(frothy_parser_advance(parser));
  }

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_RPAREN));
  *arity_out = arity;
  return FROTH_OK;
}

static froth_error_t frothy_parse_with_parameter_list(frothy_parser_t *parser,
                                                      size_t *arity_out) {
  size_t arity = 0;
  size_t local_index = 0;

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_WITH));
  while (1) {
    frothy_token_t param_token;

    if (parser->current.kind != FROTHY_TOKEN_NAME) {
      return FROTH_ERROR_SIGNATURE;
    }

    param_token = parser->current;
    memset(&parser->current, 0, sizeof(parser->current));
    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_declare_local(parser, &param_token, &local_index));
    arity++;

    if (!frothy_parser_match(parser, FROTHY_TOKEN_COMMA)) {
      break;
    }
    FROTH_TRY(frothy_parser_advance(parser));
  }

  *arity_out = arity;
  return FROTH_OK;
}

static bool frothy_token_is_binding_operator(frothy_token_kind_t kind) {
  return kind == FROTHY_TOKEN_EQUAL || kind == FROTHY_TOKEN_KW_IS;
}

static froth_error_t
frothy_parser_expect_binding_operator(frothy_parser_t *parser) {
  if (!frothy_token_is_binding_operator(parser->current.kind)) {
    return FROTH_ERROR_SIGNATURE;
  }
  return frothy_parser_advance(parser);
}

static bool frothy_token_is_set_operator(frothy_token_kind_t kind) {
  return kind == FROTHY_TOKEN_EQUAL || kind == FROTHY_TOKEN_KW_TO;
}

static froth_error_t frothy_parser_expect_set_operator(frothy_parser_t *parser) {
  if (!frothy_token_is_set_operator(parser->current.kind)) {
    return FROTH_ERROR_SIGNATURE;
  }
  return frothy_parser_advance(parser);
}

static froth_error_t frothy_parse_fn_node(
    frothy_parser_t *parser, frothy_fn_params_kind_t params_kind,
    frothy_fn_body_kind_t body_kind, frothy_ir_node_id_t *node_id_out) {
  froth_error_t err = FROTH_OK;
  size_t arity = 0;
  size_t local_count = 0;
  frothy_ir_node_id_t expr_body = FROTHY_IR_NODE_INVALID;
  frothy_ir_node_id_t body = FROTHY_IR_NODE_INVALID;
  bool frame_pushed = false;
  bool scope_pushed = false;

  err = frothy_push_frame(parser, true);
  if (err != FROTH_OK) {
    return err;
  }
  frame_pushed = true;

  err = frothy_push_scope(parser);
  if (err != FROTH_OK) {
    goto cleanup;
  }
  scope_pushed = true;

  if (params_kind == FROTHY_FN_PARAMS_LIST) {
    err = frothy_parse_parameter_list(parser, &arity);
  } else if (params_kind == FROTHY_FN_PARAMS_WITH) {
    err = frothy_parse_with_parameter_list(parser, &arity);
    if (err != FROTH_OK) {
      goto cleanup;
    }
  }

  if (body_kind == FROTHY_FN_BODY_BLOCK) {
    err = frothy_parse_block(parser, &body);
    if (err != FROTH_OK) {
      goto cleanup;
    }
  } else {
    err = frothy_parser_expect(parser, FROTHY_TOKEN_EQUAL);
    if (err != FROTH_OK) {
      goto cleanup;
    }
    err = frothy_parse_expr(parser, false, &expr_body);
    if (err != FROTH_OK) {
      goto cleanup;
    }
    err = frothy_make_single_item_seq_node(parser, expr_body, &body);
    if (err != FROTH_OK) {
      goto cleanup;
    }
  }

  local_count = frothy_current_frame(parser)->next_local_index;

cleanup:
  if (scope_pushed) {
    frothy_pop_scope(parser);
  }
  if (frame_pushed) {
    frothy_pop_frame(parser);
  }

  if (err != FROTH_OK) {
    return err;
  }
  return frothy_make_fn_node(parser, arity, local_count, body, node_id_out);
}

static frothy_top_level_named_fn_kind_t
frothy_match_top_level_named_fn(frothy_parser_t *parser) {
  frothy_parser_t look = *parser;
  frothy_top_level_named_fn_kind_t kind = FROTHY_TOP_LEVEL_NAMED_FN_NONE;
  froth_error_t err;

  look.current.owned_text = NULL;
  look.next.owned_text = NULL;

  if (look.current.kind != FROTHY_TOKEN_NAME ||
      look.next.kind != FROTHY_TOKEN_LPAREN) {
    return FROTHY_TOP_LEVEL_NAMED_FN_NONE;
  }

  err = frothy_parser_advance(&look);
  if (err != FROTH_OK) {
    goto done;
  }
  err = frothy_parser_expect(&look, FROTHY_TOKEN_LPAREN);
  if (err != FROTH_OK) {
    goto done;
  }

  while (!frothy_parser_match(&look, FROTHY_TOKEN_RPAREN)) {
    if (look.current.kind != FROTHY_TOKEN_NAME) {
      goto done;
    }
    err = frothy_parser_advance(&look);
    if (err != FROTH_OK) {
      goto done;
    }
    if (!frothy_parser_match(&look, FROTHY_TOKEN_COMMA)) {
      break;
    }
    err = frothy_parser_advance(&look);
    if (err != FROTH_OK) {
      goto done;
    }
  }

  if (!frothy_parser_match(&look, FROTHY_TOKEN_RPAREN)) {
    goto done;
  }
  err = frothy_parser_advance(&look);
  if (err != FROTH_OK) {
    goto done;
  }

  if (look.current.kind == FROTHY_TOKEN_EQUAL) {
    kind = FROTHY_TOP_LEVEL_NAMED_FN_EXPR;
  } else if (look.current.kind == FROTHY_TOKEN_LBRACE) {
    kind = FROTHY_TOP_LEVEL_NAMED_FN_BLOCK;
  }

done:
  frothy_token_clear(&look.current);
  frothy_token_clear(&look.next);
  return kind;
}

static froth_error_t frothy_parse_top_level_named_fn(
    frothy_parser_t *parser, const frothy_token_t *name_token,
    frothy_fn_body_kind_t body_kind, frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t fn_node;

  FROTH_TRY(frothy_parse_fn_node(parser, FROTHY_FN_PARAMS_LIST, body_kind,
                                 &fn_node));
  return frothy_make_top_level_write_slot_token(parser, name_token, fn_node,
                                                node_id_out);
}

static froth_error_t frothy_parse_top_level_boot_block(
    frothy_parser_t *parser, frothy_ir_node_id_t *node_id_out) {
  static const char boot_name[] = "boot";
  frothy_ir_node_id_t fn_node;

  FROTH_TRY(frothy_parser_advance(parser));
  FROTH_TRY(frothy_parse_fn_node(parser, FROTHY_FN_PARAMS_NONE,
                                 FROTHY_FN_BODY_BLOCK, &fn_node));
  return frothy_make_top_level_write_slot_range(parser, boot_name,
                                                sizeof(boot_name) - 1, fn_node,
                                                node_id_out);
}

static froth_error_t frothy_parse_top_level_to(
    frothy_parser_t *parser, frothy_ir_node_id_t *node_id_out) {
  frothy_token_t name_token;
  frothy_fn_params_kind_t params_kind = FROTHY_FN_PARAMS_NONE;
  frothy_ir_node_id_t fn_node;

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_TO));
  if (parser->current.kind != FROTHY_TOKEN_NAME) {
    return FROTH_ERROR_SIGNATURE;
  }

  name_token = parser->current;
  memset(&parser->current, 0, sizeof(parser->current));
  FROTH_TRY(frothy_parser_advance(parser));
  if (parser->current.kind == FROTHY_TOKEN_KW_WITH) {
    params_kind = FROTHY_FN_PARAMS_WITH;
  }
  if (params_kind == FROTHY_FN_PARAMS_NONE &&
      !frothy_token_is_block_start(parser->current.kind)) {
    frothy_token_clear(&name_token);
    return FROTH_ERROR_SIGNATURE;
  }

  FROTH_TRY(
      frothy_parse_fn_node(parser, params_kind, FROTHY_FN_BODY_BLOCK, &fn_node));
  return frothy_make_top_level_write_slot_token(parser, &name_token, fn_node,
                                                node_id_out);
}

static bool frothy_token_is_binary_operator(frothy_token_kind_t kind) {
  switch (kind) {
  case FROTHY_TOKEN_PLUS:
  case FROTHY_TOKEN_MINUS:
  case FROTHY_TOKEN_STAR:
  case FROTHY_TOKEN_SLASH:
  case FROTHY_TOKEN_PERCENT:
  case FROTHY_TOKEN_LT:
  case FROTHY_TOKEN_LE:
  case FROTHY_TOKEN_GT:
  case FROTHY_TOKEN_GE:
  case FROTHY_TOKEN_EQEQ:
  case FROTHY_TOKEN_NEQ:
    return true;
  default:
    return false;
  }
}

static bool frothy_token_can_start_expr(frothy_token_kind_t kind) {
  switch (kind) {
  case FROTHY_TOKEN_NAME:
  case FROTHY_TOKEN_INT:
  case FROTHY_TOKEN_TEXT:
  case FROTHY_TOKEN_LPAREN:
  case FROTHY_TOKEN_MINUS:
  case FROTHY_TOKEN_KW_FN:
  case FROTHY_TOKEN_KW_IF:
  case FROTHY_TOKEN_KW_WHILE:
  case FROTHY_TOKEN_KW_WHEN:
  case FROTHY_TOKEN_KW_UNLESS:
  case FROTHY_TOKEN_KW_REPEAT:
  case FROTHY_TOKEN_KW_TRUE:
  case FROTHY_TOKEN_KW_FALSE:
  case FROTHY_TOKEN_KW_NIL:
  case FROTHY_TOKEN_KW_NOT:
  case FROTHY_TOKEN_KW_CALL:
  case FROTHY_TOKEN_KW_CELLS:
    return true;
  default:
    return false;
  }
}

static frothy_ir_builtin_kind_t
frothy_binary_builtin(frothy_token_kind_t kind) {
  switch (kind) {
  case FROTHY_TOKEN_PLUS:
    return FROTHY_IR_BUILTIN_ADD;
  case FROTHY_TOKEN_MINUS:
    return FROTHY_IR_BUILTIN_SUB;
  case FROTHY_TOKEN_STAR:
    return FROTHY_IR_BUILTIN_MUL;
  case FROTHY_TOKEN_SLASH:
    return FROTHY_IR_BUILTIN_DIV;
  case FROTHY_TOKEN_PERCENT:
    return FROTHY_IR_BUILTIN_REM;
  case FROTHY_TOKEN_LT:
    return FROTHY_IR_BUILTIN_LT;
  case FROTHY_TOKEN_LE:
    return FROTHY_IR_BUILTIN_LE;
  case FROTHY_TOKEN_GT:
    return FROTHY_IR_BUILTIN_GT;
  case FROTHY_TOKEN_GE:
    return FROTHY_IR_BUILTIN_GE;
  case FROTHY_TOKEN_EQEQ:
    return FROTHY_IR_BUILTIN_EQ;
  case FROTHY_TOKEN_NEQ:
    return FROTHY_IR_BUILTIN_NEQ;
  default:
    return FROTHY_IR_BUILTIN_NONE;
  }
}

static froth_error_t frothy_make_builtin_call(
    frothy_parser_t *parser, frothy_ir_builtin_kind_t builtin,
    frothy_ir_node_id_t *args, size_t arg_count,
    frothy_ir_node_id_t *node_id_out) {
  size_t first_arg = parser->program->link_count;
  size_t i;

  for (i = 0; i < arg_count; i++) {
    FROTH_TRY(frothy_program_append_link(parser->program, args[i]));
  }
  return frothy_make_call_node(parser, builtin, FROTHY_IR_NODE_INVALID,
                               first_arg, arg_count, node_id_out);
}

static froth_error_t frothy_parse_fn(frothy_parser_t *parser,
                                     frothy_ir_node_id_t *node_id_out) {
  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_FN));
  if (parser->current.kind == FROTHY_TOKEN_LPAREN) {
    return frothy_parse_fn_node(parser, FROTHY_FN_PARAMS_LIST,
                                FROTHY_FN_BODY_BLOCK, node_id_out);
  }
  if (parser->current.kind == FROTHY_TOKEN_KW_WITH) {
    return frothy_parse_fn_node(parser, FROTHY_FN_PARAMS_WITH,
                                FROTHY_FN_BODY_BLOCK, node_id_out);
  }
  if (frothy_token_is_block_start(parser->current.kind)) {
    return frothy_parse_fn_node(parser, FROTHY_FN_PARAMS_NONE,
                                FROTHY_FN_BODY_BLOCK, node_id_out);
  }

  return FROTH_ERROR_SIGNATURE;
}

static froth_error_t frothy_parse_if(frothy_parser_t *parser,
                                     frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t condition;
  frothy_ir_node_id_t then_branch;
  frothy_ir_node_id_t else_branch = FROTHY_IR_NODE_INVALID;
  bool has_else = false;

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_IF));
  FROTH_TRY(frothy_parse_expr_before_block(parser, false, &condition));
  FROTH_TRY(frothy_parse_block(parser, &then_branch));
  if (frothy_parser_match(parser, FROTHY_TOKEN_KW_ELSE)) {
    has_else = true;
    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_parse_block(parser, &else_branch));
  }

  return frothy_make_if_node(parser, condition, then_branch, has_else,
                             else_branch, node_id_out);
}

static froth_error_t frothy_parse_while(frothy_parser_t *parser,
                                        frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t condition;
  frothy_ir_node_id_t body;

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_WHILE));
  FROTH_TRY(frothy_parse_expr_before_block(parser, false, &condition));
  FROTH_TRY(frothy_parse_block(parser, &body));
  return frothy_make_while_node(parser, condition, body, node_id_out);
}

static froth_error_t frothy_parse_when(frothy_parser_t *parser,
                                       frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t condition;
  frothy_ir_node_id_t body;

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_WHEN));
  FROTH_TRY(frothy_parse_expr_before_block(parser, false, &condition));
  FROTH_TRY(frothy_parse_block(parser, &body));
  return frothy_make_if_node(parser, condition, body, false,
                             FROTHY_IR_NODE_INVALID, node_id_out);
}

static froth_error_t frothy_parse_unless(frothy_parser_t *parser,
                                         frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t condition;
  frothy_ir_node_id_t negated;
  frothy_ir_node_id_t body;

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_UNLESS));
  FROTH_TRY(frothy_parse_expr_before_block(parser, false, &condition));
  FROTH_TRY(frothy_make_builtin_call(parser, FROTHY_IR_BUILTIN_NOT,
                                     &condition, 1, &negated));
  FROTH_TRY(frothy_parse_block(parser, &body));
  return frothy_make_if_node(parser, negated, body, false,
                             FROTHY_IR_NODE_INVALID, node_id_out);
}

static froth_error_t
frothy_make_bool_condition_node(frothy_parser_t *parser,
                                frothy_ir_node_id_t condition,
                                frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t true_node;
  frothy_ir_node_id_t false_node;

  FROTH_TRY(frothy_make_bool_literal_node(parser, true, &true_node));
  FROTH_TRY(frothy_make_bool_literal_node(parser, false, &false_node));
  return frothy_make_if_node(parser, condition, true_node, true, false_node,
                             node_id_out);
}

static froth_error_t
frothy_make_short_circuit_node(frothy_parser_t *parser, bool is_or,
                               frothy_ir_node_id_t lhs,
                               frothy_ir_node_id_t rhs,
                               frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t true_node;
  frothy_ir_node_id_t false_node;
  frothy_ir_node_id_t rhs_bool;

  FROTH_TRY(frothy_make_bool_literal_node(parser, true, &true_node));
  FROTH_TRY(frothy_make_bool_literal_node(parser, false, &false_node));
  FROTH_TRY(frothy_make_bool_condition_node(parser, rhs, &rhs_bool));
  if (is_or) {
    return frothy_make_if_node(parser, lhs, true_node, true, rhs_bool,
                               node_id_out);
  }
  return frothy_make_if_node(parser, lhs, rhs_bool, true, false_node,
                             node_id_out);
}

static froth_error_t frothy_parse_repeat(frothy_parser_t *parser,
                                         frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t count_expr;
  frothy_token_t index_name;
  frothy_token_kind_t close_kind;
  frothy_ir_node_id_t body = FROTHY_IR_NODE_INVALID;
  frothy_ir_node_id_t limit_init;
  frothy_ir_node_id_t counter_init;
  frothy_ir_node_id_t loop_condition;
  frothy_ir_node_id_t while_body;
  frothy_ir_node_id_t while_node;
  frothy_ir_node_id_t limit_read;
  frothy_ir_node_id_t counter_read;
  frothy_ir_node_id_t zero_node;
  frothy_ir_node_id_t one_node;
  frothy_ir_node_id_t increment_value;
  frothy_ir_node_id_t increment_node;
  frothy_ir_node_id_t prelude_node = FROTHY_IR_NODE_INVALID;
  size_t limit_local = 0;
  size_t counter_local = 0;
  size_t index_local = 0;
  bool has_index = false;
  bool scope_pushed = false;
  froth_error_t err;
  frothy_node_list_t loop_items;
  frothy_node_list_t root_items;
  size_t first_item;
  size_t root_first;

  memset(&index_name, 0, sizeof(index_name));
  memset(&loop_items, 0, sizeof(loop_items));
  memset(&root_items, 0, sizeof(root_items));

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_REPEAT));
  FROTH_TRY(frothy_parse_expr_before_block(parser, false, &count_expr));
  if (parser->current.kind == FROTHY_TOKEN_KW_AS) {
    FROTH_TRY(frothy_parser_advance(parser));
    if (parser->current.kind != FROTHY_TOKEN_NAME) {
      return FROTH_ERROR_SIGNATURE;
    }
    has_index = true;
    index_name = parser->current;
    memset(&parser->current, 0, sizeof(parser->current));
    FROTH_TRY(frothy_parser_advance(parser));
  }

  FROTH_TRY(frothy_parser_expect_block_start(parser, &close_kind));
  FROTH_TRY(frothy_push_scope(parser));
  scope_pushed = true;
  if (has_index) {
    FROTH_TRY(frothy_declare_local(parser, &index_name, &index_local));
    frothy_token_clear(&index_name);
  }
  err = frothy_parse_block_contents_until(parser, close_kind, &body);
  if (scope_pushed) {
    frothy_pop_scope(parser);
    scope_pushed = false;
  }
  if (err != FROTH_OK) {
    frothy_token_clear(&index_name);
    return err;
  }

  FROTH_TRY(frothy_reserve_hidden_local(parser, &limit_local));
  FROTH_TRY(frothy_reserve_hidden_local(parser, &counter_local));
  FROTH_TRY(frothy_make_write_local_node(parser, limit_local, count_expr,
                                         &limit_init));
  FROTH_TRY(frothy_make_int_literal_node(parser, 0, &zero_node));
  FROTH_TRY(frothy_make_write_local_node(parser, counter_local, zero_node,
                                         &counter_init));

  FROTH_TRY(frothy_make_read_local_node(parser, counter_local, &counter_read));
  FROTH_TRY(frothy_make_read_local_node(parser, limit_local, &limit_read));
  {
    frothy_ir_node_id_t compare_args[2] = {counter_read, limit_read};
    FROTH_TRY(frothy_make_builtin_call(parser, FROTHY_IR_BUILTIN_LT,
                                       compare_args, 2, &loop_condition));
  }

  if (has_index) {
    frothy_ir_node_id_t counter_for_index;

    FROTH_TRY(
        frothy_make_read_local_node(parser, counter_local, &counter_for_index));
    FROTH_TRY(frothy_make_write_local_node(parser, index_local,
                                           counter_for_index, &prelude_node));
    FROTH_TRY(frothy_node_list_push(&loop_items, prelude_node));
  }
  FROTH_TRY(frothy_node_list_push(&loop_items, body));

  FROTH_TRY(frothy_make_int_literal_node(parser, 1, &one_node));
  {
    frothy_ir_node_id_t counter_for_add;
    frothy_ir_node_id_t add_args[2];

    FROTH_TRY(
        frothy_make_read_local_node(parser, counter_local, &counter_for_add));
    add_args[0] = counter_for_add;
    add_args[1] = one_node;
    FROTH_TRY(frothy_make_builtin_call(parser, FROTHY_IR_BUILTIN_ADD,
                                       add_args, 2, &increment_value));
  }
  FROTH_TRY(frothy_make_write_local_node(parser, counter_local,
                                         increment_value, &increment_node));
  FROTH_TRY(frothy_node_list_push(&loop_items, increment_node));

  FROTH_TRY(frothy_node_list_flush(parser, &loop_items, &first_item));
  FROTH_TRY(frothy_make_seq_node(parser, first_item, loop_items.count,
                                 &while_body));
  FROTH_TRY(
      frothy_make_while_node(parser, loop_condition, while_body, &while_node));

  FROTH_TRY(frothy_node_list_push(&root_items, limit_init));
  FROTH_TRY(frothy_node_list_push(&root_items, counter_init));
  FROTH_TRY(frothy_node_list_push(&root_items, while_node));
  FROTH_TRY(frothy_node_list_flush(parser, &root_items, &root_first));
  FROTH_TRY(
      frothy_make_seq_node(parser, root_first, root_items.count, node_id_out));

  frothy_node_list_free(&loop_items);
  frothy_node_list_free(&root_items);
  return FROTH_OK;
}

static froth_error_t frothy_parse_cells(frothy_parser_t *parser,
                                        bool allow_top_level_cells,
                                        frothy_ir_node_id_t *node_id_out) {
  frothy_ir_literal_t literal;
  frothy_ir_node_id_t arg_node;

  if (!allow_top_level_cells) {
    return FROTH_ERROR_TOPLEVEL_ONLY;
  }

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_CELLS));
  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_LPAREN));
  if (parser->current.kind != FROTHY_TOKEN_INT || parser->current.int_value <= 0) {
    return FROTH_ERROR_BOUNDS;
  }

  memset(&literal, 0, sizeof(literal));
  literal.kind = FROTHY_IR_LITERAL_INT;
  literal.as.int_value = parser->current.int_value;
  FROTH_TRY(frothy_make_literal_node(parser, &literal, &arg_node));
  FROTH_TRY(frothy_parser_advance(parser));
  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_RPAREN));
  return frothy_make_builtin_call(parser, FROTHY_IR_BUILTIN_CELLS, &arg_node, 1,
                                  node_id_out);
}

static froth_error_t frothy_parse_call_expr(frothy_parser_t *parser,
                                            frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t callee;
  frothy_node_list_t args;
  size_t first_arg = 0;
  size_t arg_count = 0;

  memset(&args, 0, sizeof(args));
  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_CALL));
  FROTH_TRY(frothy_parse_expr(parser, false, &callee));
  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_WITH));
  if (!frothy_token_can_start_expr(parser->current.kind)) {
    return FROTH_ERROR_SIGNATURE;
  }

  while (1) {
    frothy_ir_node_id_t arg_node;

    FROTH_TRY(frothy_parse_expr(parser, false, &arg_node));
    FROTH_TRY(frothy_node_list_push(&args, arg_node));
    if (!frothy_parser_match(parser, FROTHY_TOKEN_COMMA)) {
      break;
    }
    FROTH_TRY(frothy_parser_advance(parser));
  }

  FROTH_TRY(frothy_node_list_flush(parser, &args, &first_arg));
  arg_count = args.count;
  frothy_node_list_free(&args);
  return frothy_make_call_node(parser, FROTHY_IR_BUILTIN_NONE, callee,
                               first_arg, arg_count, node_id_out);
}

static froth_error_t frothy_parse_primary(frothy_parser_t *parser,
                                          bool allow_top_level_cells,
                                          frothy_ir_node_id_t *node_id_out) {
  frothy_ir_literal_t literal;
  frothy_token_t name_token;
  bool is_local;
  size_t local_index = 0;
  char *slot_name = NULL;

  memset(&literal, 0, sizeof(literal));

  switch (parser->current.kind) {
  case FROTHY_TOKEN_INT:
    literal.kind = FROTHY_IR_LITERAL_INT;
    literal.as.int_value = parser->current.int_value;
    FROTH_TRY(frothy_make_literal_node(parser, &literal, node_id_out));
    return frothy_parser_advance(parser);
  case FROTHY_TOKEN_TEXT:
    literal.kind = FROTHY_IR_LITERAL_TEXT;
    literal.as.text_value = frothy_strdup_range(parser->current.owned_text,
                                                strlen(parser->current.owned_text));
    if (literal.as.text_value == NULL) {
      return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
    }
    FROTH_TRY(frothy_make_literal_node(parser, &literal, node_id_out));
    return frothy_parser_advance(parser);
  case FROTHY_TOKEN_KW_TRUE:
    literal.kind = FROTHY_IR_LITERAL_BOOL;
    literal.as.bool_value = true;
    FROTH_TRY(frothy_make_literal_node(parser, &literal, node_id_out));
    return frothy_parser_advance(parser);
  case FROTHY_TOKEN_KW_FALSE:
    literal.kind = FROTHY_IR_LITERAL_BOOL;
    literal.as.bool_value = false;
    FROTH_TRY(frothy_make_literal_node(parser, &literal, node_id_out));
    return frothy_parser_advance(parser);
  case FROTHY_TOKEN_KW_NIL:
    literal.kind = FROTHY_IR_LITERAL_NIL;
    FROTH_TRY(frothy_make_literal_node(parser, &literal, node_id_out));
    return frothy_parser_advance(parser);
  case FROTHY_TOKEN_NAME:
    name_token = parser->current;
    memset(&parser->current, 0, sizeof(parser->current));
    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_resolve_name(parser, &name_token, &is_local, &local_index,
                                  &slot_name));
    frothy_token_clear(&name_token);
    if (is_local) {
      return frothy_make_read_local_node(parser, local_index, node_id_out);
    }
    return frothy_make_read_slot_node(parser, slot_name, node_id_out);
  case FROTHY_TOKEN_LPAREN:
    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_parse_expr(parser, false, node_id_out));
    return frothy_parser_expect(parser, FROTHY_TOKEN_RPAREN);
  case FROTHY_TOKEN_KW_FN:
    return frothy_parse_fn(parser, node_id_out);
  case FROTHY_TOKEN_KW_IF:
    return frothy_parse_if(parser, node_id_out);
  case FROTHY_TOKEN_KW_WHILE:
    return frothy_parse_while(parser, node_id_out);
  case FROTHY_TOKEN_KW_WHEN:
    return frothy_parse_when(parser, node_id_out);
  case FROTHY_TOKEN_KW_UNLESS:
    return frothy_parse_unless(parser, node_id_out);
  case FROTHY_TOKEN_KW_REPEAT:
    return frothy_parse_repeat(parser, node_id_out);
  case FROTHY_TOKEN_KW_CALL:
    return frothy_parse_call_expr(parser, node_id_out);
  case FROTHY_TOKEN_KW_CELLS:
    return frothy_parse_cells(parser, allow_top_level_cells, node_id_out);
  default:
    return FROTH_ERROR_SIGNATURE;
  }
}

static froth_error_t frothy_parse_postfix(frothy_parser_t *parser,
                                          bool allow_top_level_cells,
                                          frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t node_id;

  FROTH_TRY(frothy_parse_primary(parser, allow_top_level_cells, &node_id));

  while (parser->current.kind == FROTHY_TOKEN_LPAREN ||
         parser->current.kind == FROTHY_TOKEN_LBRACKET ||
         parser->current.kind == FROTHY_TOKEN_COLON) {
    if (parser->current.kind == FROTHY_TOKEN_LPAREN) {
      frothy_node_list_t args;
      size_t first_arg;

      memset(&args, 0, sizeof(args));

      FROTH_TRY(frothy_parser_advance(parser));
      while (!frothy_parser_match(parser, FROTHY_TOKEN_RPAREN)) {
        frothy_ir_node_id_t arg_node;

        FROTH_TRY(frothy_parse_expr(parser, false, &arg_node));
        FROTH_TRY(frothy_node_list_push(&args, arg_node));
        if (!frothy_parser_match(parser, FROTHY_TOKEN_COMMA)) {
          break;
        }
        FROTH_TRY(frothy_parser_advance(parser));
      }
      FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_RPAREN));
      FROTH_TRY(frothy_node_list_flush(parser, &args, &first_arg));
      FROTH_TRY(frothy_make_call_node(parser, FROTHY_IR_BUILTIN_NONE, node_id,
                                      first_arg, args.count, &node_id));
      frothy_node_list_free(&args);
      continue;
    }

    if (parser->current.kind == FROTHY_TOKEN_COLON) {
      frothy_node_list_t args;
      size_t first_arg = parser->program->link_count;

      memset(&args, 0, sizeof(args));
      FROTH_TRY(frothy_parser_advance(parser));
      if (frothy_token_can_start_expr(parser->current.kind)) {
        while (1) {
          frothy_ir_node_id_t arg_node;

          FROTH_TRY(frothy_parse_expr(parser, false, &arg_node));
          FROTH_TRY(frothy_node_list_push(&args, arg_node));
          if (!frothy_parser_match(parser, FROTHY_TOKEN_COMMA)) {
            break;
          }
          FROTH_TRY(frothy_parser_advance(parser));
        }
        FROTH_TRY(frothy_node_list_flush(parser, &args, &first_arg));
      }
      FROTH_TRY(frothy_make_call_node(parser, FROTHY_IR_BUILTIN_NONE, node_id,
                                      first_arg, args.count, &node_id));
      frothy_node_list_free(&args);
      continue;
    }

    if (parser->current.kind == FROTHY_TOKEN_LBRACKET &&
        parser->prefer_block_on_spaced_bracket &&
        parser->prev_token_end != parser->current.start) {
      break;
    }

    {
      frothy_ir_node_id_t index_node;

      FROTH_TRY(frothy_parser_advance(parser));
      FROTH_TRY(frothy_parse_expr(parser, false, &index_node));
      FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_RBRACKET));
      FROTH_TRY(
          frothy_make_read_index_node(parser, node_id, index_node, &node_id));
    }
  }

  *node_id_out = node_id;
  return FROTH_OK;
}

static froth_error_t frothy_parse_prefix(frothy_parser_t *parser,
                                         bool allow_top_level_cells,
                                         frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t operand;

  if (parser->current.kind == FROTHY_TOKEN_MINUS) {
    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_parse_prefix(parser, false, &operand));
    return frothy_make_builtin_call(parser, FROTHY_IR_BUILTIN_NEGATE, &operand, 1,
                                    node_id_out);
  }
  if (parser->current.kind == FROTHY_TOKEN_KW_NOT) {
    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_parse_prefix(parser, false, &operand));
    return frothy_make_builtin_call(parser, FROTHY_IR_BUILTIN_NOT, &operand, 1,
                                    node_id_out);
  }

  return frothy_parse_postfix(parser, allow_top_level_cells, node_id_out);
}

static froth_error_t frothy_parse_binary_expr(
    frothy_parser_t *parser, bool allow_top_level_cells,
    frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t lhs;

  FROTH_TRY(frothy_parse_prefix(parser, allow_top_level_cells, &lhs));
  while (frothy_token_is_binary_operator(parser->current.kind)) {
    frothy_ir_node_id_t rhs;
    frothy_ir_builtin_kind_t builtin =
        frothy_binary_builtin(parser->current.kind);
    frothy_ir_node_id_t args[2];

    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_parse_prefix(parser, false, &rhs));
    args[0] = lhs;
    args[1] = rhs;
    FROTH_TRY(frothy_make_builtin_call(parser, builtin, args, 2, &lhs));
  }

  *node_id_out = lhs;
  return FROTH_OK;
}

static froth_error_t frothy_parse_and_expr(frothy_parser_t *parser,
                                           bool allow_top_level_cells,
                                           frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t lhs;

  FROTH_TRY(frothy_parse_binary_expr(parser, allow_top_level_cells, &lhs));
  while (parser->current.kind == FROTHY_TOKEN_KW_AND) {
    frothy_ir_node_id_t rhs;

    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_parse_binary_expr(parser, false, &rhs));
    FROTH_TRY(
        frothy_make_short_circuit_node(parser, false, lhs, rhs, &lhs));
  }

  *node_id_out = lhs;
  return FROTH_OK;
}

static froth_error_t
frothy_parse_expr(frothy_parser_t *parser, bool allow_top_level_cells,
                  frothy_ir_node_id_t *node_id_out) {
  frothy_ir_node_id_t lhs;

  FROTH_TRY(frothy_parse_and_expr(parser, allow_top_level_cells, &lhs));
  while (parser->current.kind == FROTHY_TOKEN_KW_OR) {
    frothy_ir_node_id_t rhs;

    FROTH_TRY(frothy_parser_advance(parser));
    FROTH_TRY(frothy_parse_and_expr(parser, false, &rhs));
    FROTH_TRY(frothy_make_short_circuit_node(parser, true, lhs, rhs, &lhs));
  }

  *node_id_out = lhs;
  return FROTH_OK;
}

static froth_error_t frothy_parse_place(frothy_parser_t *parser,
                                        frothy_place_t *place_out) {
  frothy_ir_node_id_t expr;
  const frothy_ir_node_t *node;

  memset(place_out, 0, sizeof(*place_out));
  FROTH_TRY(frothy_parse_expr(parser, false, &expr));
  node = &parser->program->nodes[expr];

  if (node->kind == FROTHY_IR_NODE_READ_LOCAL) {
    place_out->kind = FROTHY_PLACE_LOCAL;
    place_out->as.local_index = node->as.read_local.local_index;
    return FROTH_OK;
  }
  if (node->kind == FROTHY_IR_NODE_READ_SLOT) {
    place_out->kind = FROTHY_PLACE_SLOT;
    place_out->as.slot_name =
        frothy_strdup_range(node->as.read_slot.slot_name,
                            strlen(node->as.read_slot.slot_name));
    if (place_out->as.slot_name == NULL) {
      return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
    }
    return FROTH_OK;
  }
  if (node->kind == FROTHY_IR_NODE_READ_INDEX) {
    place_out->kind = FROTHY_PLACE_INDEX;
    place_out->as.index.base = node->as.read_index.base;
    place_out->as.index.index = node->as.read_index.index;
    return FROTH_OK;
  }

  return FROTH_ERROR_SIGNATURE;
}

static void frothy_place_clear(frothy_place_t *place) {
  if (place->kind == FROTHY_PLACE_SLOT) {
    free(place->as.slot_name);
  }
  memset(place, 0, sizeof(*place));
}

static froth_error_t frothy_parse_set_item(frothy_parser_t *parser,
                                           frothy_ir_node_id_t *node_id_out) {
  frothy_place_t place;
  frothy_ir_node_id_t value;

  memset(&place, 0, sizeof(place));
  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_SET));
  FROTH_TRY(frothy_parse_place(parser, &place));
  FROTH_TRY(frothy_parser_expect_set_operator(parser));
  FROTH_TRY(frothy_parse_expr(parser, false, &value));

  switch (place.kind) {
  case FROTHY_PLACE_LOCAL:
    FROTH_TRY(
        frothy_make_write_local_node(parser, place.as.local_index, value,
                                     node_id_out));
    break;
  case FROTHY_PLACE_SLOT:
    FROTH_TRY(
        frothy_make_write_slot_node(parser, place.as.slot_name, value, true,
                                    node_id_out));
    place.as.slot_name = NULL;
    break;
  case FROTHY_PLACE_INDEX:
    FROTH_TRY(frothy_make_write_index_node(parser, place.as.index.base,
                                           place.as.index.index, value,
                                           node_id_out));
    break;
  }

  frothy_place_clear(&place);
  return FROTH_OK;
}

static froth_error_t frothy_parse_local_binding_item(
    frothy_parser_t *parser, const frothy_token_t *name_token,
    frothy_ir_node_id_t *node_id_out) {
  size_t local_index;
  frothy_ir_node_id_t value;

  FROTH_TRY(frothy_parser_expect_binding_operator(parser));
  FROTH_TRY(frothy_parse_expr(parser, false, &value));
  FROTH_TRY(frothy_declare_local(parser, name_token, &local_index));
  return frothy_make_write_local_node(parser, local_index, value, node_id_out);
}

static froth_error_t frothy_parse_here_item(frothy_parser_t *parser,
                                            frothy_ir_node_id_t *node_id_out) {
  frothy_token_t name_token;

  FROTH_TRY(frothy_parser_expect(parser, FROTHY_TOKEN_KW_HERE));
  if (parser->current.kind != FROTHY_TOKEN_NAME) {
    return FROTH_ERROR_SIGNATURE;
  }

  name_token = parser->current;
  memset(&parser->current, 0, sizeof(parser->current));
  FROTH_TRY(frothy_parser_advance(parser));
  return frothy_parse_local_binding_item(parser, &name_token, node_id_out);
}

static froth_error_t frothy_parse_block_item(frothy_parser_t *parser,
                                             frothy_ir_node_id_t *node_id_out) {
  if (parser->current.kind == FROTHY_TOKEN_KW_HERE) {
    return frothy_parse_here_item(parser, node_id_out);
  }

  if (parser->current.kind == FROTHY_TOKEN_NAME &&
      frothy_token_is_binding_operator(parser->next.kind)) {
    frothy_token_t name_token = parser->current;

    memset(&parser->current, 0, sizeof(parser->current));
    FROTH_TRY(frothy_parser_advance(parser));
    return frothy_parse_local_binding_item(parser, &name_token, node_id_out);
  }

  if (parser->current.kind == FROTHY_TOKEN_KW_SET) {
    return frothy_parse_set_item(parser, node_id_out);
  }

  return frothy_parse_expr(parser, false, node_id_out);
}

static froth_error_t
frothy_parse_block_contents_until(frothy_parser_t *parser,
                                  frothy_token_kind_t close_kind,
                                  frothy_ir_node_id_t *node_id_out) {
  frothy_node_list_t items;
  size_t first_item;

  memset(&items, 0, sizeof(items));

  while (!frothy_parser_match(parser, close_kind)) {
    frothy_ir_node_id_t item_node;

    if (frothy_parser_match(parser, FROTHY_TOKEN_EOF)) {
      return FROTH_ERROR_SIGNATURE;
    }
    if (frothy_parser_match(parser, FROTHY_TOKEN_SEMICOLON)) {
      FROTH_TRY(frothy_parser_advance(parser));
      continue;
    }

    FROTH_TRY(frothy_parse_block_item(parser, &item_node));
    FROTH_TRY(frothy_node_list_push(&items, item_node));
    while (frothy_parser_match(parser, FROTHY_TOKEN_SEMICOLON)) {
      FROTH_TRY(frothy_parser_advance(parser));
    }
  }

  FROTH_TRY(frothy_parser_expect(parser, close_kind));
  FROTH_TRY(frothy_node_list_flush(parser, &items, &first_item));
  FROTH_TRY(frothy_make_seq_node(parser, first_item, items.count, node_id_out));
  frothy_node_list_free(&items);
  return FROTH_OK;
}

static froth_error_t frothy_parse_block(frothy_parser_t *parser,
                                        frothy_ir_node_id_t *node_id_out) {
  frothy_token_kind_t close_kind;
  froth_error_t err;

  FROTH_TRY(frothy_parser_expect_block_start(parser, &close_kind));
  FROTH_TRY(frothy_push_scope(parser));
  err = frothy_parse_block_contents_until(parser, close_kind, node_id_out);
  frothy_pop_scope(parser);
  return err;
}

static void frothy_parser_cleanup(frothy_parser_t *parser) {
  while (parser->scope_count > 0) {
    frothy_pop_scope(parser);
  }
  free(parser->bindings);
  free(parser->scopes);
  free(parser->frames);
  frothy_token_clear(&parser->current);
  frothy_token_clear(&parser->next);
  memset(parser, 0, sizeof(*parser));
}

froth_error_t frothy_parse_top_level(const char *source,
                                     frothy_ir_program_t *program) {
  frothy_parser_t parser;
  froth_error_t err;
  frothy_ir_node_id_t root;
  frothy_top_level_named_fn_kind_t named_fn_kind;

  frothy_ir_program_init(program);
  memset(&parser, 0, sizeof(parser));
  parser.source = source;
  parser.cursor = source;
  parser.program = program;

  err = frothy_lex_next(&parser, &parser.current);
  if (err != FROTH_OK) {
    frothy_parser_cleanup(&parser);
    frothy_ir_program_free(program);
    return err;
  }
  err = frothy_lex_next(&parser, &parser.next);
  if (err != FROTH_OK) {
    frothy_parser_cleanup(&parser);
    frothy_ir_program_free(program);
    return err;
  }

  err = frothy_push_frame(&parser, false);
  if (err != FROTH_OK) {
    frothy_parser_cleanup(&parser);
    frothy_ir_program_free(program);
    return err;
  }

  named_fn_kind = frothy_match_top_level_named_fn(&parser);

  if (parser.current.kind == FROTHY_TOKEN_NAME &&
      frothy_token_is_binding_operator(parser.next.kind)) {
    frothy_token_t name_token = parser.current;
    frothy_ir_node_id_t value;

    memset(&parser.current, 0, sizeof(parser.current));
    err = frothy_parser_advance(&parser);
    if (err != FROTH_OK) {
      frothy_token_clear(&name_token);
      frothy_parser_cleanup(&parser);
      frothy_ir_program_free(program);
      return err;
    }
    err = frothy_parser_expect_binding_operator(&parser);
    if (err != FROTH_OK) {
      frothy_token_clear(&name_token);
      frothy_parser_cleanup(&parser);
      frothy_ir_program_free(program);
      return err;
    }

    if (parser.current.kind == FROTHY_TOKEN_KW_CELLS) {
      err = frothy_parse_cells(&parser, true, &value);
    } else {
      err = frothy_parse_expr(&parser, false, &value);
    }
    if (err != FROTH_OK) {
      frothy_token_clear(&name_token);
      frothy_parser_cleanup(&parser);
      frothy_ir_program_free(program);
      return err;
    }

    err = frothy_make_top_level_write_slot_token(&parser, &name_token, value,
                                                 &root);
    frothy_token_clear(&name_token);
  } else if (parser.current.kind == FROTHY_TOKEN_NAME &&
             parser.next.kind == FROTHY_TOKEN_LBRACE &&
             frothy_token_matches_name(&parser.current, "boot")) {
    err = frothy_parse_top_level_boot_block(&parser, &root);
  } else if (parser.current.kind == FROTHY_TOKEN_KW_TO) {
    err = frothy_parse_top_level_to(&parser, &root);
  } else if (named_fn_kind != FROTHY_TOP_LEVEL_NAMED_FN_NONE) {
    frothy_token_t name_token = parser.current;

    memset(&parser.current, 0, sizeof(parser.current));
    err = frothy_parser_advance(&parser);
    if (err != FROTH_OK) {
      frothy_token_clear(&name_token);
      frothy_parser_cleanup(&parser);
      frothy_ir_program_free(program);
      return err;
    }

    if (named_fn_kind == FROTHY_TOP_LEVEL_NAMED_FN_EXPR) {
      err = frothy_parse_top_level_named_fn(&parser, &name_token,
                                            FROTHY_FN_BODY_EXPR, &root);
    } else {
      err = frothy_parse_top_level_named_fn(&parser, &name_token,
                                            FROTHY_FN_BODY_BLOCK, &root);
    }
    frothy_token_clear(&name_token);
  } else if (parser.current.kind == FROTHY_TOKEN_KW_SET) {
    err = frothy_parse_set_item(&parser, &root);
  } else {
    err = frothy_parse_expr(&parser, false, &root);
  }

  if (err == FROTH_OK && parser.current.kind != FROTHY_TOKEN_EOF) {
    err = FROTH_ERROR_SIGNATURE;
  }

  if (err == FROTH_OK) {
    program->root = root;
    program->root_local_count = frothy_current_frame(&parser)->next_local_index;
  }

  frothy_parser_cleanup(&parser);
  if (err != FROTH_OK) {
    frothy_ir_program_free(program);
  }
  return err;
}
