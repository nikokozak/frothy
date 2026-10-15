/*
 * Unity tests for T16 bytes kind.
 *
 * D22 coverage: 7 natives happy path, text.pack happy + capacity,
 * slot reject, cell + record reject.
 * ADR 0085 coverage: loop checkpoints, the attempt, event and prompt
 * marks, the `set` refusal, hidden loop locals, the checkpoint opcodes on
 * both VM engines, and the checked build (FR_BYTES_CHECKED).
 * Migrated-native regression is covered by test_t15_net.c and
 * test_t15b_tcp.c (updated for bytes-shaped assertions per D23).
 */

#include "base_image.h"
#include "code.h"
#include "compile.h"
#include "image.h"
#include "instruction.h"
#include "object.h"
#include "persist.h"
#include "platform.h"
#include "repl.h"
#include "runtime.h"
#include "slot.h"
#include "tagged.h"
#include "types.h"
#include "vm.h"

#include "unity/unity.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void setUp(void) {
#if FR_FEATURE_NET
  fr_host_net_reset();
#endif
#if FR_FEATURE_PERSISTENCE
  (void)fr_platform_persist_clear();
  fr_host_persist_debug_direct_code_pointers(true);
#endif
}

void tearDown(void) {
#if FR_FEATURE_PERSISTENCE
  fr_host_persist_debug_direct_code_pointers(true);
#endif
}

#if FR_FEATURE_BYTES

static fr_runtime_t s_runtime;

static void install_base(void) {
  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
}

static void assert_no_bytes_in_use(void) {
  TEST_ASSERT_EQUAL_UINT8(0, s_runtime.bytes.entry_count);
  TEST_ASSERT_EQUAL_UINT16(0, s_runtime.bytes.arena_used);
}

static void eval_ok(const char *line) {
  char out[256];
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&s_runtime, line, out, sizeof(out)));
}

static fr_err_t eval_err(const char *line) {
  char out[256];
  return fr_repl_eval_line(&s_runtime, line, out, sizeof(out));
}

static void eval_expect(const char *line, const char *expected) {
  char out[256];
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&s_runtime, line, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(expected, out);
}

static void eval_error_expect(const char *line, fr_err_t err,
                              const char *expected) {
  char out[256];
  TEST_ASSERT_EQUAL(err, fr_repl_eval_line(&s_runtime, line, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(expected, out);
}

/* --- 7 natives happy path --- */

static void test_bytes_from_text(void) {
  install_base();
  eval_expect("bytes.length: bytes.from-text: \"hello\"", "5\nok\n");
}

static void test_bytes_from_byte(void) {
  install_base();
  eval_expect("bytes.length: bytes.from-byte: 10", "1\nok\n");
  eval_ok("to fb-test [ here b is bytes.from-byte: 65; bytes.at: b, 0 ]");
  eval_expect("fb-test:", "65\nok\n");
}

static void test_bytes_from_int(void) {
  install_base();
  eval_expect("text.pack: bytes.from-int: 42", "\"42\"\nok\n");
}

static void test_bytes_length(void) {
  install_base();
  eval_expect("bytes.length: bytes.from-text: \"abc\"", "3\nok\n");
  eval_error_expect("bytes.length: 5", FR_ERR_TYPE,
                    "error: wrong type: 5 (2)\n"
                    "bytes.length argument 1 expects bytes, got an int\n");
}

static void test_bytes_at(void) {
  install_base();
  eval_ok("to at-test [ here b is bytes.from-text: \"A\"; bytes.at: b, 0 ]");
  eval_expect("at-test:", "65\nok\n");
  eval_ok("to at-oob [ here b is bytes.from-text: \"A\"; bytes.at: b, 1 ]");
  eval_error_expect("at-oob:", FR_ERR_RANGE,
                    "error: out of range: 1 (1)\n"
                    "detail: bytes.at argument 2 was rejected\n");
}

static void test_bytes_equals_p(void) {
  install_base();
  eval_ok("to eq-test [ here a is bytes.from-text: \"hi\";"
          " here b is bytes.from-text: \"hi\"; bytes.equals?: a, b ]");
  eval_expect("eq-test:", "true\nok\n");
  eval_ok("to neq-test [ here a is bytes.from-text: \"hi\";"
          " here b is bytes.from-text: \"ho\"; bytes.equals?: a, b ]");
  eval_expect("neq-test:", "false\nok\n");
}

static void test_bytes_concat(void) {
  install_base();
  eval_ok("to cat-test [ here a is bytes.from-text: \"ab\";"
          " here b is bytes.from-text: \"cd\";"
          " text.pack: bytes.concat: a, b ]");
  eval_expect("cat-test:", "\"abcd\"\nok\n");
}

/* --- text.pack happy + capacity --- */

static void test_text_pack_happy(void) {
  install_base();
  eval_expect("text.pack: bytes.from-text: \"hello\"", "\"hello\"\nok\n");
}

static void test_text_pack_capacity(void) {
  fr_object_id_t oid;
  uint8_t buf[3];

  install_base();
  for (int i = 0; i < FR_PROFILE_OBJECT_TABLE_SIZE + 10; i++) {
    buf[0] = (uint8_t)(0x80 + (i & 0x3F));
    buf[1] = (uint8_t)(0xC0 + ((i >> 6) & 0x3F));
    buf[2] = (uint8_t)(i & 0xFF);
    if (fr_text_install(&s_runtime, buf, 3, &oid) != FR_OK) {
      break;
    }
  }
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY,
                    eval_err("text.pack: bytes.from-byte: 254"));
}

/* --- slot reject --- */

static void test_slot_rejects_bytes(void) {
  install_base();
  eval_error_expect("x is bytes.from-text: \"hi\"", FR_ERR_VOLATILE,
                    "error: not saved: bytes 2 (13)\n"
                    "detail: value cannot be stored in a slot\n");
}

/* --- cell + record reject --- */

static void test_cell_rejects_bytes(void) {
  install_base();
  eval_ok("c is cells: 1");
  eval_error_expect("set c[0] to bytes.from-text: \"hi\"", FR_ERR_VOLATILE,
                    "error: not saved: bytes 2 (13)\n"
                    "detail: value cannot be stored in cells\n");
}

#if FR_FEATURE_RECORDS
static void test_record_field_reject_reports_bytes(void) {
  install_base();
  eval_ok("record Point [ x ]");
  eval_ok("point is Point: 1");
  eval_error_expect("set point->x to bytes.from-text: \"secret\"",
                    FR_ERR_VOLATILE,
                    "error: not saved: bytes 6 (13)\n"
                    "detail: value cannot be stored in record fields\n");
  eval_expect("point->x", "1\nok\n");
}
#endif

static void test_record_field_rejects_bytes(void) {
  const uint8_t data[2] = {'h', 'i'};
  fr_tagged_t tagged = 0;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_install(&s_runtime, data, 2, &tagged));
  TEST_ASSERT_FALSE(fr_record_field_value_allowed(&s_runtime, tagged));
}

