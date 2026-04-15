#include "frothy_shell.h"

#include "frothy_base_image.h"
#include "frothy_control.h"
#include "frothy_eval.h"
#include "frothy_inspect.h"
#include "frothy_ir.h"
#include "frothy_name_rules.h"
#include "frothy_parser.h"
#include "frothy_value.h"
#include "froth_slot_table.h"
#include "froth_vm.h"
#include "platform.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *source;
  size_t length;
  size_t capacity;
  int paren_depth;
  int brace_depth;
  int bracket_depth;
  bool in_string;
  bool trailing_equal;
  bool trailing_keyword;
  bool trailing_comma;
  bool trailing_operator;
  bool trailing_named_code;
} frothy_input_state_t;

typedef enum {
  FROTHY_SHELL_COMMAND_NONE = 0,
  FROTHY_SHELL_COMMAND_HELP,
  FROTHY_SHELL_COMMAND_WORDS,
  FROTHY_SHELL_COMMAND_SAVE,
  FROTHY_SHELL_COMMAND_RESTORE,
  FROTHY_SHELL_COMMAND_WIPE,
  FROTHY_SHELL_COMMAND_SEE,
  FROTHY_SHELL_COMMAND_SHOW,
  FROTHY_SHELL_COMMAND_CORE,
  FROTHY_SHELL_COMMAND_INFO,
  FROTHY_SHELL_COMMAND_CONTROL,
  FROTHY_SHELL_COMMAND_QUIT,
  FROTHY_SHELL_COMMAND_EXIT,
} frothy_shell_command_kind_t;

typedef struct {
  frothy_shell_command_kind_t kind;
  const char *name_arg;
} frothy_shell_command_t;

static froth_error_t frothy_shell_prepare_input_line(
    frothy_input_state_t *input, const char *line, char *command_buffer,
    size_t command_capacity, char *rewritten_buffer, size_t rewritten_capacity,
    const char **command_out, const char **line_for_input_out,
    frothy_shell_command_t *shell_command_out);

static const char *prompt_normal = "frothy> ";
static const char *prompt_cont = ".. ";
/* Keep large line buffers off the ESP32 main task stack. Frothy runs one
 * interactive shell at a time, so static storage matches the inherited
 * Froth REPL approach and avoids avoidable stack pressure during parse/eval. */
static char shell_line[FROTH_LINE_BUFFER_SIZE];
static char shell_command_buffer[FROTH_LINE_BUFFER_SIZE];
static char shell_rewritten_command_buffer[FROTH_LINE_BUFFER_SIZE];
static char shell_pending_source[FROTHY_SHELL_SOURCE_CAPACITY];
static bool shell_at_primary_prompt = false;

static froth_error_t frothy_emit_text(const char *text) {
  while (*text != '\0') {
    FROTH_TRY(platform_emit((uint8_t)*text));
    text++;
  }
  return FROTH_OK;
}

static bool frothy_is_name_start(unsigned char byte) {
  return frothy_name_byte_is_start(byte);
}

static bool frothy_is_name_continue(unsigned char byte) {
  return frothy_name_byte_is_slot_continue(byte);
}

static bool frothy_word_equals(const char *start, size_t length,
                               const char *text) {
  return strlen(text) == length && strncmp(start, text, length) == 0;
}

static size_t frothy_trim_end_index(const char *text, size_t length) {
  while (length > 0 && isspace((unsigned char)text[length - 1])) {
    length--;
  }
  return length;
}

static const char *frothy_shell_skip_spaces(const char *text) {
  while (*text != '\0' && isspace((unsigned char)*text)) {
    text++;
  }
  return text;
}

static size_t frothy_segment_start(const char *text, size_t end) {
  while (end > 0) {
    if (text[end - 1] == ';') {
      return end;
    }
    end--;
  }
  return 0;
}

static bool frothy_find_prev_word(const char *text, size_t *cursor_io,
                                  const char **start_out,
                                  size_t *length_out) {
  size_t cursor = frothy_trim_end_index(text, *cursor_io);
  size_t end = cursor;

  while (cursor > 0 &&
         frothy_is_name_continue((unsigned char)text[cursor - 1])) {
    cursor--;
  }
  if (cursor == end || !frothy_is_name_start((unsigned char)text[cursor])) {
    return false;
  }

  *cursor_io = cursor;
  *start_out = text + cursor;
  *length_out = end - cursor;
  return true;
}

static const char *frothy_find_last_word(const char *start, const char *end,
                                         const char *word) {
  const char *last = NULL;
  const char *cursor = start;

  while (cursor < end) {
    const char *word_start = cursor;
    size_t word_length = 0;

    if (!frothy_is_name_start((unsigned char)*cursor)) {
      cursor++;
      continue;
    }

    cursor++;
    while (cursor < end && frothy_is_name_continue((unsigned char)*cursor)) {
      cursor++;
    }
    word_length = (size_t)(cursor - word_start);
    if (frothy_word_equals(word_start, word_length, word)) {
      last = word_start;
    }
  }

  return last;
}

static bool frothy_contains_leading_word(const char *start, const char *end,
                                         const char *word) {
  const char *cursor = frothy_shell_skip_spaces(start);
  const char *word_start = cursor;
  size_t word_length = 0;

  if (cursor >= end || !frothy_is_name_start((unsigned char)*cursor)) {
    return false;
  }
  cursor++;
  while (cursor < end && frothy_is_name_continue((unsigned char)*cursor)) {
    cursor++;
  }
  word_length = (size_t)(cursor - word_start);
  return frothy_word_equals(word_start, word_length, word);
}

