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

static int run_render_surface_case(const char *name, const char *source,
                                   const char *expected) {
  frothy_ir_program_t program;
  const frothy_ir_node_t *root;
  frothy_ir_node_id_t value_node;
  char *rendered = NULL;
  froth_error_t err;
  int ok = 1;

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(source, &program);
  if (err != FROTH_OK) {
    fprintf(stderr, "render_surface parse failed for %s: %d\n", name, (int)err);
    return 0;
  }

  if (program.root == FROTHY_IR_NODE_INVALID) {
    fprintf(stderr, "render_surface root missing for %s\n", name);
    ok = 0;
    goto done;
  }

  root = &program.nodes[program.root];
  if (root->kind != FROTHY_IR_NODE_WRITE_SLOT) {
    fprintf(stderr, "render_surface root was not a slot write for %s\n", name);
    ok = 0;
    goto done;
  }

  value_node = root->as.write_slot.value;
  if (value_node >= program.node_count ||
      program.nodes[value_node].kind != FROTHY_IR_NODE_FN) {
    fprintf(stderr, "render_surface value was not a function for %s\n", name);
    ok = 0;
    goto done;
  }

  err = frothy_ir_render_surface_code(
      &program, program.nodes[value_node].as.fn.body,
      program.nodes[value_node].as.fn.arity,
      program.nodes[value_node].as.fn.local_count,
      root->as.write_slot.slot_name, &rendered);
  if (err != FROTH_OK) {
    fprintf(stderr, "render_surface failed for %s: %d\n", name, (int)err);
    ok = 0;
    goto done;
  }

  if (strcmp(rendered, expected) != 0) {
    fprintf(stderr,
            "render_surface mismatch for %s\nexpected: %s\nactual:   %s\n",
            name, expected, rendered);
    ok = 0;
  }

done:
  free(rendered);
  frothy_ir_program_free(&program);
  return ok;
}

static char *build_binding_capacity_source(size_t binding_count) {
  size_t capacity = 32 + (binding_count * 24);
  char *source = (char *)malloc(capacity);
  size_t cursor = 0;
  size_t i;

  if (source == NULL) {
    return NULL;
  }

  cursor += (size_t)snprintf(source + cursor, capacity - cursor,
                             "outer = fn() { ");
  for (i = 0; i < binding_count; i++) {
    cursor += (size_t)snprintf(source + cursor, capacity - cursor,
                               "local%zu = seed; ", i);
  }
  (void)snprintf(source + cursor, capacity - cursor, "seed }");
  return source;
}

static char *build_literal_capacity_source(size_t literal_count) {
  size_t capacity = 32 + (literal_count * 8);
  char *source = (char *)malloc(capacity);
  size_t cursor = 0;
  size_t i;

  if (source == NULL) {
    return NULL;
  }

  cursor += (size_t)snprintf(source + cursor, capacity - cursor,
                             "outer = fn() { ");
  for (i = 0; i < literal_count; i++) {
    cursor +=
        (size_t)snprintf(source + cursor, capacity - cursor, "%zu; ", i);
  }
  (void)snprintf(source + cursor, capacity - cursor, "0 }");
  return source;
}

static char *build_node_capacity_source(size_t item_count) {
  size_t capacity = 32 + (item_count * 8);
  char *source = (char *)malloc(capacity);
  size_t cursor = 0;
  size_t i;

  if (source == NULL) {
    return NULL;
  }

  cursor += (size_t)snprintf(source + cursor, capacity - cursor,
                             "outer = fn() { ");
  for (i = 0; i < item_count; i++) {
    cursor += (size_t)snprintf(source + cursor, capacity - cursor, "seed; ");
  }
  (void)snprintf(source + cursor, capacity - cursor, "seed }");
  return source;
}