/* --- ADR 0085: the allocator --- */

static void test_rewind_makes_newer_values_stale(void) {
  const uint8_t data[2] = {'h', 'i'};
  fr_tagged_t tagged = 0;
  fr_bytes_ref_t ref = {0};
  const uint8_t *view = NULL;
  uint16_t view_len = 0;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_install(&s_runtime, data, 2, &tagged));
  TEST_ASSERT_EQUAL(FR_OK, fr_tagged_decode_bytes_ref(tagged, &ref));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_view(&s_runtime, ref, &view, &view_len));
  TEST_ASSERT_EQUAL_UINT16(2, view_len);

  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_rewind(&s_runtime, (fr_bytes_mark_t){0}));

  TEST_ASSERT_EQUAL(FR_ERR_VOLATILE,
                    fr_bytes_view(&s_runtime, ref, &view, &view_len));
}

static void test_rewind_refuses_a_mark_above_the_cursors(void) {
  const uint8_t data[1] = {'x'};
  fr_tagged_t tagged = 0;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_install(&s_runtime, data, 1, &tagged));
  TEST_ASSERT_EQUAL(FR_ERR_INVALID,
                    fr_bytes_rewind(&s_runtime,
                                    (fr_bytes_mark_t){.arena_used = 1,
                                                      .entry_count = 2}));
  TEST_ASSERT_EQUAL(FR_ERR_INVALID,
                    fr_bytes_rewind(&s_runtime,
                                    (fr_bytes_mark_t){.arena_used = 2,
                                                      .entry_count = 1}));
  TEST_ASSERT_EQUAL_UINT8(1, s_runtime.bytes.entry_count);
  TEST_ASSERT_EQUAL_UINT16(1, s_runtime.bytes.arena_used);
}

static void test_empty_value_takes_one_entry(void) {
  fr_tagged_t tagged = 0;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK, fr_bytes_install(&s_runtime, NULL, 0, &tagged));
  TEST_ASSERT_EQUAL_UINT8(1, s_runtime.bytes.entry_count);
  TEST_ASSERT_EQUAL_UINT16(0, s_runtime.bytes.arena_used);
}

static void test_failed_capacity_changes_nothing(void) {
  static uint8_t big[FR_PROFILE_BYTES_ARENA_BYTES];
  const uint8_t abc[3] = {'a', 'b', 'c'};
  fr_tagged_t first = 0;
  fr_tagged_t tagged = 0;
  fr_bytes_ref_t ref = {0};
  const uint8_t *view = NULL;
  uint16_t view_len = 0;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK, fr_bytes_install(&s_runtime, abc, 3, &first));
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY,
                    fr_bytes_install(&s_runtime, big,
                                     FR_PROFILE_BYTES_ARENA_BYTES - 2,
                                     &tagged));
  TEST_ASSERT_EQUAL_UINT8(1, s_runtime.bytes.entry_count);
  TEST_ASSERT_EQUAL_UINT16(3, s_runtime.bytes.arena_used);
  for (int i = 1; i < FR_PROFILE_BYTES_COUNT; i++) {
    TEST_ASSERT_EQUAL(FR_OK, fr_bytes_install(&s_runtime, abc, 1, &tagged));
  }
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY,
                    fr_bytes_install(&s_runtime, abc, 1, &tagged));
  TEST_ASSERT_EQUAL_UINT8(FR_PROFILE_BYTES_COUNT,
                          s_runtime.bytes.entry_count);
  TEST_ASSERT_EQUAL_UINT16(3 + FR_PROFILE_BYTES_COUNT - 1,
                           s_runtime.bytes.arena_used);
  TEST_ASSERT_EQUAL(FR_OK, fr_tagged_decode_bytes_ref(first, &ref));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_view(&s_runtime, ref, &view, &view_len));
  TEST_ASSERT_EQUAL_UINT16(3, view_len);
  TEST_ASSERT_EQUAL_MEMORY(abc, view, 3);
}

/* --- ADR 0085: loop checkpoints on both engines --- */

/* Compile one line, then save, so that the next line has the whole pending
 * area. */
static void define(const char *line) {
  eval_ok(line);
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));
}

/* Mount the saved image again. With no direct code pointers, the
 * flash-reader engine runs the saved words. */
static void remount(bool direct) {
  fr_host_persist_debug_direct_code_pointers(direct);
  install_base();
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore(&s_runtime));
}

static void assert_reader_runs(const char *word) {
  fr_slot_id_t slot_id = 0;
  fr_tagged_t tagged = 0;
  fr_code_object_id_t code_id = 0;
  fr_instruction_stream_t view = {0};

  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&s_runtime, word, &slot_id));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_read(&s_runtime, slot_id, &tagged));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_tagged_decode_code_object_id(tagged, &code_id));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_code_get_instructions(&s_runtime, code_id, &view));
  TEST_ASSERT_NULL(view.bytes);
}

/* Decision 5: each case runs on the contiguous engine, and then on the
 * flash-reader engine from the saved image. */
static void check_on_both_engines(void (*check)(void), const char *word) {
  check();
  remount(false);
  assert_reader_runs(word);
  check();
}

static void eval_error_contains(const char *line, fr_err_t err,
                                const char *text) {
  char out[256];

  TEST_ASSERT_EQUAL(err, fr_repl_eval_line(&s_runtime, line, out,
                                           sizeof(out)));
  TEST_ASSERT_NOT_NULL(strstr(out, text));
}

