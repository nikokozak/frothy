#include "frothy_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect_text(const char *actual, const char *expected,
                       const char *label) {
  if (strcmp(actual, expected) != 0) {
    fprintf(stderr, "%s expected `%s`, got `%s`\n", label, expected, actual);
    return 0;
  }
  return 1;
}

static int test_multiline_pending_source(void) {
  int ok = 1;

  frothy_shell_test_reset_pending_source();
  ok &= frothy_shell_test_append_pending_line("helper = fn() {") == FROTH_OK;
  ok &= !frothy_shell_test_pending_is_complete();
  ok &= frothy_shell_test_append_pending_line("1 +") == FROTH_OK;
  ok &= !frothy_shell_test_pending_is_complete();
  ok &= frothy_shell_test_append_pending_line("2 }") == FROTH_OK;
  ok &= frothy_shell_test_pending_is_complete();
  ok &= expect_text(frothy_shell_test_pending_source(),
                    "helper = fn() {\n1 +\n2 }\n",
                    "pending multiline source");

  frothy_shell_test_reset_pending_source();
  if (frothy_shell_test_pending_length() != 0) {
    fprintf(stderr, "pending source reset expected length 0, got %zu\n",
            frothy_shell_test_pending_length());
    ok = 0;
  }
  ok &= expect_text(frothy_shell_test_pending_source(), "",
                    "pending source cleared");
  return ok;
}

static int test_pending_source_capacity(void) {
  char *line = NULL;
  size_t length = FROTHY_SHELL_SOURCE_CAPACITY - 2;
  int ok = 1;

  line = (char *)malloc(length + 1);
  if (line == NULL) {
    fprintf(stderr, "failed to allocate overflow line\n");
    return 0;
  }

  memset(line, 'a', length);
  line[length] = '\0';

  frothy_shell_test_reset_pending_source();
  ok &= frothy_shell_test_append_pending_line(line) == FROTH_OK;
  ok &= frothy_shell_test_append_pending_line("b") ==
        FROTH_ERROR_HEAP_OUT_OF_MEMORY;

  frothy_shell_test_reset_pending_source();
  if (frothy_shell_test_pending_length() != 0) {
    fprintf(stderr, "overflow reset expected length 0, got %zu\n",
            frothy_shell_test_pending_length());
    ok = 0;
  }

  free(line);
  return ok;
}

static int test_simple_call_rewrite_stays_bounded(void) {
  char rewritten[64];
  char *line = NULL;
  size_t rewritten_length;
  size_t length;
  int ok = 1;

  if (!frothy_shell_test_rewrite_simple_call("blink 1", rewritten,
                                             sizeof(rewritten))) {
    fprintf(stderr, "simple call rewrite failed\n");
    return 0;
  }
  ok &= expect_text(rewritten, "blink: 1", "rewritten command");

  rewritten_length = strlen(rewritten);
  length = FROTHY_SHELL_SOURCE_CAPACITY - rewritten_length - 2;
  line = (char *)malloc(length + 1);
  if (line == NULL) {
    fprintf(stderr, "failed to allocate rewrite fill line\n");
    return 0;
  }

  memset(line, 'a', length);
  line[length] = '\0';

  frothy_shell_test_reset_pending_source();
  ok &= frothy_shell_test_append_pending_line(line) == FROTH_OK;
  ok &= frothy_shell_test_append_pending_line(rewritten) ==
        FROTH_ERROR_HEAP_OUT_OF_MEMORY;
  frothy_shell_test_reset_pending_source();

  free(line);
  return ok;
}

int main(void) {
  int ok = 1;

  ok &= test_multiline_pending_source();
  ok &= test_pending_source_capacity();
  ok &= test_simple_call_rewrite_stays_bounded();
  return ok ? 0 : 1;
}
