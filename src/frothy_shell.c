#include "frothy_shell.h"

#include "frothy_base_image.h"
#include "frothy_control.h"
#include "frothy_eval.h"
#include "frothy_inspect.h"
#include "frothy_ir.h"
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
  bool trailing_comma;
  bool trailing_operator;
} frothy_input_state_t;

typedef enum {
  FROTHY_SHELL_COMMAND_NONE = 0,
  FROTHY_SHELL_COMMAND_HELP,
  FROTHY_SHELL_COMMAND_WORDS,
  FROTHY_SHELL_COMMAND_SAVE,
  FROTHY_SHELL_COMMAND_RESTORE,
  FROTHY_SHELL_COMMAND_WIPE,
  FROTHY_SHELL_COMMAND_SEE,
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

static const char *prompt_normal = "frothy> ";
static const char *prompt_cont = ".. ";
/* Keep large line buffers off the ESP32 main task stack. Frothy runs one
 * interactive shell at a time, so static storage matches the inherited
 * Froth REPL approach and avoids avoidable stack pressure during parse/eval. */
static char shell_line[FROTH_LINE_BUFFER_SIZE];
static char shell_command_buffer[FROTH_LINE_BUFFER_SIZE];
static bool shell_at_primary_prompt = false;

static froth_error_t frothy_emit_text(const char *text) {
  while (*text != '\0') {
    FROTH_TRY(platform_emit((uint8_t)*text));
    text++;
  }
  return FROTH_OK;
}

static bool frothy_is_name_start(unsigned char byte) {
  return isalpha(byte) || byte == '_';
}

static bool frothy_is_name_continue(unsigned char byte) {
  return isalnum(byte) || byte == '_' || byte == '.';
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
}

static void frothy_input_state_reset(frothy_input_state_t *state) {
  state->length = 0;
  state->paren_depth = 0;
  state->brace_depth = 0;
  state->bracket_depth = 0;
  state->in_string = false;
  state->trailing_equal = false;
  state->trailing_comma = false;
  state->trailing_operator = false;
  if (state->source != NULL) {
    state->source[0] = '\0';
  }
}

static void frothy_input_state_free(frothy_input_state_t *state) {
  free(state->source);
  memset(state, 0, sizeof(*state));
}

static bool frothy_input_state_has_pending(const frothy_input_state_t *state) {
  return state->length != 0;
}

static bool frothy_input_state_is_complete(const frothy_input_state_t *state) {
  return state->length != 0 && state->paren_depth == 0 &&
         state->brace_depth == 0 && state->bracket_depth == 0 &&
         !state->in_string && !state->trailing_equal &&
         !state->trailing_comma && !state->trailing_operator;
}

static froth_error_t
frothy_input_state_incomplete_error(const frothy_input_state_t *state) {
  if (state->in_string) {
    return FROTH_ERROR_UNTERMINATED_STRING;
  }
  return FROTH_ERROR_SIGNATURE;
}

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
  size_t capacity = state->capacity == 0 ? 64 : state->capacity;
  char *resized;

  if (needed <= state->capacity) {
    return FROTH_OK;
  }

  while (capacity < needed) {
    capacity *= 2;
  }

  resized = (char *)realloc(state->source, capacity);
  if (resized == NULL) {
    return FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  }

  state->source = resized;
  state->capacity = capacity;
  return FROTH_OK;
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
  size_t i = state->length;

  state->trailing_equal = false;
  state->trailing_comma = false;
  state->trailing_operator = false;

  if (state->in_string) {
    return;
  }

  while (i > 0 && isspace((unsigned char)state->source[i - 1])) {
    i--;
  }
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

  {
    size_t token_end = i;
    size_t token_start = token_end;

    while (token_start > 0 &&
           frothy_is_name_continue((unsigned char)state->source[token_start - 1])) {
      token_start--;
    }
    if (token_start < token_end &&
        token_end - token_start == 3 &&
        strncmp(state->source + token_start, "not", 3) == 0 &&
        (token_start == 0 ||
         !frothy_is_name_continue((unsigned char)state->source[token_start - 1]))) {
      state->trailing_operator = true;
      return;
    }
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
  FROTH_TRY(frothy_emit_text("see @name\n"));
  FROTH_TRY(frothy_emit_text("core @name\n"));
  FROTH_TRY(frothy_emit_text("info @name\n"));
  FROTH_TRY(frothy_emit_text("save\n"));
  FROTH_TRY(frothy_emit_text("restore\n"));
  FROTH_TRY(frothy_emit_text("wipe\n"));
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
    return "wipe";
  case FROTHY_SHELL_COMMAND_SEE:
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

static const char *frothy_shell_skip_spaces(const char *text) {
  while (*text != '\0' && isspace((unsigned char)*text)) {
    text++;
  }
  return text;
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
  } else if (strcmp(command, "save") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_SAVE;
  } else if (strcmp(command, "restore") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_RESTORE;
  } else if (strcmp(command, "wipe") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_WIPE;
  } else if (strcmp(command, ".control") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_CONTROL;
  } else if (strcmp(command, "quit") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_QUIT;
  } else if (strcmp(command, "exit") == 0) {
    result.kind = FROTHY_SHELL_COMMAND_EXIT;
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
    format = "words()";
    break;
  case FROTHY_SHELL_COMMAND_SAVE:
    format = "save()";
    break;
  case FROTHY_SHELL_COMMAND_RESTORE:
    format = "restore()";
    break;
  case FROTHY_SHELL_COMMAND_WIPE:
    format = "wipe()";
    break;
  case FROTHY_SHELL_COMMAND_SEE:
    format = "see(\"%.*s\")";
    break;
  case FROTHY_SHELL_COMMAND_CORE:
    format = "core(\"%.*s\")";
    break;
  case FROTHY_SHELL_COMMAND_INFO:
    format = "slotInfo(\"%.*s\")";
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

static froth_error_t frothy_print_eval_result(const char *source) {
  frothy_shell_eval_result_t result;
  const char *label = "eval";
  froth_error_t err = frothy_shell_eval_source(source, &result);

  if (err != FROTH_OK) {
    if (result.phase == FROTHY_SHELL_EVAL_PHASE_PARSE) {
      label = "parse";
    }
    frothy_shell_eval_result_free(&result);
    return frothy_emit_error(label, err);
  }

  if (!result.suppress_raw_output) {
    FROTH_TRY(frothy_emit_text(result.rendered));
    FROTH_TRY(platform_emit('\n'));
  }

  frothy_shell_eval_result_free(&result);
  return FROTH_OK;
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
    char *command = NULL;
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

    snprintf(shell_command_buffer, sizeof(shell_command_buffer), "%s",
             shell_line);
    command = frothy_trim_command(shell_command_buffer);
    if (*command == '\\' && !input.in_string) {
      continue;
    }

    if (!frothy_input_state_has_pending(&input)) {
      frothy_shell_command_t shell_command;

      shell_command = frothy_shell_parse_command(command);
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

    err = frothy_input_state_append_line(&input, shell_line);
    if (err != FROTH_OK) {
      result = err;
      goto cleanup;
    }
    if (!frothy_input_state_is_complete(&input)) {
      continue;
    }

    err = frothy_print_eval_result(input.source);
    frothy_input_state_reset(&input);
    if (err != FROTH_OK) {
      result = err;
      goto cleanup;
    }
  }

cleanup:
  shell_at_primary_prompt = false;
  frothy_input_state_free(&input);
  return result;
}