static void check_loop_frees_each_pass(void) {
  eval_expect("conv:", "1\nok\n");
  assert_no_bytes_in_use();
}

static void test_loop_frees_each_pass(void) {
  install_base();
  define("conv is fn [ here keep is bytes.from-text: \"k\"; "
         "repeat 10000 [ bytes.from-int: 7 ]; bytes.length: keep ]");
  check_on_both_engines(check_loop_frees_each_pass, "conv");
}

static void test_million_conversions_keep_the_cursors(void) {
  install_base();
  eval_ok("repeat 1000000 [ bytes.from-int: 1234567 ]");
  assert_no_bytes_in_use();
}

#if FR_FEATURE_I2C
static void test_i2c_read_loop_runs_2000_passes(void) {
  install_base();
  eval_ok("bus is i2c.open: 0, 21, 22, 100000");
  eval_ok("read-many is fn [ here n is 0; repeat 2000 [ "
          "here b is i2c.read: bus, 0x40, 4; "
          "set n to n + bytes.at: b, 3 ]; n ]");
  eval_expect("read-many:", "134000\nok\n");
  eval_ok("i2c.close: bus");
}
#endif

#if FR_FEATURE_EVENTS && FR_FEATURE_CELLS
static void check_event_body_loop(void) {
  eval_ok("set count[0] to 0");
  eval_ok("start:");
  eval_ok("repeat 1000 [ frothy.event-fire: \"every\", 50, nil ]");
  eval_expect("count[0]", "1000\nok\n");
}

static void test_event_body_loop_makes_nine_values(void) {
  install_base();
  define("count is cells: 1");
  define("start is fn [ every 50 [ repeat 9 [ bytes.from-byte: 1 ]; "
         "set count[0] to count[0] + 1 ] ]");
  check_on_both_engines(check_event_body_loop, "start");
}

/* The event body ends with one value outside its loop. The body frees it,
 * so `outer` fits its eight values. */
static void check_outer_values(void) {
  eval_ok("noise:");
  eval_expect("outer:", "true\nok\n");
}

static void test_outer_values_survive_loops_and_events(void) {
  install_base();
  define("noise is fn [ every 60 [ repeat 3 [ bytes.from-int: 99 ]; "
         "bytes.from-int: 98 ] ]");
  define("outer is fn [ here a is bytes.from-text: \"outer\"; "
         "repeat 3 [ repeat 3 [ bytes.from-int: 123 ] ]; "
         "frothy.event-fire: \"every\", 60, nil; "
         "here b1 is bytes.from-int: 1; here b2 is bytes.from-int: 2; "
         "here b3 is bytes.from-int: 3; here b4 is bytes.from-int: 4; "
         "here b5 is bytes.from-int: 5; here b6 is bytes.from-int: 6; "
         "bytes.equals?: a, bytes.from-text: \"outer\" ]");
  check_on_both_engines(check_outer_values, "outer");
}
#endif

/* --- ADR 0085 decision 4: the paths that could keep a value --- */

static void check_set_local_refuses_bytes(void) {
  eval_expect("keep-b:", "3\nok\n");
  eval_error_contains("replace-b:", FR_ERR_VOLATILE,
                      "error: not saved: bytes 2 (13)\n"
                      "detail: value cannot be stored in a local with set "
                      "-- use here in the loop body\n");
}

static void test_set_local_refuses_bytes(void) {
  install_base();
  define("keep-b is fn [ here b is bytes.from-text: \"abc\"; "
         "attempt [ set b to bytes.from-text: \"zz\" ] rescue [ 0 ]; "
         "bytes.length: b ]");
  define("replace-b is fn [ here b is 0; "
         "set b to bytes.from-text: \"zz\" ]");
  check_on_both_engines(check_set_local_refuses_bytes, "keep-b");
}

static void test_set_local_in_a_line_refuses_bytes(void) {
  install_base();
  eval_error_contains("here b is 0; set b to bytes.from-text: \"zz\"",
                      FR_ERR_VOLATILE,
                      "detail: value cannot be stored in a local with set "
                      "-- use here in the loop body\n");
}

static void check_here_scope_ends_with_its_pass(void) {
  eval_expect("after-loop:", "5\nok\n");
  eval_expect("next-pass:", "5\nok\n");
}

static void test_here_scope_ends_with_its_pass(void) {
  install_base();
  define("b is 5");
  define("after-loop is fn [ repeat 2 [ here b is bytes.from-int: 1 ]; b ]");
  define("next-pass is fn [ here n is 0; repeat 2 as i [ "
         "if i = 1 [ set n to b ]; here b is bytes.from-int: 9 ]; n ]");
  check_on_both_engines(check_here_scope_ends_with_its_pass, "next-pass");
}

static void check_values_made_before_a_loop(void) {
  eval_expect("keep-both: bytes.from-text: \"pp\"", "true\nok\n");
}

static void test_values_made_before_a_loop_survive(void) {
  install_base();
  define("keep-both is fn with p [ here q is bytes.from-text: \"qq\"; "
         "repeat 4 [ bytes.from-int: 77 ]; "
         "(bytes.equals?: p, bytes.from-text: \"pp\") and "
         "(bytes.equals?: q, bytes.from-text: \"qq\") ]");
  check_on_both_engines(check_values_made_before_a_loop, "keep-both");
}

static void test_set_of_a_name_refuses_bytes(void) {
  install_base();
  eval_ok("x is 0");
  eval_error_expect("set x to bytes.from-text: \"hi\"", FR_ERR_VOLATILE,
                    "error: not saved: bytes 2 (13)\n"
                    "detail: value cannot be stored in a slot\n");
}

static void check_word_result_in_the_caller_pass(void) {
  eval_expect("sum-mk:", "200\nok\n");
}

static void test_word_result_lives_in_the_caller_pass(void) {
  install_base();
  define("mk is fn [ bytes.from-text: \"xy\" ]");
  define("sum-mk is fn [ here n is 0; repeat 100 [ here b is mk:; "
         "set n to n + bytes.length: b ]; n ]");
  check_on_both_engines(check_word_result_in_the_caller_pass, "sum-mk");
}