static bool frothy_contains_call_separator(const char *start,
                                           const char *end) {
  const char *cursor = start;
  const char *prev_word_start = NULL;
  size_t prev_word_length = 0;

  while (cursor < end) {
    const char *word_start = cursor;
    size_t word_length = 0;

    if (!frothy_is_name_start((unsigned char)*cursor)) {
      cursor++;
      continue;
    }

    cursor++;
    while (cursor < end && frothy_is_name_continue((unsigned char)*cursor)) {
      cursor++;
    }
    word_length = (size_t)(cursor - word_start);
    if (frothy_word_equals(word_start, word_length, "with") &&
        !(prev_word_start != NULL &&
          frothy_word_equals(prev_word_start, prev_word_length, "fn"))) {
      return true;
    }

    prev_word_start = word_start;
    prev_word_length = word_length;
  }

  return false;
}

static const char *frothy_find_matching_bracket(const char *start,
                                                const char *end, char open,
                                                char close) {
  const char *cursor = start;
  int depth = 0;
  bool in_string = false;

  while (cursor < end) {
    if (in_string) {
      if (*cursor == '\\' && cursor + 1 < end) {
        cursor += 2;
        continue;
      }
      if (*cursor == '"') {
        in_string = false;
      }
      cursor++;
      continue;
    }

    if (*cursor == '"') {
      in_string = true;
      cursor++;
      continue;
    }
    if (*cursor == open) {
      depth++;
    } else if (*cursor == close) {
      depth--;
      if (depth == 0) {
        return cursor;
      }
    }
    cursor++;
  }

  return NULL;
}

static bool frothy_source_has_body_opener(const char *start, const char *end) {
  const char *cursor = start;

  while (cursor < end) {
    const char *match = NULL;
    const char *tail = NULL;
    char close = ']';

    if (*cursor != '[' && *cursor != '{') {
      cursor++;
      continue;
    }
    if (cursor != start && !isspace((unsigned char)cursor[-1])) {
      cursor++;
      continue;
    }
    if (*cursor == '{') {
      close = '}';
    }

    match = frothy_find_matching_bracket(cursor, end, *cursor, close);
    if (match == NULL) {
      cursor++;
      continue;
    }

    tail = frothy_shell_skip_spaces(match + 1);
    if (tail >= end || frothy_contains_call_separator(tail, end) ||
        frothy_contains_leading_word(tail, end, "else")) {
      return true;
    }
    cursor++;
  }

  return false;
}

static bool frothy_source_ends_with_named_code_header(const char *text,
                                                      size_t length) {
  const char *token_start = NULL;
  size_t token_length = 0;
  size_t cursor = frothy_trim_end_index(text, length);

  if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
    return false;
  }
  if (frothy_word_equals(token_start, token_length, "to")) {
    return true;
  }

  while (1) {
    size_t before = frothy_trim_end_index(text, cursor);

    if (before == 0 || text[before - 1] != ',') {
      break;
    }
    cursor = before - 1;
    if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
      return false;
    }
  }

  if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
    return false;
  }
  if (frothy_word_equals(token_start, token_length, "to")) {
    return true;
  }
  if (!frothy_word_equals(token_start, token_length, "with")) {
    return false;
  }

  if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
    return false;
  }
  if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
    return false;
  }
  return frothy_word_equals(token_start, token_length, "to");
}

static bool frothy_source_ends_with_function_literal_header(const char *text,
                                                            size_t length) {
  const char *token_start = NULL;
  size_t token_length = 0;
  size_t cursor = frothy_trim_end_index(text, length);

  if (cursor == 0) {
    return false;
  }
  if (text[cursor - 1] == ']' || text[cursor - 1] == '}') {
    return false;
  }
  if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
    return false;
  }
  if (frothy_word_equals(token_start, token_length, "fn")) {
    return true;
  }

  while (1) {
    size_t before = frothy_trim_end_index(text, cursor);

    if (before == 0 || text[before - 1] != ',') {
      break;
    }
    cursor = before - 1;
    if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
      return false;
    }
  }

  if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length) ||
      !frothy_word_equals(token_start, token_length, "with")) {
    return false;
  }
  if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
    return false;
  }
  return frothy_word_equals(token_start, token_length, "fn");
}

static bool frothy_source_ends_with_block_header(const char *text,
                                                 size_t length) {
  const char *end = text + frothy_trim_end_index(text, length);
  const char *segment = text + frothy_segment_start(text, (size_t)(end - text));
  const char *cursor;
  const char *word_start;
  size_t word_length;

  if (end == text) {
    return false;
  }
  if (end[-1] == ']' || end[-1] == '}') {
    return false;
  }

  cursor = frothy_shell_skip_spaces(segment);
  if (cursor >= end) {
    return false;
  }

  word_start = cursor;
  if (!frothy_is_name_start((unsigned char)*cursor)) {
    return false;
  }
  cursor++;
  while (cursor < end && frothy_is_name_continue((unsigned char)*cursor)) {
    cursor++;
  }
  word_length = (size_t)(cursor - word_start);
  if (!frothy_word_equals(word_start, word_length, "fn") &&
      !frothy_word_equals(word_start, word_length, "cond") &&
      !frothy_word_equals(word_start, word_length, "case") &&
      !frothy_word_equals(word_start, word_length, "in") &&
      !frothy_word_equals(word_start, word_length, "if") &&
      !frothy_word_equals(word_start, word_length, "record") &&
      !frothy_word_equals(word_start, word_length, "when") &&
      !frothy_word_equals(word_start, word_length, "unless") &&
      !frothy_word_equals(word_start, word_length, "repeat") &&
      !frothy_word_equals(word_start, word_length, "while")) {
    return false;
  }

  return !frothy_source_has_body_opener(cursor, end);
}

