#include "frothy_ir.h"
#include "frothy_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  const char *source;
  froth_error_t expected_error;
} negative_case_t;

static char *read_text_file(const char *path) {
  FILE *file = fopen(path, "rb");
  char *buffer;
  long length;

  if (file == NULL) {
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  length = ftell(file);
  if (length < 0) {
    fclose(file);
    return NULL;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  buffer = (char *)malloc((size_t)length + 1);
  if (buffer == NULL) {
    fclose(file);
    return NULL;
  }

  if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
    free(buffer);
    fclose(file);
    return NULL;
  }

  buffer[length] = '\0';
  fclose(file);
  return buffer;
}

static void trim_trailing_newline(char *text) {
  size_t length = strlen(text);

  while (length > 0 &&
         (text[length - 1] == '\n' || text[length - 1] == '\r')) {
    text[--length] = '\0';
  }
}

static int run_fixture_case(const char *stem) {
  char source_path[512];
  char expected_path[512];
  char *source;
  char *expected;
  char *rendered = NULL;
  frothy_ir_program_t program;
  froth_error_t err;
  int ok = 1;

  snprintf(source_path, sizeof(source_path), "%s/%s.frothy",
           FROTHY_PARSER_FIXTURE_DIR, stem);
  snprintf(expected_path, sizeof(expected_path), "%s/%s.ir",
           FROTHY_PARSER_FIXTURE_DIR, stem);

  source = read_text_file(source_path);
  expected = read_text_file(expected_path);
  if (source == NULL || expected == NULL) {
    fprintf(stderr, "fixture load failed: %s\n", stem);
    free(source);
    free(expected);
    return 0;
  }

  trim_trailing_newline(expected);
  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(source, &program);
  if (err != FROTH_OK) {
    fprintf(stderr, "parse failed for %s: %d\n", stem, (int)err);
    ok = 0;
    goto done;
  }

  err = frothy_ir_render(&program, &rendered);
  if (err != FROTH_OK) {
    fprintf(stderr, "render failed for %s: %d\n", stem, (int)err);
    ok = 0;
    goto done;
  }

  if (strcmp(rendered, expected) != 0) {
    fprintf(stderr, "fixture mismatch: %s\nexpected: %s\nactual:   %s\n", stem,
            expected, rendered);
    ok = 0;
  }

done:
  free(source);
  free(expected);
  free(rendered);
  frothy_ir_program_free(&program);
  return ok;
}

static int run_negative_case(const negative_case_t *test_case) {
  frothy_ir_program_t program;
  froth_error_t err;

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(test_case->source, &program);
  frothy_ir_program_free(&program);

  if (err != test_case->expected_error) {
    fprintf(stderr, "negative case %s expected %d, got %d\n", test_case->name,
            (int)test_case->expected_error, (int)err);
    return 0;
  }

  return 1;
}

static int run_render_code_case(const char *name, const char *source,
                                const char *expected) {
  frothy_ir_program_t program;
  frothy_ir_node_id_t value_node;
  char *rendered = NULL;
  froth_error_t err;
  int ok = 1;

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(source, &program);
  if (err != FROTH_OK) {
    fprintf(stderr, "render_code parse failed for %s: %d\n", name, (int)err);
    return 0;
  }

  if (program.root == FROTHY_IR_NODE_INVALID ||
      program.nodes[program.root].kind != FROTHY_IR_NODE_WRITE_SLOT) {
    fprintf(stderr, "render_code root was not a top-level slot write for %s\n",
            name);
    ok = 0;
    goto done;
  }

  value_node = program.nodes[program.root].as.write_slot.value;
  if (value_node >= program.node_count ||
      program.nodes[value_node].kind != FROTHY_IR_NODE_FN) {
    fprintf(stderr, "render_code value was not a function node for %s\n",
            name);
    ok = 0;
    goto done;
  }

  err = frothy_ir_render_code(
      &program, program.nodes[value_node].as.fn.body,
      program.nodes[value_node].as.fn.arity,
      program.nodes[value_node].as.fn.local_count, &rendered);
  if (err != FROTH_OK) {
    fprintf(stderr, "render_code failed for %s: %d\n", name, (int)err);
    ok = 0;
    goto done;
  }

  if (strcmp(rendered, expected) != 0) {
    fprintf(stderr,
            "render_code mismatch for %s\nexpected: %s\nactual:   %s\n", name,
            expected, rendered);
    ok = 0;
  }

done:
  free(rendered);
  frothy_ir_program_free(&program);
  return ok;
}

int main(void) {
  static const char *const fixture_cases[] = {
      "assign_int",
      "boot_block",
      "loop_fn",
      "loop_fn_here",
      "set_index_fn",
      "if_expr",
      "top_level_block_fn",
      "top_level_expr_fn",
      "top_level_expr_fn_text",
      "top_level_set_index",
      "words_call",
      "see_call",
      "core_call",
      "slot_info_call",
  };
  static const negative_case_t negative_cases[] = {
      {"cells_expr", "cells(2)", FROTH_ERROR_TOPLEVEL_ONLY},
      {"cells_zero", "frame = cells(0)", FROTH_ERROR_BOUNDS},
      {"capture", "outer = fn() { x = 1 fn() { x } }", FROTH_ERROR_SIGNATURE},
      {"nested_cells", "probe = fn() { cells(2) }", FROTH_ERROR_TOPLEVEL_ONLY},
      {"here_top_level", "here x = 1", FROTH_ERROR_SIGNATURE},
      {"bad_named_params", "bad(1) = 2", FROTH_ERROR_SIGNATURE},
      {"block_named_fn", "outer = fn() { helper(x) = x }", FROTH_ERROR_SIGNATURE},
      {"block_here_named_fn", "outer = fn() { here helper(x) = x }",
       FROTH_ERROR_SIGNATURE},
      {"boot_in_block", "outer = fn() { boot { nil } }",
       FROTH_ERROR_SIGNATURE},
  };
  static const char *const render_code_expected =
      "(fn arity=1 locals=1 (seq (call (builtin \"+\") (read-local 0) (lit 1))))";
  size_t i;
  int ok = 1;

  for (i = 0; i < sizeof(fixture_cases) / sizeof(fixture_cases[0]); i++) {
    ok &= run_fixture_case(fixture_cases[i]);
  }

  for (i = 0; i < sizeof(negative_cases) / sizeof(negative_cases[0]); i++) {
    ok &= run_negative_case(&negative_cases[i]);
  }

  ok &= run_render_code_case("fn_literal", "inc = fn(x) { x + 1 }",
                             render_code_expected);
  ok &= run_render_code_case("named_expr_sugar", "inc(x) = x + 1",
                             render_code_expected);

  return ok ? 0 : 1;
}