#if FR_FEATURE_NET
static void test_tcp_read_loop_frees_each_pass(void) {
  uint8_t body[40];

  memset(body, 'x', sizeof(body));
  install_base();
  fr_host_tcp_queue_response(0, body, sizeof(body));
  eval_ok("sock is tcp.open: \"example.com\", 80");
  eval_ok("tcp.write: sock, \"GET\"");
  eval_ok("drain is fn [ here n is 0; repeat 40 [ "
          "here b is tcp.read: sock, 1; set n to n + bytes.length: b ]; n ]");
  eval_expect("drain:", "40\nok\n");
  eval_ok("tcp.close: sock");
}

static void test_http_value_is_freed_by_its_pass(void) {
  const uint8_t body[2] = {'h', 'i'};
  static fr_compile_expression_t expression;
  fr_tagged_t result = 0;

  install_base();
  eval_ok("url is \"http://example.com/\"");
  fr_host_wifi_set_connected(true);
  fr_host_http_queue_response(200, body, sizeof(body));
  TEST_ASSERT_EQUAL(FR_OK, fr_compile_expression_for_runtime(
                               &s_runtime, "repeat 1 [ http.get: url ]",
                               &expression));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_vm_run_instruction_stream(
                        &s_runtime, &expression.instructions, &result));
  assert_no_bytes_in_use();

  fr_host_http_queue_response(200, body, sizeof(body));
  TEST_ASSERT_EQUAL(FR_OK, fr_compile_expression_for_runtime(
                               &s_runtime, "http.get: url", &expression));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_vm_run_instruction_stream(
                        &s_runtime, &expression.instructions, &result));
  TEST_ASSERT_EQUAL_UINT8(1, s_runtime.bytes.entry_count);
}
#endif

/* --- ADR 0085: the read, decode and print loop --- */

static void test_read_decode_print_loop_runs_10000_passes(void) {
  static char capture[24000];
  static fr_object_table_t objects_before;

  install_base();
  memcpy(&objects_before, &s_runtime.objects, sizeof(objects_before));
  fr_host_capture_text(capture, (uint16_t)sizeof(capture));
  eval_ok("repeat 10000 [ here b is bytes.from-int: 1234; "
          "print: bytes.at: b, 0 ]");
  fr_host_capture_text(NULL, 0);
  TEST_ASSERT_EQUAL_size_t(20000, strlen(capture));
  TEST_ASSERT_EQUAL_MEMORY(&objects_before, &s_runtime.objects,
                           sizeof(objects_before));
}

/* --- ADR 0085: the false while condition, recursion and attempt --- */

static void check_false_while_conditions(void) {
  eval_expect("five-whiles:", "1\nok\n");
  eval_expect("while none: [ nil ]; while none: [ nil ]; "
              "while none: [ nil ]; while none: [ nil ]; "
              "while none: [ nil ]; while none: [ nil ]; "
              "while none: [ nil ]; while none: [ nil ]; "
              "while none: [ nil ]; 1",
              "1\nok\n");
}

static void test_false_while_conditions_free_their_bytes(void) {
  install_base();
  define("none is fn [ bytes.from-byte: 1; false ]");
  define("two is fn [ bytes.from-byte: 1; bytes.from-byte: 2; false ]");
  define("five-whiles is fn [ while two: [ nil ]; while two: [ nil ]; "
         "while two: [ nil ]; while two: [ nil ]; while two: [ nil ]; 1 ]");
  check_on_both_engines(check_false_while_conditions, "five-whiles");
}

static void check_recursion_with_loops(void) {
  eval_expect("deep: 12", "48\nok\n");
}

static void test_recursion_with_a_loop_in_each_frame(void) {
  install_base();
  define("deep is fn with n [ here total is 0; repeat 3 [ "
         "here b is bytes.from-int: n; "
         "set total to total + bytes.length: b ]; "
         "if n > 0 [ total + (deep: n - 1) ] else [ total ] ]");
  check_on_both_engines(check_recursion_with_loops, "deep");
}

/* `guarded` makes eight values after its attempt. The rescue frees the value
 * that the failed body made, so the eight fit. */
static void check_attempt_and_loops(void) {
  eval_expect("retry:", "20\nok\n");
  eval_expect("guarded:", "7\nok\n");
}

static void test_attempt_inside_and_around_loops(void) {
  install_base();
  define("retry is fn [ here n is 0; repeat 20 [ attempt [ "
         "here b is bytes.from-int: 1; 1 / 0 ] rescue [ "
         "set n to n + 1 ] ]; n ]");
  define("guarded is fn [ attempt [ repeat 3 [ bytes.from-int: 1 ]; "
         "here b is bytes.from-text: \"x\"; 1 / 0 ] rescue [ 0 ]; "
         "here c1 is bytes.from-int: 1; here c2 is bytes.from-int: 2; "
         "here c3 is bytes.from-int: 3; here c4 is bytes.from-int: 4; "
         "here c5 is bytes.from-int: 5; here c6 is bytes.from-int: 6; "
         "here c7 is bytes.from-int: 7; here c8 is bytes.from-int: 8; 7 ]");
  check_on_both_engines(check_attempt_and_loops, "guarded");
}

/* --- ADR 0085: hidden loop locals --- */

/* "here a0 is nil; ...; here a<n-1> is nil; <tail>" */
static void locals_line(char *out, size_t cap, int visible,
                        const char *tail) {
  int used = 0;

  for (int i = 0; i < visible; i++) {
    used += snprintf(out + used, cap - (size_t)used, "here a%d is nil; ", i);
  }
  (void)snprintf(out + used, cap - (size_t)used, "%s", tail);
}

static void expect_locals_line(int visible, const char *tail,
                               const char *expected) {
  char line[400];

  locals_line(line, sizeof(line), visible, tail);
  eval_expect(line, expected);
}

static void expect_locals_full(int visible, const char *tail) {
  char line[400];

  locals_line(line, sizeof(line), visible, tail);
  eval_error_contains(line, FR_ERR_CAPACITY,
                      "note: space for locals in one form is full "
                      "(limit 16)\n");
}