static bool frothy_source_ends_with_call_header(const char *text,
                                                size_t length) {
  const char *end = text + frothy_trim_end_index(text, length);
  const char *segment = text + frothy_segment_start(text, (size_t)(end - text));
  const char *call_start = frothy_find_last_word(segment, end, "call");

  if (call_start == NULL) {
    return false;
  }
  return !frothy_contains_call_separator(call_start + strlen("call"), end);
}

static bool frothy_word_in_list(const char *start, size_t length,
                                const char *const *words, size_t count) {
  size_t i;

  for (i = 0; i < count; i++) {
    if (frothy_word_equals(start, length, words[i])) {
      return true;
    }
  }
  return false;
}

static bool frothy_shell_parse_leading_name(const char *text,
                                            const char **start_out,
                                            size_t *length_out,
                                            const char **rest_out) {
  const char *cursor = text;

  if (!frothy_is_name_start((unsigned char)*cursor)) {
    return false;
  }

  cursor++;
  while (frothy_is_name_continue((unsigned char)*cursor)) {
    cursor++;
  }

  *start_out = text;
  *length_out = (size_t)(cursor - text);
  *rest_out = cursor;
  return true;
}

static bool frothy_shell_is_reserved_leader(const char *start, size_t length) {
  static const char *const reserved[] = {
      "and",      "as",       "boot",  "call",   "case", "cond", "core",
      "else",     "exit",     "false", "fn",     "help", "here", "if",
      "in",       "info",     "is",    "nil",    "not",  "or",   "quit",
      "record",   "remember",
      "repeat",   "restore",  "save",  "see",    "set",  "show",
      "to",       "true",     "unless","when",   "while","dangerous.wipe",
      "with",     "words",
  };

  return frothy_word_in_list(start, length, reserved,
                             sizeof(reserved) / sizeof(reserved[0]));
}

static bool frothy_shell_rest_starts_syntax_word(const char *text) {
  static const char *const reserved[] = {
      "and",   "as",   "boot", "call", "case", "cond", "else", "fn",
      "here",  "if",   "in",   "is",   "or",   "repeat", "set", "to",
      "unless","when", "while","with",
  };
  const char *start = frothy_shell_skip_spaces(text);
  const char *cursor = start;

  if (!frothy_is_name_start((unsigned char)*cursor)) {
    return false;
  }

  cursor++;
  while (frothy_is_name_continue((unsigned char)*cursor)) {
    cursor++;
  }

  return frothy_word_in_list(start, (size_t)(cursor - start), reserved,
                             sizeof(reserved) / sizeof(reserved[0]));
}

static bool frothy_shell_rewrite_simple_call(const char *command, char *buffer,
                                             size_t capacity) {
  const char *name_start = NULL;
  const char *rest = NULL;
  size_t name_length = 0;
  int needed;

  if (!frothy_shell_parse_leading_name(command, &name_start, &name_length,
                                       &rest)) {
    return false;
  }
  if (frothy_shell_is_reserved_leader(name_start, name_length)) {
    return false;
  }
  if (*rest != '\0' && !isspace((unsigned char)*rest)) {
    return false;
  }
  /* Dotted names are ordinary slot names, not prompt-level implicit calls. */
  if (memchr(name_start, '.', name_length) != NULL) {
    return false;
  }

  rest = frothy_shell_skip_spaces(rest);
  if (*rest == '\0') {
    return false;
  }

  if (frothy_shell_rest_starts_syntax_word(rest)) {
    return false;
  }
  if (*rest == '=' || *rest == '+' || *rest == '*' ||
      *rest == '/' || *rest == '%' || *rest == '<' || *rest == '>' ||
      *rest == '!' || *rest == ':') {
    return false;
  }
  if (*rest == '-' && !isdigit((unsigned char)rest[1])) {
    return false;
  }

  needed = snprintf(buffer, capacity, "%.*s: %s", (int)name_length,
                    name_start, rest);
  return needed >= 0 && (size_t)needed < capacity;
}

static char *frothy_trim_command(char *buffer) {
  char *start = buffer;
  char *end;

  while (*start != '\0' && isspace((unsigned char)*start)) {
    start++;
  }

  end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  *end = '\0';

  return start;
}

static void frothy_input_state_init(frothy_input_state_t *state) {
  memset(state, 0, sizeof(*state));
  state->source = shell_pending_source;
  state->capacity = sizeof(shell_pending_source);
  state->source[0] = '\0';
}

static void frothy_input_state_reset(frothy_input_state_t *state) {
  state->length = 0;
  state->paren_depth = 0;
  state->brace_depth = 0;
  state->bracket_depth = 0;
  state->in_string = false;
  state->trailing_equal = false;
  state->trailing_keyword = false;
  state->trailing_comma = false;
  state->trailing_operator = false;
  state->trailing_named_code = false;
  if (state->source != NULL) {
    state->source[0] = '\0';
  }
}

static void frothy_input_state_free(frothy_input_state_t *state) {
  frothy_input_state_reset(state);
  memset(state, 0, sizeof(*state));
}

static bool frothy_input_state_has_pending(const frothy_input_state_t *state) {
  return state->length != 0;
}