static int run_capacity_case(const char *name, char *source,
                             froth_error_t expected_error) {
  frothy_ir_program_t program;
  froth_error_t err;
  int ok = 1;

  if (source == NULL) {
    fprintf(stderr, "capacity case %s failed to build source\n", name);
    return 0;
  }

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(source, &program);
  frothy_ir_program_free(&program);
  if (err != expected_error) {
    fprintf(stderr, "capacity case %s expected %d, got %d\n", name,
            (int)expected_error, (int)err);
    ok = 0;
  }

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level("unit = 1", &program);
  if (err != FROTH_OK) {
    fprintf(stderr, "capacity case %s did not recover: %d\n", name, (int)err);
    ok = 0;
  }
  frothy_ir_program_free(&program);
  free(source);
  return ok;
}

static int test_capacity_failures_recover(void) {
  int ok = 1;

  ok &= run_capacity_case("binding_cap",
                          build_binding_capacity_source(
                              FROTHY_PARSER_BINDING_CAPACITY + 1),
                          FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  ok &= run_capacity_case("literal_cap",
                          build_literal_capacity_source(
                              FROTHY_IR_LITERAL_CAPACITY + 1),
                          FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  ok &= run_capacity_case("node_link_cap",
                          build_node_capacity_source(FROTHY_IR_NODE_CAPACITY),
                          FROTH_ERROR_HEAP_OUT_OF_MEMORY);
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
      "spoken_to_with",
      "spoken_fn_with",
      "spoken_set_index_to",
      "spoken_when_expr",
      "spoken_unless_expr",
      "spoken_colon_call",
      "spoken_repeat_expr",
      "spoken_and_expr",
      "spoken_or_expr",
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
      {"repeat_missing_name", "repeat 3 as [ nil ]", FROTH_ERROR_SIGNATURE},
      {"to_missing_block", "to helper with x", FROTH_ERROR_SIGNATURE},
      {"call_missing_args", "call helper with", FROTH_ERROR_SIGNATURE},
  };
  static const char *const render_code_expected =
      "(fn arity=1 locals=1 (seq (call (builtin \"+\") (read-local 0) (lit 1))))";
  static const char *const render_surface_named_expected =
      "to inc with arg0 [ arg0 + 1 ]";
  static const char *const render_surface_set_expected =
      "to helper with arg0 [ set frame[0] to arg0; frame[0] ]";
  static const char *const render_surface_call_expected =
      "to invoke [ call makeInc: with 41 ]";
  static const char *const render_surface_repeat_expected =
      "to loopDemo [ repeat 3 as local0 [ local0 ] ]";
  static const char *const render_surface_and_expected =
      "to logicDemo with arg0, arg1 [ arg0 and arg1 ]";
  static const char *const render_surface_or_expected =
      "to logicOr with arg0, arg1 [ arg0 or arg1 ]";
  static const char *const render_surface_here_expected =
      "to scoped [ here local0 is 1; local0 ]";
  static const char *const render_surface_local_expected =
      "to localDemo [ here local0 is 6; local0 ]";
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
  ok &= run_render_surface_case("spoken_to", "to inc with x [ x + 1 ]",
                                render_surface_named_expected);
  ok &= run_render_surface_case(
      "spoken_set", "helper is fn with v [ set frame[0] to v; frame[0] ]",
      render_surface_set_expected);
  ok &= run_render_surface_case(
      "spoken_call_with", "invoke is fn [ call makeInc: with 41 ]",
      render_surface_call_expected);
  ok &= run_render_surface_case(
      "spoken_repeat_surface", "loopDemo is fn [ repeat 3 as i [ i ] ]",
      render_surface_repeat_expected);
  ok &= run_render_surface_case(
      "spoken_and_surface", "logicDemo is fn with x, y [ x and y ]",
      render_surface_and_expected);
  ok &= run_render_surface_case(
      "spoken_or_surface", "logicOr is fn with x, y [ x or y ]",
      render_surface_or_expected);
  ok &= run_render_surface_case(
      "spoken_here_surface", "scoped is fn [ here n is 1; n ]",
      render_surface_here_expected);
  ok &= run_render_surface_case(
      "spoken_local_surface", "localDemo is fn [ n is 6; n ]",
      render_surface_local_expected);
  ok &= test_capacity_failures_recover();

  return ok ? 0 : 1;
}