static void test_loop_levels_share_hidden_locals(void) {
  install_base();
  expect_locals_line(15, "repeat 2 [ 0 ]; while false [ 0 ]; "
                         "repeat 3 [ 0 ]; 1",
                     "1\nok\n");
  expect_locals_line(14, "repeat 2 [ repeat 2 [ 0 ]; repeat 2 [ 0 ] ]; 1",
                     "1\nok\n");
  expect_locals_full(15, "repeat 2 [ repeat 2 [ 0 ] ]; 1");
}

static void test_while_condition_loop_uses_the_next_level(void) {
  install_base();
  expect_locals_full(15, "while (if true [ repeat 1 [ 0 ]; false ] "
                         "else [ false ]) [ 0 ]; 1");
  expect_locals_line(15, "repeat (if true [ repeat 1 [ 0 ]; 2 ] "
                         "else [ 2 ]) [ 0 ]; 1",
                     "1\nok\n");
}

static void test_hidden_locals_count_in_the_sixteen(void) {
  install_base();
  expect_locals_line(15, "repeat 2 [ 0 ]; 1", "1\nok\n");
  expect_locals_full(16, "repeat 2 [ 0 ]; 1");
  expect_locals_full(0, "repeat 2 [ 0 ]; here a0 is nil; here a1 is nil; "
                        "here a2 is nil; here a3 is nil; here a4 is nil; "
                        "here a5 is nil; here a6 is nil; here a7 is nil; "
                        "here a8 is nil; here a9 is nil; here a10 is nil; "
                        "here a11 is nil; here a12 is nil; here a13 is nil; "
                        "here a14 is nil; here a15 is nil; 1");
  expect_locals_line(14, "repeat 2 as i [ i ]; 1", "1\nok\n");
  expect_locals_full(15, "repeat 2 as i [ i ]; 1");
}

static void test_event_body_loop_uses_its_own_locals(void) {
  char line[400];
  char body[300];

  install_base();
  locals_line(body, sizeof(body), 15, "repeat 2 [ 0 ]");
  (void)snprintf(line, sizeof(line), "pulse is fn [ every 70 [ %s ] ]",
                 body);
  define(line);
  locals_line(body, sizeof(body), 15, "every 80 [ repeat 2 [ 0 ] ]; 1");
  (void)snprintf(line, sizeof(line), "outer-pulse is fn [ %s ]", body);
  define(line);
  /* Each body counts its own locals: 2 + 15 visible fit in two frames. */
  locals_line(body, sizeof(body), 15, "repeat 2 [ 0 ]");
  (void)snprintf(line, sizeof(line),
                 "both-pulse is fn [ here x is 1; here y is 2; "
                 "every 90 [ %s ]; x + y ]",
                 body);
  define(line);
}

/* --- ADR 0085: the renderer and the disassembler --- */

static void expect_see(const char *word, const char *source) {
  char line[64];
  char expected[256];

  (void)snprintf(line, sizeof(line), "see %s", word);
  (void)snprintf(expected, sizeof(expected), "overlay code\n%s\nok\n",
                 source);
  eval_expect(line, expected);
}

static void check_see_of_nested_loops(void) {
  expect_see("f", "to f [ local0 is 1 ; repeat 2 [ repeat 2 [ local0 ] ] ; "
                  "local1 is 2 ; local0 + local1 ]");
  expect_see("w1", "to w1 [ local0 is 0 ; while if true [ repeat 2 [ 0 ] ; "
                   "local0 < 3 ] else [ false ] [ set local0 to local0 + 1 "
                   "] ; local0 ]");
  expect_see("w2", "to w2 [ forever [ while false [ 0 ] ; forever [ 1 ] ] ]");
  expect_see("w3", "to w3 [ repeat if true [ repeat 1 [ 0 ] ; 2 ] else "
                   "[ 2 ] as local0 [ repeat local0 as local1 [ local1 ] ] ]");
  expect_see("w4", "to w4 [ forever [ forever [ 1 ] ] ]");
  expect_see("w5", "to w5 [ local0 is 0 ; forever [ repeat 2 [ local0 ] ; "
                   "set local0 to local0 + 1 ] ]");
  expect_see("w6", "to w6 [ while false [ forever [ 0 ] ] ]");
  expect_see("w7", "to w7 [ local0 is while false [ 0 ] ; local0 ]");
  expect_see("w8", "to w8 [ forever [ local0 is while false [ 0 ] ; "
                   "local0 ] ]");
}

static void test_see_renders_nested_loops(void) {
  install_base();
  define("f is fn [ here a is 1; repeat 2 [ repeat 2 [ a ] ]; "
         "here b is 2; a + b ]");
  define("w1 is fn [ here n is 0; while (if true [ repeat 2 [ 0 ]; "
         "n < 3 ] else [ false ]) [ set n to n + 1 ]; n ]");
  define("w2 is fn [ forever [ while false [ 0 ]; forever [ 1 ] ] ]");
  define("w3 is fn [ repeat (if true [ repeat 1 [ 0 ]; 2 ] else [ 2 ]) "
         "as i [ repeat i as j [ j ] ] ]");
  define("w4 is fn [ forever [ forever [ 1 ] ] ]");
  define("w5 is fn [ here a is 0; forever [ repeat 2 [ a ]; "
         "set a to a + 1 ] ]");
  define("w6 is fn [ while false [ forever [ 0 ] ] ]");
  define("w7 is fn [ here v is while false [ 0 ]; v ]");
  define("w8 is fn [ forever [ here v is while false [ 0 ]; v ] ]");
  check_on_both_engines(check_see_of_nested_loops, "w8");
}