static bool frothy_input_state_is_complete(const frothy_input_state_t *state) {
  return state->length != 0 && state->paren_depth == 0 &&
         state->brace_depth == 0 && state->bracket_depth == 0 &&
         !state->in_string && !state->trailing_equal &&
         !state->trailing_keyword && !state->trailing_comma &&
         !state->trailing_operator && !state->trailing_named_code;
}

static froth_error_t
frothy_input_state_incomplete_error(const frothy_input_state_t *state) {
  if (state->in_string) {
    return FROTH_ERROR_UNTERMINATED_STRING;
  }
  return FROTH_ERROR_SIGNATURE;
}

static froth_error_t frothy_input_state_append_line(frothy_input_state_t *state,
                                                    const char *line);

static froth_error_t frothy_emit_error(const char *label, froth_error_t err) {
  char buffer[48];

  snprintf(buffer, sizeof(buffer), "%s error (%d)\n", label, (int)err);
  return frothy_emit_text(buffer);
}

void frothy_shell_eval_result_free(frothy_shell_eval_result_t *result) {
  if (result == NULL) {
    return;
  }

  free(result->rendered);
  (void)frothy_value_release(&froth_vm.frothy_runtime, result->value);
  memset(result, 0, sizeof(*result));
  result->value = frothy_value_make_nil();
}

static froth_error_t frothy_input_state_reserve(frothy_input_state_t *state,
                                                size_t extra) {
  size_t needed = state->length + extra + 1;

  if (needed <= state->capacity) {
    return FROTH_OK;
  }
  return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
}

static void frothy_scan_chunk(frothy_input_state_t *state, const char *chunk,
                              size_t length) {
  size_t i = 0;

  while (i < length) {
    char ch = chunk[i];

    if (state->in_string) {
      if (ch == '\\' && i + 1 < length) {
        i += 2;
        continue;
      }
      if (ch == '"') {
        state->in_string = false;
      }
      i++;
      continue;
    }

    if (ch == '"') {
      state->in_string = true;
      i++;
      continue;
    }

    if (ch == '(') {
      state->paren_depth++;
    } else if (ch == ')') {
      if (state->paren_depth > 0) {
        state->paren_depth--;
      }
    } else if (ch == '[') {
      state->bracket_depth++;
    } else if (ch == ']') {
      if (state->bracket_depth > 0) {
        state->bracket_depth--;
      }
    } else if (ch == '{') {
      state->brace_depth++;
    } else if (ch == '}') {
      if (state->brace_depth > 0) {
        state->brace_depth--;
      }
    }

    i++;
  }
}

static void frothy_update_trailing_state(frothy_input_state_t *state) {
  static const char *const keyword_words[] = {"as", "else", "is", "to",
                                              "with"};
  static const char *const operator_words[] = {"and", "not", "or"};
  size_t i = state->length;
  const char *token_start = NULL;
  size_t token_length = 0;
  size_t cursor;

  state->trailing_equal = false;
  state->trailing_keyword = false;
  state->trailing_comma = false;
  state->trailing_operator = false;
  state->trailing_named_code = false;

  if (state->in_string) {
    return;
  }

  i = frothy_trim_end_index(state->source, i);
  if (i == 0) {
    return;
  }

  if (state->source[i - 1] == '=') {
    if (i >= 2) {
      char prev = state->source[i - 2];

      if (prev == '=' || prev == '!' || prev == '<' || prev == '>') {
        state->trailing_operator = true;
        return;
      }
    }
    state->trailing_equal = true;
    return;
  }

  if (state->source[i - 1] == ',') {
    state->trailing_comma = true;
    return;
  }

  if (i >= 2) {
    char prev = state->source[i - 2];
    char last = state->source[i - 1];

    if ((prev == '<' || prev == '>' || prev == '!' || prev == '=') &&
        last == '=') {
      state->trailing_operator = true;
      return;
    }
  }

  if (state->source[i - 1] == '+' || state->source[i - 1] == '*' ||
      state->source[i - 1] == '/' || state->source[i - 1] == '%' ||
      state->source[i - 1] == '<' || state->source[i - 1] == '>') {
    state->trailing_operator = true;
    return;
  }

  if (state->source[i - 1] == '-') {
    state->trailing_operator = true;
    return;
  }

  cursor = i;
  if (frothy_find_prev_word(state->source, &cursor, &token_start,
                            &token_length)) {
    if (frothy_word_in_list(token_start, token_length, keyword_words,
                            sizeof(keyword_words) / sizeof(keyword_words[0]))) {
      state->trailing_keyword = true;
      return;
    }
    if (frothy_word_in_list(token_start, token_length, operator_words,
                            sizeof(operator_words) /
                                sizeof(operator_words[0]))) {
      state->trailing_operator = true;
      return;
    }
  }

  if (frothy_source_ends_with_named_code_header(state->source, i)) {
    state->trailing_named_code = true;
    return;
  }
  if (frothy_source_ends_with_function_literal_header(state->source, i)) {
    state->trailing_named_code = true;
    return;
  }
  if (frothy_source_ends_with_block_header(state->source, i)) {
    state->trailing_named_code = true;
    return;
  }
  if (frothy_source_ends_with_call_header(state->source, i)) {
    state->trailing_named_code = true;
  }
}

static bool frothy_source_ends_with_bare_colon_call(const char *text,
                                                    size_t length) {
  size_t end = frothy_trim_end_index(text, length);
  size_t cursor;
  const char *token_start = NULL;
  size_t token_length = 0;

  if (end == 0 || text[end - 1] != ':') {
    return false;
  }
  if (end >= 2 && text[end - 2] == ':') {
    return false;
  }

  cursor = end - 1;
  if (!frothy_find_prev_word(text, &cursor, &token_start, &token_length)) {
    return false;
  }
  return token_start + token_length == text + end - 1;
}