static void expect_disassembly(const char *source, const char *expected) {
  static fr_compile_overlay_update_t update;
  char text[512];
  uint16_t text_len = 0;

  TEST_ASSERT_EQUAL(FR_OK, fr_compile_overlay_update_for_runtime(
                               &s_runtime, source, &update));
  TEST_ASSERT_EQUAL_UINT8(FR_INSTRUCTION_LOCALS_HEADER_SIZE,
                          update.instruction_bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(16, update.instruction_bytes[3]);
  TEST_ASSERT_EQUAL(FR_OK, fr_instruction_stream_disassemble(
                               &update.code_object.instructions, text,
                               sizeof(text), &text_len));
  TEST_ASSERT_EQUAL_STRING(expected, text);
}

static void test_loops_compile_to_checkpoint_opcodes(void) {
  install_base();
  expect_disassembly("boot is fn [ repeat 2 [ 0 ] ]",
                     "PUSH_INT 2\nBYTES_BEGIN 15\nREPEAT_BEGIN 25\n"
                     "PUSH_INT 0\nDROP\nBYTES_REWIND 15\nREPEAT_NEXT 14\n"
                     "PUSH_NIL\nRETURN\n");
  expect_disassembly("boot is fn [ while false [ 0 ] ]",
                     "BYTES_BEGIN 15\nPUSH_FALSE\nJUMP_IF_FALSY 21\n"
                     "PUSH_INT 0\nDROP\nBYTES_REWIND 15\nJUMP 6\n"
                     "BYTES_REWIND 15\nPUSH_NIL\nRETURN\n");
  expect_disassembly("boot is fn [ forever [ 0 ] ]",
                     "BYTES_BEGIN 15\nPUSH_INT 0\nDROP\nBYTES_REWIND 15\n"
                     "JUMP 6\nPUSH_NIL\nRETURN\n");
  expect_disassembly("boot is fn [ while false [ repeat 2 [ 0 ] ] ]",
                     "BYTES_BEGIN 15\nPUSH_FALSE\nJUMP_IF_FALSY 38\n"
                     "PUSH_INT 2\nBYTES_BEGIN 14\nREPEAT_BEGIN 31\n"
                     "PUSH_INT 0\nDROP\nBYTES_REWIND 14\nREPEAT_NEXT 20\n"
                     "PUSH_NIL\nDROP\nBYTES_REWIND 15\nJUMP 6\n"
                     "BYTES_REWIND 15\nPUSH_NIL\nRETURN\n");
}

/* --- ADR 0085: invalid checkpoints and streams --- */

static fr_err_t run_stream(const uint8_t *bytes, uint16_t length) {
  fr_instruction_stream_t view;
  fr_tagged_t result = 0;

  TEST_ASSERT_EQUAL(FR_OK, fr_instruction_stream_init(&view, bytes, length));
  return fr_vm_run_instruction_stream(&s_runtime, &view, &result);
}

/* Store a value in local 15 with STORE_LOCAL, then rewind to it. */
static fr_err_t rewind_to(const uint8_t *push, uint16_t push_length) {
  uint8_t bytes[32] = {FR_INSTRUCTION_FORMAT_VERSION,
                       FR_INSTRUCTION_LOCALS_HEADER_SIZE, 0, 16};
  uint16_t used = FR_INSTRUCTION_LOCALS_HEADER_SIZE;

  memcpy(&bytes[used], push, push_length);
  used = (uint16_t)(used + push_length);
  bytes[used++] = FR_OP_STORE_LOCAL;
  bytes[used++] = 15;
  bytes[used++] = FR_OP_DROP;
  bytes[used++] = FR_OP_BYTES_REWIND;
  bytes[used++] = 15;
  bytes[used++] = FR_OP_RETURN;
  return run_stream(bytes, used);
}

static void test_rewind_rejects_bad_checkpoints(void) {
  const uint8_t entry_above[5] = {FR_OP_PUSH_INT, 0x00, 0x00, 0x01, 0x00};
  const uint8_t arena_above[5] = {FR_OP_PUSH_INT, 0x01, 0x00, 0x00, 0x00};
  const uint8_t negative[5] = {FR_OP_PUSH_INT, 0xff, 0xff, 0xff, 0xff};
  const uint8_t not_int[1] = {FR_OP_PUSH_FALSE};
  const uint8_t zero[5] = {FR_OP_PUSH_INT, 0x00, 0x00, 0x00, 0x00};
  const uint8_t never_set[7] = {FR_INSTRUCTION_FORMAT_VERSION,
                                FR_INSTRUCTION_LOCALS_HEADER_SIZE,
                                0,
                                16,
                                FR_OP_BYTES_REWIND,
                                15,
                                FR_OP_RETURN};

  install_base();
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, rewind_to(entry_above, 5));
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, rewind_to(arena_above, 5));
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, rewind_to(negative, 5));
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, rewind_to(not_int, 1));
  TEST_ASSERT_EQUAL(FR_OK, rewind_to(zero, 5));
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, run_stream(never_set, 7));
}

static void test_checkpoint_opcodes_check_their_local(void) {
  const uint8_t past_locals[7] = {FR_INSTRUCTION_FORMAT_VERSION,
                                  FR_INSTRUCTION_LOCALS_HEADER_SIZE,
                                  0,
                                  16,
                                  FR_OP_BYTES_BEGIN,
                                  16,
                                  FR_OP_RETURN};
  const uint8_t no_locals[5] = {FR_INSTRUCTION_FORMAT_VERSION,
                                FR_INSTRUCTION_MIN_HEADER_SIZE,
                                FR_OP_BYTES_REWIND, 0, FR_OP_RETURN};

  install_base();
  TEST_ASSERT_NOT_EQUAL(FR_OK, run_stream(past_locals, 7));
  TEST_ASSERT_NOT_EQUAL(FR_OK, run_stream(no_locals, 5));
}

static void test_old_reset_opcode_and_format_one_are_refused(void) {
  const uint8_t old_reset[4] = {FR_INSTRUCTION_FORMAT_VERSION,
                                FR_INSTRUCTION_MIN_HEADER_SIZE, 0x22,
                                FR_OP_RETURN};
  const uint8_t format_one[4] = {1, FR_INSTRUCTION_MIN_HEADER_SIZE,
                                 FR_OP_PUSH_NIL, FR_OP_RETURN};

  install_base();
  TEST_ASSERT_EQUAL_UINT8(2, FR_INSTRUCTION_FORMAT_VERSION);
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, run_stream(old_reset, 4));
  TEST_ASSERT_EQUAL(FR_ERR_UNSUPPORTED, run_stream(format_one, 4));
}

/* Compile `source` and change the first `op` of its word: the opcode byte
 * becomes `new_op` and the operand byte becomes `new_operand`. */
static void compile_patched(fr_compile_overlay_update_t *update,
                            const char *source, fr_opcode_t op,
                            uint8_t new_op, uint8_t new_operand) {
  fr_instruction_stream_t view;
  fr_instruction_header_t header;
  fr_code_offset_t ip = 0;

  TEST_ASSERT_EQUAL(FR_OK, fr_compile_overlay_update_for_runtime(
                               &s_runtime, source, update));
  view = update->code_object.instructions;
  TEST_ASSERT_EQUAL(FR_OK, fr_instruction_read_header(&view, &header));
  ip = header.header_size;
  while (view.bytes[ip] != op) {
    char line[32];
    uint16_t line_len = 0;
    fr_code_offset_t next_ip = 0;

    TEST_ASSERT_EQUAL(FR_OK,
                      fr_instruction_disassemble_at(&view, ip, line,
                                                    sizeof(line), &line_len,
                                                    &next_ip));
    ip = next_ip;
    TEST_ASSERT_TRUE(ip < view.length);
  }
  update->instruction_bytes[ip] = new_op;
  update->instruction_bytes[ip + 1u] = new_operand;
}

/* Each case runs boot on the contiguous engine, then saves and runs it
 * again on the flash-reader engine. */
static void test_bad_checkpoint_is_invalid_on_both_engines(void) {
  static fr_compile_overlay_update_t update;
  fr_tagged_t result = 0;

  /* The REWIND reads the visible local, which holds no checkpoint. */
  install_base();
  compile_patched(&update, "boot is fn [ here x is 1000000; repeat 1 [ 0 ] ]",
                  FR_OP_BYTES_REWIND, FR_OP_BYTES_REWIND, 0);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_overlay_apply(&s_runtime, &update.overlay_update));
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, fr_vm_run_boot(&s_runtime, &result));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));
  remount(false);
  assert_reader_runs("boot");
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, fr_vm_run_boot(&s_runtime, &result));

  /* A REWIND in the place of the BEGIN reads a checkpoint that was never
   * set. */
  install_base();
  compile_patched(&update, "boot is fn [ repeat 1 [ 0 ] ]",
                  FR_OP_BYTES_BEGIN, FR_OP_BYTES_REWIND, 15);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_overlay_apply(&s_runtime, &update.overlay_update));
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, fr_vm_run_boot(&s_runtime, &result));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));
  remount(false);
  assert_reader_runs("boot");
  TEST_ASSERT_EQUAL(FR_ERR_INVALID, fr_vm_run_boot(&s_runtime, &result));
}

/* The save checks every instruction, so no image holds opcode 0x22 or a
 * checkpoint operand past the locals: the flash-reader engine never meets
 * them. */
static void test_save_refuses_malformed_checkpoint_code(void) {
  static fr_compile_overlay_update_t update;

  install_base();
  compile_patched(&update, "boot is fn [ repeat 1 [ 0 ] ]",
                  FR_OP_BYTES_BEGIN, FR_OP_BYTES_BEGIN, 16);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_overlay_apply(&s_runtime, &update.overlay_update));
  TEST_ASSERT_NOT_EQUAL(FR_OK, fr_persist_save(&s_runtime));

  install_base();
  compile_patched(&update, "boot is fn [ repeat 1 [ 0 ] ]",
                  FR_OP_BYTES_BEGIN, 0x22, FR_OP_DROP);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_overlay_apply(&s_runtime, &update.overlay_update));
  TEST_ASSERT_NOT_EQUAL(FR_OK, fr_persist_save(&s_runtime));
}

/* --- ADR 0085 decision 3: every exit of attempt and boot --- */

/* The innermost failed body cannot start its rescue: every rescue context
 * is in use. Its Bytes value is still freed. Each rescue body has a
 * statement after its inner attempt, so the rescue contexts stay open. */
static void test_rescue_limit_still_frees_the_failed_body(void) {
  static fr_compile_expression_t expression;
  char line[600];
  int used = 0;
  fr_tagged_t result = 0;

  for (int i = 0; i < FR_PROFILE_MAX_ATTEMPT_DEPTH; i++) {
    used += snprintf(line + used, sizeof(line) - (size_t)used,
                     "attempt [ fail: ] rescue [ ");
  }
  used += snprintf(line + used, sizeof(line) - (size_t)used,
                   "attempt [ here b is bytes.from-int: 1; fail: ] "
                   "rescue [ 0 ]");
  for (int i = 0; i < FR_PROFILE_MAX_ATTEMPT_DEPTH; i++) {
    used += snprintf(line + used, sizeof(line) - (size_t)used, "; 0 ]");
  }

  install_base();
  eval_ok("fail is fn [ 1 / 0 ]");
  TEST_ASSERT_EQUAL(FR_OK, fr_compile_expression_for_runtime(
                               &s_runtime, line, &expression));
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY,
                    fr_vm_run_instruction_stream(
                        &s_runtime, &expression.instructions, &result));
  assert_no_bytes_in_use();
}

static void test_every_boot_exit_frees_bytes(void) {
  const uint8_t data[1] = {'x'};
  fr_tagged_t tagged = 0;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK, fr_bytes_install(&s_runtime, data, 1, &tagged));
  TEST_ASSERT_EQUAL(FR_OK, fr_vm_run_boot(&s_runtime, &tagged));
  assert_no_bytes_in_use();

  eval_ok("boot is fn [ 1 / 0 ]");
  TEST_ASSERT_EQUAL(FR_OK, fr_bytes_install(&s_runtime, data, 1, &tagged));
  TEST_ASSERT_EQUAL(FR_ERR_DOMAIN, fr_vm_run_boot(&s_runtime, &tagged));
  assert_no_bytes_in_use();
}

/* --- ADR 0085 decision 7: the checked build --- */