static froth_error_t frothy_input_state_append_line(frothy_input_state_t *state,
                                                    const char *line) {
  size_t length = strlen(line);

  FROTH_TRY(frothy_input_state_reserve(state, length + 1));
  memcpy(state->source + state->length, line, length);
  frothy_scan_chunk(state, line, length);
  state->length += length;
  state->source[state->length++] = '\n';
  state->source[state->length] = '\0';
  frothy_update_trailing_state(state);
  return FROTH_OK;
}

static froth_error_t frothy_emit_prompt(bool continued) {
  return frothy_emit_text(continued ? prompt_cont : prompt_normal);
}

bool frothy_shell_is_idle(void) { return shell_at_primary_prompt; }

#ifdef FROTHY_SHELL_TESTING
static frothy_input_state_t *frothy_shell_test_input_state(void) {
  static frothy_input_state_t state;

  if (state.source == NULL) {
    frothy_input_state_init(&state);
  }
  return &state;
}

froth_error_t frothy_shell_test_accept_line(const char *line) {
  frothy_input_state_t *input = frothy_shell_test_input_state();
  char command_buffer[FROTH_LINE_BUFFER_SIZE];
  char rewritten_buffer[FROTH_LINE_BUFFER_SIZE];
  const char *command = NULL;
  const char *line_for_input = line;
  frothy_shell_command_t shell_command;
  froth_error_t err;

  err = frothy_shell_prepare_input_line(
      input, line, command_buffer, sizeof(command_buffer), rewritten_buffer,
      sizeof(rewritten_buffer), &command, &line_for_input, &shell_command);
  if (err != FROTH_OK) {
    return err;
  }
  if (!frothy_input_state_has_pending(input) &&
      shell_command.kind != FROTHY_SHELL_COMMAND_NONE) {
    return FROTH_ERROR_SIGNATURE;
  }
  if (!frothy_input_state_has_pending(input) && *command == '\0') {
    return FROTH_OK;
  }

  return frothy_input_state_append_line(input, line_for_input);
}

void frothy_shell_test_reset_pending_source(void) {
  frothy_input_state_reset(frothy_shell_test_input_state());
}

froth_error_t frothy_shell_test_append_pending_line(const char *line) {
  return frothy_input_state_append_line(frothy_shell_test_input_state(), line);
}

const char *frothy_shell_test_pending_source(void) {
  return frothy_shell_test_input_state()->source;
}

size_t frothy_shell_test_pending_length(void) {
  return frothy_shell_test_input_state()->length;
}

bool frothy_shell_test_pending_is_complete(void) {
  return frothy_input_state_is_complete(frothy_shell_test_input_state());
}

bool frothy_shell_test_rewrite_simple_call(const char *command, char *buffer,
                                           size_t capacity) {
  return frothy_shell_rewrite_simple_call(command, buffer, capacity);
}
#endif

static froth_error_t frothy_emit_backspace(void) {
  FROTH_TRY(platform_emit('\b'));
  FROTH_TRY(platform_emit(' '));
  return platform_emit('\b');
}

static froth_error_t frothy_finish_read_interrupt(char *buffer, bool echo_input,
                                                  bool *interrupted_out) {
  froth_vm.interrupted = 0;
  *interrupted_out = true;
  if (echo_input) {
    FROTH_TRY(frothy_emit_text("^C\n"));
  }
  buffer[0] = '\0';
  return FROTH_OK;
}

static froth_error_t frothy_read_line(char *buffer, size_t capacity,
                                      bool echo_input, bool *eof_out,
                                      bool *interrupted_out) {
  size_t length = 0;

  *eof_out = false;
  *interrupted_out = false;

  while (1) {
    uint8_t byte;
    froth_error_t err = platform_key(&byte);

    if (err != FROTH_OK) {
      if (froth_vm.interrupted) {
        return frothy_finish_read_interrupt(buffer, echo_input,
                                            interrupted_out);
      }

      if (platform_input_closed() && length == 0) {
        *eof_out = true;
        buffer[0] = '\0';
        return FROTH_OK;
      }

      if (platform_input_closed()) {
        buffer[length] = '\0';
        return FROTH_OK;
      }

      return err;
    }

    if (byte == 0x03) {
      return frothy_finish_read_interrupt(buffer, echo_input,
                                          interrupted_out);
    }

    if (byte == '\r' || byte == '\n') {
      if (echo_input) {
        FROTH_TRY(platform_emit('\n'));
      }
      buffer[length] = '\0';
      return FROTH_OK;
    }

    if (byte == 0x7f || byte == 0x08) {
      if (length > 0) {
        length--;
        if (echo_input) {
          FROTH_TRY(frothy_emit_backspace());
        }
      }
      continue;
    }

    if (length + 1 < capacity) {
      buffer[length++] = (char)byte;
    }
    if (echo_input) {
      FROTH_TRY(platform_emit(byte));
    }
  }
}