#if FR_BYTES_CHECKED
static void test_checked_build_finds_a_stale_tag(void) {
  const uint8_t data[2] = {'h', 'i'};
  fr_tagged_t old_tag = 0;
  fr_tagged_t new_tag = 0;
  fr_bytes_ref_t ref = {0};
  const uint8_t *view = NULL;
  uint16_t view_len = 0;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_install(&s_runtime, data, 2, &old_tag));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_rewind(&s_runtime, (fr_bytes_mark_t){0}));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_install(&s_runtime, data, 2, &new_tag));
  TEST_ASSERT_NOT_EQUAL(old_tag, new_tag);
  TEST_ASSERT_EQUAL(FR_OK, fr_tagged_decode_bytes_ref(old_tag, &ref));
  TEST_ASSERT_EQUAL(FR_ERR_VOLATILE,
                    fr_bytes_view(&s_runtime, ref, &view, &view_len));
  TEST_ASSERT_EQUAL(FR_OK, fr_tagged_decode_bytes_ref(new_tag, &ref));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_view(&s_runtime, ref, &view, &view_len));
}

static void test_checked_build_fills_freed_bytes(void) {
  const uint8_t data[3] = {'a', 'b', 'c'};
  fr_tagged_t tagged = 0;
  fr_bytes_ref_t ref = {0};
  const uint8_t *kept = NULL;
  uint16_t kept_len = 0;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_install(&s_runtime, data, 3, &tagged));
  TEST_ASSERT_EQUAL(FR_OK, fr_tagged_decode_bytes_ref(tagged, &ref));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_view(&s_runtime, ref, &kept, &kept_len));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_bytes_rewind(&s_runtime, (fr_bytes_mark_t){0}));
  for (uint16_t i = 0; i < kept_len; i++) {
    TEST_ASSERT_EQUAL_HEX8(0xA5, kept[i]);
  }
}
#endif

/* --- loop pass and event handler --- */

static void test_loop_reset_prevents_arena_exhaustion(void) {
  install_base();
  eval_ok("repeat 20 [ bytes.from-text: \"hello world test\" ]");
}

#if FR_FEATURE_NET
static void test_event_handler_loop_keeps_its_value(void) {
  install_base();
  eval_ok("counter is cells: 1");
  eval_ok("set counter[0] to 0");
  eval_ok("handler is fn [ "
          "here b is bytes.from-text: \"abc\"; "
          "repeat 2 [ 0 ]; "
          "set counter[0] to bytes.length: b ]");
  eval_ok("boot is fn [ on wifi.disconnected [ handler: ] ]");
  eval_ok("boot:");
  fr_host_wifi_fire_event(FR_EVENT_KIND_WIFI_DISCONNECTED);
  eval_ok("0");
  eval_expect("counter[0]", "3\nok\n");
}
#endif

#endif /* FR_FEATURE_BYTES */

int main(void) {
  UNITY_BEGIN();
#if FR_FEATURE_BYTES
  RUN_TEST(test_bytes_from_text);
  RUN_TEST(test_bytes_from_byte);
  RUN_TEST(test_bytes_from_int);
  RUN_TEST(test_bytes_length);
  RUN_TEST(test_bytes_at);
  RUN_TEST(test_bytes_equals_p);
  RUN_TEST(test_bytes_concat);
  RUN_TEST(test_text_pack_happy);
  RUN_TEST(test_text_pack_capacity);
  RUN_TEST(test_slot_rejects_bytes);
  RUN_TEST(test_cell_rejects_bytes);
#if FR_FEATURE_RECORDS
  RUN_TEST(test_record_field_reject_reports_bytes);
#endif
  RUN_TEST(test_record_field_rejects_bytes);
  RUN_TEST(test_rewind_makes_newer_values_stale);
  RUN_TEST(test_rewind_refuses_a_mark_above_the_cursors);
  RUN_TEST(test_empty_value_takes_one_entry);
  RUN_TEST(test_failed_capacity_changes_nothing);
  RUN_TEST(test_loop_frees_each_pass);
  RUN_TEST(test_million_conversions_keep_the_cursors);
#if FR_FEATURE_I2C
  RUN_TEST(test_i2c_read_loop_runs_2000_passes);
#endif
#if FR_FEATURE_EVENTS && FR_FEATURE_CELLS
  RUN_TEST(test_event_body_loop_makes_nine_values);
  RUN_TEST(test_outer_values_survive_loops_and_events);
#endif
  RUN_TEST(test_set_local_refuses_bytes);
  RUN_TEST(test_set_local_in_a_line_refuses_bytes);
  RUN_TEST(test_here_scope_ends_with_its_pass);
  RUN_TEST(test_values_made_before_a_loop_survive);
  RUN_TEST(test_set_of_a_name_refuses_bytes);
  RUN_TEST(test_word_result_lives_in_the_caller_pass);
#if FR_FEATURE_NET
  RUN_TEST(test_tcp_read_loop_frees_each_pass);
  RUN_TEST(test_http_value_is_freed_by_its_pass);
#endif
  RUN_TEST(test_read_decode_print_loop_runs_10000_passes);
  RUN_TEST(test_false_while_conditions_free_their_bytes);
  RUN_TEST(test_recursion_with_a_loop_in_each_frame);
  RUN_TEST(test_attempt_inside_and_around_loops);
  RUN_TEST(test_loop_levels_share_hidden_locals);
  RUN_TEST(test_while_condition_loop_uses_the_next_level);
  RUN_TEST(test_hidden_locals_count_in_the_sixteen);
  RUN_TEST(test_event_body_loop_uses_its_own_locals);
  RUN_TEST(test_see_renders_nested_loops);
  RUN_TEST(test_loops_compile_to_checkpoint_opcodes);
  RUN_TEST(test_rewind_rejects_bad_checkpoints);
  RUN_TEST(test_checkpoint_opcodes_check_their_local);
  RUN_TEST(test_old_reset_opcode_and_format_one_are_refused);
  RUN_TEST(test_bad_checkpoint_is_invalid_on_both_engines);
  RUN_TEST(test_save_refuses_malformed_checkpoint_code);
  RUN_TEST(test_rescue_limit_still_frees_the_failed_body);
  RUN_TEST(test_every_boot_exit_frees_bytes);
#if FR_BYTES_CHECKED
  RUN_TEST(test_checked_build_finds_a_stale_tag);
  RUN_TEST(test_checked_build_fills_freed_bytes);
#endif
  RUN_TEST(test_loop_reset_prevents_arena_exhaustion);
#if FR_FEATURE_NET
  RUN_TEST(test_event_handler_loop_keeps_its_value);
#endif
#endif
  return UNITY_END();
}