static froth_error_t frothy_print_help(void) {
  FROTH_TRY(frothy_emit_text("help\n"));
  FROTH_TRY(frothy_emit_text("words\n"));
  FROTH_TRY(frothy_emit_text("show @name\n"));
  FROTH_TRY(frothy_emit_text("see @name\n"));
  FROTH_TRY(frothy_emit_text("core @name\n"));
  FROTH_TRY(frothy_emit_text("info @name\n"));
  FROTH_TRY(frothy_emit_text("remember\n"));
  FROTH_TRY(frothy_emit_text("save\n"));
  FROTH_TRY(frothy_emit_text("restore\n"));
  FROTH_TRY(frothy_emit_text("dangerous.wipe\n"));
  FROTH_TRY(frothy_emit_text(".control\n"));
  FROTH_TRY(frothy_emit_text("quit\n"));
  return frothy_emit_text("exit\n");
}

static froth_error_t frothy_print_words(void) {
  const char **names = NULL;
  size_t count = 0;
  size_t i;
  froth_error_t err;

  err = frothy_inspect_collect_words(&names, &count);
  if (err != FROTH_OK) {
    return err;
  }
  for (i = 0; i < count; i++) {
    err = frothy_emit_text(names[i]);
    if (err != FROTH_OK) {
      break;
    }
    err = platform_emit('\n');
    if (err != FROTH_OK) {
      break;
    }
  }
  frothy_inspect_free_words(names);

  return err;
}
static bool frothy_slot_emits_output(frothy_runtime_t *runtime,
                                     const char *slot_name) {
  froth_cell_u_t slot_index = 0;
  froth_cell_t impl = 0;
  const char *native_name = NULL;

  if (froth_slot_find_name(slot_name, &slot_index) != FROTH_OK) {
    return false;
  }
  if (froth_slot_get_impl(slot_index, &impl) != FROTH_OK) {
    return false;
  }
  if (frothy_runtime_get_native(runtime, frothy_value_from_cell(impl), NULL,
                                NULL,
                                &native_name, NULL) != FROTH_OK) {
    return false;
  }

  return frothy_base_image_builtin_emits_output(native_name);
}

static bool frothy_program_suppresses_output(frothy_runtime_t *runtime,
                                             const frothy_ir_program_t *program) {
  const frothy_ir_node_t *root;

  if (program->root == FROTHY_IR_NODE_INVALID) {
    return false;
  }

  root = &program->nodes[program->root];
  if (root->kind == FROTHY_IR_NODE_WRITE_SLOT) {
    return true;
  }
  if (root->kind != FROTHY_IR_NODE_CALL ||
      root->as.call.builtin != FROTHY_IR_BUILTIN_NONE ||
      root->as.call.callee >= program->node_count) {
    return false;
  }

  root = &program->nodes[root->as.call.callee];
  if (root->kind != FROTHY_IR_NODE_READ_SLOT) {
    return false;
  }

  return frothy_slot_emits_output(runtime, root->as.read_slot.slot_name);
}

static const char *
frothy_shell_command_builtin_name(frothy_shell_command_kind_t kind) {
  switch (kind) {
  case FROTHY_SHELL_COMMAND_WORDS:
    return "words";
  case FROTHY_SHELL_COMMAND_SAVE:
    return "save";
  case FROTHY_SHELL_COMMAND_RESTORE:
    return "restore";
  case FROTHY_SHELL_COMMAND_WIPE:
    return "dangerous.wipe";
  case FROTHY_SHELL_COMMAND_SEE:
  case FROTHY_SHELL_COMMAND_SHOW:
    return "see";
  case FROTHY_SHELL_COMMAND_CORE:
    return "core";
  case FROTHY_SHELL_COMMAND_INFO:
    return "slotInfo";
  default:
    return NULL;
  }
}

static bool frothy_shell_command_suppresses_raw_output(
    frothy_shell_command_kind_t kind) {
  const char *name = frothy_shell_command_builtin_name(kind);

  if (name == NULL) {
    return false;
  }

  return frothy_base_image_shell_suppresses_raw_output(name);
}

static bool frothy_shell_parse_name_arg(const char *text, const char **name_out,
                                        const char **end_out) {
  const char *cursor = text;

  if (*cursor != '@' || !frothy_is_name_start((unsigned char)cursor[1])) {
    return false;
  }

  cursor++;
  *name_out = cursor;
  cursor++;
  while (frothy_is_name_continue((unsigned char)*cursor)) {
    cursor++;
  }
  *end_out = cursor;
  return true;
}

static frothy_shell_command_t frothy_shell_parse_command(const char *command) {
  frothy_shell_command_t result;
  const char *name_text = NULL;
  const char *name_end = NULL;

  memset(&result, 0, sizeof(result));

  if (strcmp(command, "help") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_HELP;
  } else if (strcmp(command, "words") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_WORDS;
  } else if (strcmp(command, "remember") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_SAVE;
  } else if (strcmp(command, "save") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_SAVE;
  } else if (strcmp(command, "restore") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_RESTORE;
  } else if (strcmp(command, "dangerous.wipe") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_WIPE;
  } else if (strcmp(command, ".control") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_CONTROL;
  } else if (strcmp(command, "quit") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_QUIT;
  } else if (strcmp(command, "exit") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_EXIT;
  } else if (strncmp(command, "show", 4) == 0 &&
             (name_text = frothy_shell_skip_spaces(command + 4)) != command + 4 &&
             frothy_shell_parse_name_arg(name_text, &result.name_arg,
                                         &name_end) &&
             *name_end == '\0') {
    result.kind = FROTHY_SHELL_COMMAND_SHOW;
  } else if (strncmp(command, "see", 3) == 0 &&
             (name_text = frothy_shell_skip_spaces(command + 3)) != command + 3 &&
             frothy_shell_parse_name_arg(name_text, &result.name_arg,
                                         &name_end) &&
             *name_end == '\0') {
    result.kind = FROTHY_SHELL_COMMAND_SEE;
  } else if (strncmp(command, "core", 4) == 0 &&
             (name_text = frothy_shell_skip_spaces(command + 4)) != command + 4 &&
             frothy_shell_parse_name_arg(name_text, &result.name_arg,
                                         &name_end) &&
             *name_end == '\0') {
    result.kind = FROTHY_SHELL_COMMAND_CORE;
  } else if (strncmp(command, "info", 4) == 0 &&
             (name_text = frothy_shell_skip_spaces(command + 4)) != command + 4 &&
             frothy_shell_parse_name_arg(name_text, &result.name_arg,
                                         &name_end) &&
             *name_end == '\0') {
    result.kind = FROTHY_SHELL_COMMAND_INFO;
  }

  return result;
}

static froth_error_t frothy_shell_prepare_input_line(
    frothy_input_state_t *input, const char *line, char *command_buffer,
    size_t command_capacity, char *rewritten_buffer, size_t rewritten_capacity,
    const char **command_out, const char **line_for_input_out,
    frothy_shell_command_t *shell_command_out) {
  int written;

  *command_out = "";
  *line_for_input_out = line;
  memset(shell_command_out, 0, sizeof(*shell_command_out));

  written = snprintf(command_buffer, command_capacity, "%s", line);
  if (written < 0 || (size_t)written >= command_capacity) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  *command_out = frothy_trim_command(command_buffer);
  if (frothy_input_state_has_pending(input)) {
    return FROTH_OK;
  }

  *shell_command_out = frothy_shell_parse_command(*command_out);
  if (shell_command_out->kind == FROTHY_SHELL_COMMAND_NONE &&
      frothy_shell_rewrite_simple_call(*command_out, rewritten_buffer,
                                       rewritten_capacity)) {
    *command_out = rewritten_buffer;
    *line_for_input_out = rewritten_buffer;
  }

  return FROTH_OK;
}

static froth_error_t
frothy_shell_command_source(const frothy_shell_command_t *command,
                            char **source_out) {
  const char *format = NULL;
  size_t name_length = 0;
  size_t needed = 0;
  char *source = NULL;

  *source_out = NULL;
  if (command->name_arg != NULL) {
    name_length = strspn(command->name_arg,
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                         "0123456789_.");
    if (name_length == 0) {
      return FROTH_ERROR_SIGNATURE;
    }
  }

  switch (command->kind) {
  case FROTHY_SHELL_COMMAND_WORDS:
    format = "words:";
    break;
  case FROTHY_SHELL_COMMAND_SAVE:
    format = "save:";
    break;
  case FROTHY_SHELL_COMMAND_RESTORE:
    format = "restore:";
    break;
  case FROTHY_SHELL_COMMAND_WIPE:
    format = "dangerous.wipe:";
    break;
  case FROTHY_SHELL_COMMAND_SHOW:
  case FROTHY_SHELL_COMMAND_SEE:
    format = "see: @%.*s";
    break;
  case FROTHY_SHELL_COMMAND_CORE:
    format = "core: @%.*s";
    break;
  case FROTHY_SHELL_COMMAND_INFO:
    format = "slotInfo: @%.*s";
    break;
  default:
    return FROTH_ERROR_SIGNATURE;
  }

  if (command->name_arg == NULL) {
    needed = strlen(format);
  } else {
    needed = (size_t)snprintf(NULL, 0, format, (int)name_length,
                              command->name_arg);
  }

  source = (char *)malloc(needed + 1);
  if (source == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  if (command->name_arg == NULL) {
    memcpy(source, format, needed + 1);
  } else {
    snprintf(source, needed + 1, format, (int)name_length, command->name_arg);
  }

  *source_out = source;
  return FROTH_OK;
}

froth_error_t frothy_shell_eval_source(const char *source,
                                       frothy_shell_eval_result_t *out) {
  frothy_runtime_t *runtime = &froth_vm.frothy_runtime;
  frothy_ir_program_t program;
  froth_error_t err;

  memset(out, 0, sizeof(*out));
  out->value = frothy_value_make_nil();
  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(source, &program);
  if (err != FROTH_OK) {
    out->phase = FROTHY_SHELL_EVAL_PHASE_PARSE;
    frothy_ir_program_free(&program);
    return err;
  }

  out->suppress_raw_output = frothy_program_suppresses_output(runtime, &program);
  err = frothy_eval_program(&program, &out->value);
  if (err != FROTH_OK) {
    out->phase = FROTHY_SHELL_EVAL_PHASE_EVAL;
    frothy_ir_program_free(&program);
    return err;
  }

  err = frothy_value_render(runtime, out->value, &out->rendered);
  if (err != FROTH_OK) {
    out->phase = FROTHY_SHELL_EVAL_PHASE_EVAL;
    frothy_shell_eval_result_free(out);
    frothy_ir_program_free(&program);
    return err;
  }

  out->phase = FROTHY_SHELL_EVAL_PHASE_NONE;
  frothy_ir_program_free(&program);
  return FROTH_OK;
}

static bool frothy_shell_should_continue_after_eval_error(
    const char *source, froth_error_t err, frothy_shell_eval_phase_t phase) {
  if (phase != FROTHY_SHELL_EVAL_PHASE_EVAL || err != FROTH_ERROR_SIGNATURE) {
    return false;
  }
  return frothy_source_ends_with_bare_colon_call(source, strlen(source));
}

static froth_error_t
frothy_shell_run_command(const frothy_shell_command_t *command) {
  frothy_shell_eval_result_t result;
  char *source = NULL;
  const char *label = "eval";
  froth_error_t err;

  err = frothy_shell_command_source(command, &source);
  if (err != FROTH_OK) {
    if (err == FROTH_ERROR_SIGNATURE) {
      return frothy_emit_error("parse", err);
    }
    return frothy_emit_error("eval", err);
  }

  err = frothy_shell_eval_source(source, &result);
  free(source);
  if (err != FROTH_OK) {
    if (result.phase == FROTHY_SHELL_EVAL_PHASE_PARSE) {
      label = "parse";
    }
    frothy_shell_eval_result_free(&result);
    return frothy_emit_error(label, err);
  }

  if (!result.suppress_raw_output &&
      !frothy_shell_command_suppresses_raw_output(command->kind)) {
    FROTH_TRY(frothy_emit_text(result.rendered));
    FROTH_TRY(platform_emit('\n'));
  }

  frothy_shell_eval_result_free(&result);
  return FROTH_OK;
}

froth_error_t frothy_shell_run(void) {
  bool echo_input = platform_should_echo_input();
  frothy_input_state_t input;
  froth_error_t result = FROTH_OK;

  frothy_input_state_init(&input);
  shell_at_primary_prompt = false;

  while (1) {
    const char *command = NULL;
    const char *line_for_input = shell_line;
    frothy_shell_command_t shell_command;
    bool saw_eof;
    bool saw_interrupt;
    froth_error_t err;
    bool has_pending = frothy_input_state_has_pending(&input);

    err = frothy_emit_prompt(has_pending);
    if (err != FROTH_OK) {
      shell_at_primary_prompt = false;
      result = err;
      goto cleanup;
    }

    shell_at_primary_prompt = !has_pending;
    err = frothy_read_line(shell_line, sizeof(shell_line), echo_input, &saw_eof,
                           &saw_interrupt);
    shell_at_primary_prompt = false;
    if (err != FROTH_OK) {
      (void)frothy_emit_text("input error\n");
      result = err;
      goto cleanup;
    }

    if (saw_eof) {
      if (frothy_input_state_has_pending(&input)) {
        err = frothy_input_state_incomplete_error(&input);
        if (frothy_emit_error("parse", err) != FROTH_OK) {
          result = FROTH_ERROR_IO;
          goto cleanup;
        }
        result = err;
        goto cleanup;
      }
      err = platform_emit('\n');
      result = err == FROTH_OK ? FROTH_OK : err;
      goto cleanup;
    }

    if (saw_interrupt) {
      frothy_input_state_reset(&input);
      continue;
    }

    err = frothy_shell_prepare_input_line(
        &input, shell_line, shell_command_buffer, sizeof(shell_command_buffer),
        shell_rewritten_command_buffer, sizeof(shell_rewritten_command_buffer),
        &command, &line_for_input, &shell_command);
    if (err != FROTH_OK) {
      result = err;
      goto cleanup;
    }
    if (*command == '\\' && !input.in_string) {
      continue;
    }

    if (!frothy_input_state_has_pending(&input)) {
      switch (shell_command.kind) {
      case FROTHY_SHELL_COMMAND_NONE:
        break;
      case FROTHY_SHELL_COMMAND_HELP:
        err = frothy_print_help();
        if (err != FROTH_OK) {
          result = err;
          goto cleanup;
        }
        continue;
      case FROTHY_SHELL_COMMAND_CONTROL:
        err = frothy_emit_text("control: ready\n");
        if (err != FROTH_OK) {
          result = err;
          goto cleanup;
        }
        err = frothy_control_run();
        if (err != FROTH_OK) {
          result = err;
          goto cleanup;
        }
        continue;
      case FROTHY_SHELL_COMMAND_QUIT:
      case FROTHY_SHELL_COMMAND_EXIT:
        result = FROTH_OK;
        goto cleanup;
      default:
        err = frothy_shell_run_command(&shell_command);
        if (err != FROTH_OK) {
          result = err;
          goto cleanup;
        }
        continue;
      }
    }

    if (!frothy_input_state_has_pending(&input) && *command == '\0') {
      continue;
    }

    err = frothy_input_state_append_line(&input, line_for_input);
    if (err != FROTH_OK) {
      result = err;
      goto cleanup;
    }
    if (!frothy_input_state_is_complete(&input)) {
      continue;
    }

    {
      frothy_shell_eval_result_t eval_result;
      const char *label = "eval";

      err = frothy_shell_eval_source(input.source, &eval_result);
      if (err != FROTH_OK) {
        if (frothy_shell_should_continue_after_eval_error(
                input.source, err, eval_result.phase)) {
          frothy_shell_eval_result_free(&eval_result);
          continue;
        }
        if (eval_result.phase == FROTHY_SHELL_EVAL_PHASE_PARSE) {
          label = "parse";
        }
        frothy_shell_eval_result_free(&eval_result);
        frothy_input_state_reset(&input);
        err = frothy_emit_error(label, err);
        if (err != FROTH_OK) {
          result = err;
          goto cleanup;
        }
        continue;
      }

      if (!eval_result.suppress_raw_output) {
        FROTH_TRY(frothy_emit_text(eval_result.rendered));
        FROTH_TRY(platform_emit('\n'));
      }
      frothy_shell_eval_result_free(&eval_result);
      frothy_input_state_reset(&input);
    }
  }

cleanup:
  shell_at_primary_prompt = false;
  frothy_input_state_free(&input);
  return result;
}
