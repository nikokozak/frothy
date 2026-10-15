/*
 * Unity tests for buffers (ADR 0088): the six words, the definition form,
 * the holders, the Text pool, the saved BUFFER record, the tiers and the
 * compile gate. The ten byte parameters of the hardware words also have
 * cases in the test files of those words.
 */

#include "base_defs.h"
#include "base_image.h"
#include "event.h"
#include "lib_native.h"
#include "object.h"
#include "persist.h"
#include "persist_payload.h"
#include "platform.h"
#include "repl.h"
#include "runtime.h"
#include "slot.h"
#include "tagged.h"

#include "unity/unity.h"

#include <stdio.h>
#include <string.h>

static fr_runtime_t s_runtime;
static char s_out[FR_REPL_OUTPUT_BYTES];

void setUp(void) {
  (void)fr_platform_persist_clear();
  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
}

void tearDown(void) {}

static fr_err_t eval(const char *line) {
  return fr_repl_eval_line(&s_runtime, line, s_out, (uint16_t)sizeof(s_out));
}

/* One prompt line, the result that it returns and its whole reply. */
static void expect(const char *line, fr_err_t err, const char *reply) {
  TEST_ASSERT_EQUAL_MESSAGE(err, eval(line), line);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(reply, s_out, line);
}

static void expect_ok(const char *line) { expect(line, FR_OK, "ok\n"); }

/* A refusal, checked by its result and one line of its reply. */
static void expect_refused(const char *line, fr_err_t err, const char *part) {
  TEST_ASSERT_EQUAL_MESSAGE(err, eval(line), line);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_out, part), s_out);
}

static bool name_exists(const char *name) {
  fr_slot_id_t slot_id = 0;

  return fr_slot_id_for_name(&s_runtime, name, &slot_id) == FR_OK;
}

/* test.pass counts the passes of a loop and stops the loop at pass
 * 10,000. */
static uint16_t s_passes;

static fr_err_t test_pass(fr_runtime_t *runtime, const fr_tagged_t *args,
                          uint8_t arg_count, fr_tagged_t *out) {
  (void)runtime;
  (void)args;
  (void)arg_count;
  s_passes++;
  *out = fr_tagged_nil();
  return s_passes < 10000u ? FR_OK : FR_ERR_INTERRUPTED;
}

const fr_lib_native_def_t fr_lib_natives[] = {
    {"test.pass", test_pass, 0},
};
const uint16_t fr_lib_natives_count = 1;

/* The parts of the runtime that a refusal must not change. Each test that
 * uses them defines `keep` as 7 first. */
typedef struct runtime_marks_t {
  uint16_t names;
  fr_slot_id_t slots;
  fr_object_id_t objects;
  uint16_t pool;
} runtime_marks_t;

static runtime_marks_t runtime_marks(void) {
  return (runtime_marks_t){
      .names = fr_slot_project_name_count(&s_runtime),
      .slots = s_runtime.slots.count,
      .objects = fr_object_count(&s_runtime),
      .pool = s_runtime.objects.used_text_bytes,
  };
}

static void expect_unchanged(runtime_marks_t before) {
  runtime_marks_t after = runtime_marks();

  TEST_ASSERT_EQUAL_UINT16(before.names, after.names);
  TEST_ASSERT_EQUAL_UINT16(before.slots, after.slots);
  TEST_ASSERT_EQUAL_UINT16(before.objects, after.objects);
  TEST_ASSERT_EQUAL_UINT16(before.pool, after.pool);
  expect("keep", FR_OK, "7\nok\n");
}

/* `buffer` is reserved where `cells` is, on every profile (decision 14). */
static void test_buffer_is_a_reserved_word(void) {
  expect_refused("to f with buffer [ 1 ]", FR_ERR_INVALID,
                 "reserved word cannot be used as a parameter");
  expect_refused("to g [ here buffer is 1; 2 ]", FR_ERR_INVALID,
                 "reserved word cannot be used as a name");
  expect_refused("repeat 3 as buffer [ 1 ]", FR_ERR_INVALID,
                 "reserved word cannot be used as a name");
  expect_ok("to h with rx-buffer [ rx-buffer ]");
  expect("h: 7", FR_OK, "7\nok\n");
  expect_ok("buffer.x is 5");
  expect("buffer.x", FR_OK, "5\nok\n");
#if FR_FEATURE_BUFFERS
  expect_refused("wait: buffer", FR_ERR_INVALID,
                 "buffer takes its capacity after ':' -- write buffer: 64\n");
#else
  expect_refused("wait: buffer", FR_ERR_UNSUPPORTED,
                 "buffers are not enabled in this build\n");
#endif
}

#if FR_FEATURE_BUFFERS

static fr_object_id_t buffer_id(const char *name) {
  fr_slot_id_t slot_id = 0;
  fr_tagged_t tagged = 0;
  fr_object_id_t object_id = 0;

  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&s_runtime, name, &slot_id));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_read(&s_runtime, slot_id, &tagged));
  TEST_ASSERT_EQUAL(FR_OK, fr_tagged_decode_object_id(tagged, &object_id));
  return object_id;
}

/* A restart: a fresh runtime, then the two passes of the boot mount. */
static void boot(void) {
  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore_library(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore_user(&s_runtime));
}

static void test_buffer_words_read_and_write(void) {
  expect_ok("b is buffer: 8");
  expect("b", FR_OK, "buffer 0/8\nok\n");
  expect("buffer.capacity: b", FR_OK, "8\nok\n");
  expect_ok("buffer.set: b, \"hi\"");
  expect_ok("buffer.append: b, 42");
  expect("b", FR_OK, "buffer 4/8\nok\n");
  expect("buffer.length: b", FR_OK, "4\nok\n");
  expect("buffer.at: b, 0", FR_OK, "104\nok\n");
  expect("buffer.at: b, 3", FR_OK, "50\nok\n");
  expect("buffer.at: b, 4", FR_ERR_RANGE,
         "error: out of range: 4 (1)\n"
         "detail: buffer.at argument 2 was rejected\n");
  expect("buffer.at: b, -1", FR_ERR_RANGE,
         "error: out of range: -1 (1)\n"
         "detail: buffer.at argument 2 was rejected\n");
  expect("buffer.equals?: b, \"hi42\"", FR_OK, "true\nok\n");
  expect("buffer.equals?: b, \"hi4\"", FR_OK, "false\nok\n");
  expect_ok("buffer.set: b, \"\"");
  expect("b", FR_OK, "buffer 0/8\nok\n");
  expect("buffer.equals?: b, \"\"", FR_OK, "true\nok\n");
  expect_refused("buffer.length: \"hi\"", FR_ERR_TYPE,
                 "buffer.length argument 1 expects a buffer, got text\n");
}

/* `value` takes Text, Bytes, an integer or a buffer (decision 4). */
static void test_buffer_takes_each_value_kind(void) {
  expect_ok("b is buffer: 8");
  expect_ok("c is buffer: 8");
  expect_ok("buffer.set: c, \"12\"");
  expect_ok("buffer.set: b, 12");
  expect("buffer.equals?: b, \"12\"", FR_OK, "true\nok\n");
  expect("buffer.equals?: b, 12", FR_OK, "true\nok\n");
  expect("buffer.equals?: b, c", FR_OK, "true\nok\n");
#if FR_FEATURE_BYTES
  expect("buffer.equals?: b, (bytes.from-text: \"12\")", FR_OK, "true\nok\n");
  expect_ok("buffer.set: b, (bytes.from-text: \"ab\")");
  expect("buffer.equals?: b, \"ab\"", FR_OK, "true\nok\n");
#else
  expect_ok("buffer.set: b, \"ab\"");
#endif
  expect("buffer.set: b, nil", FR_ERR_TYPE,
         "error: wrong type: nil (2)\n"
         "buffer.set argument 2 expects int, text, bytes, or buffer, got "
         "nil\n");
  expect("buffer.set: b, true", FR_ERR_TYPE,
         "error: wrong type: true (2)\n"
         "buffer.set argument 2 expects int, text, bytes, or buffer, got a "
         "bool\n");
  expect_refused("buffer.equals?: b, false", FR_ERR_TYPE, "got a bool\n");
  expect_refused("buffer.append: b, nil", FR_ERR_TYPE, "got nil\n");
#if FR_FEATURE_CELLS
  expect_ok("x is cells: 1");
  expect_refused("buffer.set: b, x", FR_ERR_TYPE, "got cells\n");
#endif
  expect_ok("record pt [ v ]");
  expect_ok("p is pt: 1");
  expect_refused("buffer.set: b, p", FR_ERR_TYPE, "got a record\n");
#if FR_FEATURE_PWM
  expect_ok("h is pwm.open: 20, 1000");
  expect_refused("buffer.set: b, h", FR_ERR_TYPE, "got a handle\n");
  fr_handle_close_all(&s_runtime);
#endif
  expect("buffer.equals?: b, \"ab\"", FR_OK, "true\nok\n");
}

/* A value that does not fit changes nothing; a value of exactly the
 * capacity fits; a copy from the same buffer is safe (decision 6). */
static void test_buffer_fit_and_copy_rules(void) {
  expect_ok("b is buffer: 4");
  expect_ok("buffer.set: b, \"abcd\"");
  expect("buffer.append: b, \"e\"", FR_ERR_CAPACITY,
         "error: capacity exceeded (4)\n"
         "note: buffer is full (limit 4 bytes)\n");
  expect("buffer.set: b, \"abcde\"", FR_ERR_CAPACITY,
         "error: capacity exceeded (4)\n"
         "note: buffer is full (limit 4 bytes)\n");
  expect("buffer.equals?: b, \"abcd\"", FR_OK, "true\nok\n");

  expect_ok("s is buffer: 8");
  expect_ok("buffer.set: s, \"abc\"");
  expect_ok("buffer.append: s, s");
  expect("buffer.equals?: s, \"abcabc\"", FR_OK, "true\nok\n");
  expect("buffer.append: s, s", FR_ERR_CAPACITY,
         "error: capacity exceeded (4)\n"
         "note: buffer is full (limit 8 bytes)\n");
  expect_ok("buffer.set: s, s");
  expect("buffer.equals?: s, \"abcabc\"", FR_OK, "true\nok\n");

  expect_ok("n is buffer: 11");
  expect_ok("buffer.set: n, 1073741823");
  expect("buffer.equals?: n, \"1073741823\"", FR_OK, "true\nok\n");
  expect_ok("buffer.set: n, (0 - 1073741823 - 1)");
  expect("buffer.equals?: n, \"-1073741824\"", FR_OK, "true\nok\n");
#if FR_FEATURE_BYTES
  expect_ok("buffer.set: n, \"\"");
  expect_ok("buffer.append: n, (bytes.from-byte: 65)");
  expect("buffer.equals?: n, \"A\"", FR_OK, "true\nok\n");
#endif

  /* Two buffers are two stores. */
  expect_ok("other is buffer: 4");
  expect_ok("buffer.set: other, \"y\"");
  expect_ok("buffer.set: b, \"z\"");
  expect("buffer.equals?: other, \"y\"", FR_OK, "true\nok\n");
}

/* The other words treat a buffer as decision 3 says. */
static void test_buffer_with_other_words(void) {
  char capture[16] = {0};

  expect_ok("b is buffer: 4");
  expect_ok("buffer.set: b, \"x\"");
  expect("b = b", FR_ERR_TYPE,
         "error: wrong type: buffer 1/4 (2)\n"
         "expected an int, got a buffer\n");
  expect("print.value: \"n\", b", FR_ERR_TYPE,
         "error: wrong type: buffer 1/4 (2)\n"
         "print.value argument 2 expects an int, got a buffer\n");
  expect("print.value: b, 1", FR_ERR_TYPE,
         "error: wrong type: buffer 1/4 (2)\n"
         "print.value argument 1 expects text, got a buffer\n");
  expect("text.length: b", FR_ERR_TYPE,
         "error: wrong type: buffer 1/4 (2)\n"
         "text.length argument 1 expects text, got a buffer\n");
#if FR_FEATURE_BYTES
  expect("bytes.length: b", FR_ERR_TYPE,
         "error: wrong type: buffer 1/4 (2)\n"
         "bytes.length argument 1 expects bytes, got a buffer\n");
  expect_refused("text.pack: b", FR_ERR_TYPE, "got a buffer\n");
#endif
  fr_host_capture_text(capture, (uint16_t)sizeof(capture));
  TEST_ASSERT_EQUAL(FR_OK, eval("print: b"));
  fr_host_capture_text(NULL, 0);
  TEST_ASSERT_EQUAL_STRING("x", capture);
  expect_ok("set b to \"text\"");
  expect("b", FR_OK, "\"text\"\nok\n");
}

/* A parameter and a local hold a buffer; a cell and a record field refuse
 * it and keep their values (decision 3). */
static void test_buffer_holders(void) {
  expect_ok("b is buffer: 4");
  expect_ok("to f with v [ here l is v; buffer.append: l, \"!\"; l ]");
  expect("f: b", FR_OK, "buffer 1/4\nok\n");
  expect("buffer.length: b", FR_OK, "1\nok\n");
#if FR_FEATURE_CELLS
  expect_ok("x is cells: 1");
  expect_ok("set x[0] to 5");
  expect("set x[0] to b", FR_ERR_TYPE,
         "error: wrong type: buffer 1/4 (2)\n"
         "detail: value cannot be stored in cells\n");
  expect("x[0]", FR_OK, "5\nok\n");
#endif
  expect_ok("record pt [ v ]");
  expect_ok("p is pt: 1");
  expect("set p -> v to b", FR_ERR_TYPE,
         "error: wrong type: buffer 1/4 (2)\n"
         "detail: value cannot be stored in record fields\n");
  expect("p -> v", FR_OK, "1\nok\n");
}

/* A definition with the same capacity keeps the buffer and empties it, so a
 * file can run again and again with no growth (decision 2). */
static void test_buffer_definition_reuses_the_buffer(void) {
  uint16_t objects = 0;
  uint16_t pool = 0;

  for (int run = 0; run < 10; run++) {
    expect_ok("reply is buffer: 256");
    expect("buffer.length: reply", FR_OK, "0\nok\n");
    expect_ok("buffer.set: reply, \"data\"");
    if (run == 0) {
      objects = s_runtime.objects.count;
      pool = s_runtime.objects.used_text_bytes;
    }
    TEST_ASSERT_EQUAL_UINT16(objects, s_runtime.objects.count);
    TEST_ASSERT_EQUAL_UINT16(pool, s_runtime.objects.used_text_bytes);
  }
  expect_ok("alias is reply");
  expect_ok("reply is buffer: 256");
  expect("buffer.length: alias", FR_OK, "0\nok\n");
  expect_ok("buffer.set: reply, \"data\"");
  expect_ok("reply is buffer: 128");
  expect("buffer.capacity: reply", FR_OK, "128\nok\n");
  expect("buffer.equals?: alias, \"data\"", FR_OK, "true\nok\n");
}

/* The reuse needs no storage, so it works when the object table is full.
 * A failed definition keeps the old buffer and its contents. */
static void test_buffer_definition_with_a_full_object_table(void) {
  char line[48];
  char note[96];
  unsigned made = 0;

  expect_ok("b is buffer: 4");
  expect_ok("buffer.set: b, 1234");
  while (s_runtime.objects.count < FR_PROFILE_OBJECT_TABLE_SIZE) {
    (void)snprintf(line, sizeof(line), "text.from-int: %u", 100000u + made);
    TEST_ASSERT_EQUAL_MESSAGE(FR_OK, eval(line), line);
    made++;
  }
  expect_ok("b is buffer: 4");
  expect("buffer.length: b", FR_OK, "0\nok\n");
  expect_ok("buffer.set: b, 1234");
  (void)snprintf(note, sizeof(note),
                 "error: capacity exceeded (4)\n"
                 "note: object table is full (limit %u)\n",
                 (unsigned)FR_PROFILE_OBJECT_TABLE_SIZE);
  expect("b is buffer: 5", FR_ERR_CAPACITY, note);
  expect("buffer.capacity: b", FR_OK, "4\nok\n");
  expect("buffer.equals?: b, 1234", FR_OK, "true\nok\n");
}

/* The same rule at a full Text pool; a new buffer gets the pool note and
 * changes nothing (decision 7). */
static void test_buffer_definition_with_a_full_pool(void) {
  char line[48];
  char note[160];
  uint16_t objects = 0;

  expect_ok("b is buffer: 4");
  (void)snprintf(line, sizeof(line), "big is buffer: %u",
                 (unsigned)(FR_PROFILE_MAX_OVERLAY_TEXT_BYTES -
                            s_runtime.objects.used_text_bytes));
  expect_ok(line);
  TEST_ASSERT_EQUAL_UINT16(FR_PROFILE_MAX_OVERLAY_TEXT_BYTES,
                           s_runtime.objects.used_text_bytes);
  objects = s_runtime.objects.count;
  expect_ok("b is buffer: 4");
  expect_ok("buffer.set: b, 1234");
  (void)snprintf(note, sizeof(note),
                 "error: capacity exceeded (4)\n"
                 "note: Text pool is full (limit %u bytes) -- a save that "
                 "succeeds frees what the program no longer uses\n",
                 (unsigned)FR_PROFILE_MAX_OVERLAY_TEXT_BYTES);
  expect("b is buffer: 5", FR_ERR_CAPACITY, note);
  expect("buffer.capacity: b", FR_OK, "4\nok\n");
  expect("buffer.equals?: b, 1234", FR_OK, "true\nok\n");
  expect("n is buffer: 1", FR_ERR_CAPACITY, note);
  TEST_ASSERT_EQUAL_UINT16(objects, s_runtime.objects.count);
  TEST_ASSERT_EQUAL_UINT16(FR_PROFILE_MAX_OVERLAY_TEXT_BYTES,
                           s_runtime.objects.used_text_bytes);
  TEST_ASSERT_FALSE(name_exists("n"));
}

/* `buffer:` is definition syntax with a literal capacity (decision 2). */
static void test_buffer_definition_refusals(void) {
  char line[48];
  uint16_t objects = s_runtime.objects.count;

  expect("to g [ here k is buffer: 3 ]", FR_ERR_UNSUPPORTED,
         "error: unsupported (9)\n");
  expect("wait: buffer: 3", FR_ERR_UNSUPPORTED, "error: unsupported (9)\n");
  expect_refused("z is buffer: 0", FR_ERR_RANGE,
                 "integer literal is out of range\n");
  (void)snprintf(line, sizeof(line), "z is buffer: %u",
                 (unsigned)(FR_PROFILE_MAX_OVERLAY_TEXT_BYTES + 1u));
  expect(line, FR_ERR_RANGE, "error: out of range (1)\n");
  expect_refused("z is buffer: x", FR_ERR_INVALID, "unexpected token\n");
  expect_refused("z is buffer 3", FR_ERR_INVALID,
                 "buffer takes its capacity after ':' -- write buffer: 64\n");
  TEST_ASSERT_EQUAL_UINT16(objects, s_runtime.objects.count);
  TEST_ASSERT_FALSE(name_exists("z"));
}

/* The pool counts the RAM Text bytes and the buffer capacities. A mounted
 * Text value uses no pool bytes; a mounted buffer does (decisions 7, 10). */
static void test_buffer_pool_accounting(void) {
  uint16_t base = s_runtime.objects.base_used_text_bytes;
  char mem[1024];
  char row[64];

  expect_ok("t is \"hello\"");
  expect_ok("b is buffer: 10");
  TEST_ASSERT_EQUAL_UINT16(base + 15u, s_runtime.objects.used_text_bytes);
  TEST_ASSERT_EQUAL_UINT16(10, fr_buffer_used_bytes(&s_runtime));
  expect_ok("save");
  TEST_ASSERT_EQUAL_UINT16(base + 10u, s_runtime.objects.used_text_bytes);
  TEST_ASSERT_EQUAL_UINT16(10, fr_buffer_used_bytes(&s_runtime));
  expect_ok("restore");
  TEST_ASSERT_EQUAL_UINT16(base + 10u, s_runtime.objects.used_text_bytes);
  boot();
  TEST_ASSERT_EQUAL_UINT16(base + 10u, s_runtime.objects.used_text_bytes);
  TEST_ASSERT_EQUAL_UINT16(10, fr_buffer_used_bytes(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_repl_eval_line(&s_runtime, "mem objects", mem,
                                             (uint16_t)sizeof(mem)));
  (void)snprintf(row, sizeof(row), "objects.text.used %u\n",
                 (unsigned)(base + 10u));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mem, row), mem);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mem, "objects.buffers.used 10\n"), mem);
}

/* A buffer lives as Cells do (decision 8). */
static void test_buffer_lifetime(void) {
  expect_ok("b is buffer: 8");
  expect_ok("buffer.set: b, \"saved\"");
  expect_ok("save");
  expect_ok("buffer.set: b, \"later\"");
  expect_ok("n is buffer: 3");
  expect_ok("clear");
  expect("buffer.equals?: b, \"later\"", FR_OK, "true\nok\n");
  TEST_ASSERT_FALSE(name_exists("n"));
  expect_ok("restore");
  expect("buffer.equals?: b, \"saved\"", FR_OK, "true\nok\n");
  boot();
  expect("b", FR_OK, "buffer 5/8\nok\n");
  expect_ok("wipe-user");
  TEST_ASSERT_FALSE(name_exists("b"));

  expect_ok("d is buffer: 2");
  expect_ok("save");
  TEST_ASSERT_EQUAL(FR_OK, eval("dangerous.wipe"));
  TEST_ASSERT_FALSE(name_exists("d"));
  TEST_ASSERT_EQUAL_UINT16(s_runtime.objects.base_count,
                           s_runtime.objects.count);
  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
  TEST_ASSERT_NOT_EQUAL(FR_OK, fr_persist_restore_library(&s_runtime));
}

/* The tiers (decisions 8 and 16): a library buffer returns to its saved
 * contents at each save, also when a user name holds it. */
static void test_buffer_tiers(void) {
  expect_ok("install-library");
  expect_ok("lb is buffer: 8");
  expect_ok("buffer.set: lb, \"lib\"");
  expect_ok("lw is 1");
  expect_ok("install-user");
  expect_ok("buffer.set: lb, \"user\"");
  expect_ok("save");
  expect("buffer.equals?: lb, \"lib\"", FR_OK, "true\nok\n");

  expect_ok("mine is lb");
  expect_ok("buffer.set: lb, \"both\"");
  expect_ok("save");
  expect("buffer.equals?: mine, \"lib\"", FR_OK, "true\nok\n");
  expect_ok("mine is buffer: 8");
  expect("buffer.length: lb", FR_OK, "0\nok\n");
  expect_ok("save");
  expect("buffer.equals?: lb, \"lib\"", FR_OK, "true\nok\n");
  expect("buffer.equals?: mine, \"lib\"", FR_OK, "true\nok\n");

  expect_ok("ub is buffer: 4");
  expect_ok("buffer.set: ub, \"u\"");
  expect_ok("save");
  boot();
  expect("buffer.equals?: ub, \"u\"", FR_OK, "true\nok\n");
  expect("buffer.equals?: lb, \"lib\"", FR_OK, "true\nok\n");
  expect_ok("restore");
  expect("buffer.equals?: ub, \"u\"", FR_OK, "true\nok\n");
  expect_ok("wipe-user");
  TEST_ASSERT_FALSE(name_exists("ub"));
  TEST_ASSERT_FALSE(name_exists("mine"));
  expect("buffer.equals?: lb, \"lib\"", FR_OK, "true\nok\n");
}

/* A save that fails keeps each buffer as it was: a flash error, and an
 * image that does not fit the storage. */
static void test_buffer_survives_a_failed_save(void) {
  static uint8_t image_before[FR_PERSIST_STORAGE_BYTES];
  static uint8_t image_after[FR_PERSIST_STORAGE_BYTES];
  char line[FR_REPL_LINE_BYTES];
  uint16_t before_length = 0;
  uint16_t after_length = 0;
  runtime_marks_t before;
  fr_err_t err = FR_OK;

  expect_ok("keep is 7");
  expect_ok("b is buffer: 8");
  expect_ok("buffer.set: b, 1234");
  before = runtime_marks();
  fr_host_persist_debug_fail_stream_step(1);
  TEST_ASSERT_NOT_EQUAL(FR_OK, eval("save"));
  expect_unchanged(before);
  expect("b", FR_OK, "buffer 4/8\nok\n");
  expect("buffer.equals?: b, 1234", FR_OK, "true\nok\n");

  /* Text values fill the image: each save frees the pool, and the image
   * keeps the values. */
  expect_ok("save");
  for (unsigned cycle = 0; err == FR_OK; cycle++) {
    int used = 0;

    TEST_ASSERT_TRUE(cycle < 40u);
    used = snprintf(line, sizeof(line), "t%ua is \"%03u", cycle, cycle);
    memset(&line[used], 'x', 247);
    (void)snprintf(&line[used + 247], sizeof(line) - (size_t)used - 247u, "\"");
    expect_ok(line);
    (void)snprintf(line, sizeof(line), "t%ub is text.concat: t%ua, t%ua", cycle,
                   cycle, cycle);
    expect_ok(line);
    (void)snprintf(line, sizeof(line), "t%uc is text.concat: t%ub, t%ub", cycle,
                   cycle, cycle);
    expect_ok(line);
    (void)snprintf(line, sizeof(line), "t%ud is text.concat: t%uc, t%uc", cycle,
                   cycle, cycle);
    expect_ok(line);
    TEST_ASSERT_EQUAL(FR_OK, fr_platform_persist_read(
                                 image_before, (uint16_t)sizeof(image_before),
                                 &before_length, 0));
    before = runtime_marks();
    err = eval("save");
  }
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY, err);
  expect_unchanged(before);
  expect("b", FR_OK, "buffer 4/8\nok\n");
  expect("buffer.equals?: b, 1234", FR_OK, "true\nok\n");
  TEST_ASSERT_EQUAL(
      FR_OK, fr_platform_persist_read(
                 image_after, (uint16_t)sizeof(image_after), &after_length, 0));
  TEST_ASSERT_EQUAL_UINT16(before_length, after_length);
  TEST_ASSERT_EQUAL_MEMORY(image_before, image_after, before_length);
}

/* A save never writes an image that its own mount refuses: a pool full of
 * buffers saves and mounts. */
static void test_buffer_full_pool_saves_and_mounts(void) {
  char line[48];

  expect_ok("b is buffer: 8");
  expect_ok("buffer.set: b, \"keep\"");
  expect_ok("save");
  (void)snprintf(line, sizeof(line), "f is buffer: %u",
                 (unsigned)(FR_PROFILE_MAX_OVERLAY_TEXT_BYTES -
                            s_runtime.objects.used_text_bytes));
  expect_ok(line);
  expect_ok("save");
  boot();
  TEST_ASSERT_EQUAL_UINT16(FR_PROFILE_MAX_OVERLAY_TEXT_BYTES,
                           s_runtime.objects.used_text_bytes);
  /* A full pool has no room for a new Text literal, so the check uses
   * integers. */
  expect("buffer.length: b", FR_OK, "4\nok\n");
  expect("buffer.at: b, 0", FR_OK, "107\nok\n");
}

/* A runtime copy points a buffer entry at its own pool (decision 7). */
static void test_buffer_runtime_copy_rebases(void) {
  static fr_runtime_t copy;
  fr_object_id_t object_id = 0;
  const uint8_t *bytes = NULL;
  uint16_t length = 0;
  uint16_t capacity = 0;

  expect_ok("b is buffer: 4");
  expect_ok("buffer.set: b, \"ab\"");
  object_id = buffer_id("b");
  memcpy(&copy, &s_runtime, sizeof(copy));
  fr_object_rebase_ram_pointers(&copy, &s_runtime);
  TEST_ASSERT_EQUAL_PTR(
      &copy.objects.text_bytes[copy.objects.entries[object_id].first],
      copy.objects.entries[object_id].bytes);
  TEST_ASSERT_EQUAL(
      FR_OK, fr_buffer_view(&copy, object_id, &bytes, &length, &capacity));
  TEST_ASSERT_EQUAL_UINT16(2, length);
  TEST_ASSERT_EQUAL_UINT16(4, capacity);
  TEST_ASSERT_EQUAL_MEMORY("ab", bytes, 2);
}

/* 10,000 passes reuse one buffer: no object entry and no pool byte. */
static void test_buffer_many_passes_use_no_storage(void) {
  uint16_t objects = 0;
  uint16_t pool = 0;

  expect_ok("src is buffer: 200");
  expect_ok("repeat 20 [ buffer.append: src, \"0123456789\" ]");
  expect("buffer.length: src", FR_OK, "200\nok\n");
  expect_ok("b is buffer: 256");
  expect_ok("to work [ buffer.set: b, src; buffer.append: b, 1073741823 ]");
  expect_ok("work:");
  expect_ok("to spin [ forever [ work:; test.pass: ] ]");
  objects = s_runtime.objects.count;
  pool = s_runtime.objects.used_text_bytes;
  s_passes = 0;
  TEST_ASSERT_EQUAL(FR_ERR_INTERRUPTED, eval("spin:"));
  TEST_ASSERT_EQUAL_UINT16(10000, s_passes);
  TEST_ASSERT_EQUAL_UINT16(objects, s_runtime.objects.count);
  TEST_ASSERT_EQUAL_UINT16(pool, s_runtime.objects.used_text_bytes);

  expect_ok("to arm [ every 50 [ work: ] ]");
  expect_ok("arm:");
  objects = s_runtime.objects.count;
  for (int pass = 0; pass < 10000; pass++) {
    s_runtime.events.entries[0].pending = true;
    TEST_ASSERT_EQUAL(FR_OK, fr_event_dispatch(&s_runtime));
  }
  TEST_ASSERT_EQUAL_UINT16(objects, s_runtime.objects.count);
  TEST_ASSERT_EQUAL_UINT16(pool, s_runtime.objects.used_text_bytes);
  expect("buffer.length: b", FR_OK, "210\nok\n");
}

static fr_err_t read_end_of_input(char *line, uint16_t cap, bool *out_eof) {
  (void)cap;
  line[0] = '\0';
  *out_eof = true;
  return FR_OK;
}

static fr_err_t write_nothing(const char *text) {
  (void)text;
  return FR_OK;
}

/* A value that does not fit stops that run of an event body with no note.
 * At the idle prompt the device writes the error on a `! ` line. Inside a
 * running program the error also stops the program. A repeating event
 * stays; an `after` event is already gone (decision 6). */
static void test_buffer_overflow_in_an_event_body(void) {
  const fr_repl_io_t io = {.read_line = read_end_of_input,
                           .write_text = write_nothing};
  char capture[96] = {0};

  expect_ok("b is buffer: 2");
  expect_ok("to arm [ every 50 [ buffer.append: b, \"abc\" ] ]");
  expect_ok("arm:");
  /* The prompt registers its idle handler, then ends at the end of input. */
  TEST_ASSERT_EQUAL(FR_OK, fr_repl_run(&s_runtime, &io));
  s_runtime.events.entries[0].pending = true;
  fr_host_capture_text(capture, (uint16_t)sizeof(capture));
  fr_host_debug_run_idle();
  fr_host_capture_text(NULL, 0);
  TEST_ASSERT_EQUAL_STRING("! error: capacity exceeded (4)\n", capture);
  TEST_ASSERT_EQUAL(FR_EVENT_KIND_EVERY, s_runtime.events.entries[0].kind);
  expect("frothy.event-fire: \"every\", 50, nil", FR_ERR_CAPACITY,
         "error: capacity exceeded (4)\n");
  expect("buffer.length: b", FR_OK, "0\nok\n");

  expect_ok("to later [ after 60 [ buffer.append: b, \"abc\" ] ]");
  expect_ok("later:");
  TEST_ASSERT_EQUAL(FR_EVENT_KIND_AFTER, s_runtime.events.entries[1].kind);
  s_runtime.events.entries[1].pending = true;
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY, fr_event_dispatch(&s_runtime));
  TEST_ASSERT_EQUAL(FR_EVENT_KIND_NONE, s_runtime.events.entries[1].kind);
  expect("buffer.length: b", FR_OK, "0\nok\n");
}

/* Only the ten parameters of decision 5 and the `value` of the buffer words
 * take a buffer, and each of them that this build has does. */
static void test_buffer_parameters_are_the_ten_of_decision_5(void) {
  static const char *const allowed[] = {
      "buffer.set value",
      "buffer.append value",
      "buffer.equals? value",
      "print value",
      "i2c.write bytes",
      "tcp.write bytes",
      "http.post body",
      "ble.advertise.start advertising_data",
      "ble.advertise.start scan_response_data",
      "ble.gatt.set data",
      "ble.gatt.notify data",
      "ble.gatt.indicate data",
      "ble.gatt.write data",
  };
  uint16_t found = 0;
  uint16_t expected = 0;

  for (uint16_t i = 0; i < fr_base_def_count(); i++) {
    const fr_base_def_t *def = NULL;
    fr_base_layer_t layer = FR_BASE_LAYER_CORE;

    TEST_ASSERT_EQUAL(FR_OK, fr_base_def_at(i, &def, &layer));
    if (def->kind != FR_BASE_DEF_NATIVE || def->native_signature == NULL) {
      continue;
    }
    for (uint8_t p = 0; p < def->native_signature->arg_count; p++) {
      const fr_native_param_t *param = &def->native_signature->params[p];
      char pair[64];
      bool listed = false;

      if (param->type != FR_NATIVE_VALUE_TEXT_BYTES_OR_BUFFER &&
          param->type != FR_NATIVE_VALUE_INT_TEXT_BYTES_OR_BUFFER) {
        continue;
      }
      (void)snprintf(pair, sizeof(pair), "%s %s", def->name, param->name);
      for (size_t k = 0; k < sizeof(allowed) / sizeof(allowed[0]); k++) {
        listed = listed || strcmp(pair, allowed[k]) == 0;
      }
      TEST_ASSERT_TRUE_MESSAGE(listed, pair);
      found++;
    }
  }
  for (size_t k = 0; k < sizeof(allowed) / sizeof(allowed[0]); k++) {
    char word[48] = {0};
    const char *space = strchr(allowed[k], ' ');
    fr_slot_id_t slot_id = 0;

    memcpy(word, allowed[k], (size_t)(space - allowed[k]));
    if (fr_base_slot_id_for_name(word, &slot_id) == FR_OK) {
      expected++;
    }
  }
  TEST_ASSERT_EQUAL_UINT16(expected, found);
}

/* The saved record: phases D and C refuse a bad BUFFER record with no
 * change. Buffers with equal contents stay apart from each other and from
 * an equal Text. */
static void restore_refused(const uint8_t *payload, uint16_t length,
                            fr_err_t err, runtime_marks_t before,
                            fr_object_id_t kept) {
  TEST_ASSERT_EQUAL(err,
                    fr_persist_payload_restore(&s_runtime, payload, length));
  expect_unchanged(before);
  TEST_ASSERT_EQUAL_UINT16(kept, buffer_id("kb"));
  expect("kb", FR_OK, "buffer 2/4\nok\n");
  expect("buffer.equals?: kb, 12", FR_OK, "true\nok\n");
}

#define FR_TEST_PAYLOAD_HEAD 'F', 'R', 'P', 'O', 6
#define FR_TEST_U16(v) (uint8_t)((v) & 0xffu), (uint8_t)((v) >> 8)

static void test_buffer_records_are_checked(void) {
  enum { POOL = FR_PROFILE_MAX_OVERLAY_TEXT_BYTES, HALF = POOL / 2 + 1 };
  static const uint8_t good[] = {
      FR_TEST_PAYLOAD_HEAD, 10, 0, 0, 4, 0, 2, 0, 'h', 'i', 0xff};
  static const uint8_t too_long[] = {
      FR_TEST_PAYLOAD_HEAD, 10, 0, 0, 2, 0, 3, 0, 'a', 'b', 'c', 0xff};
  static const uint8_t cut[] = {
      FR_TEST_PAYLOAD_HEAD, 10, 0, 0, 4, 0, 3, 0, 'a', 'b'};
  static const uint8_t empty[] = {
      FR_TEST_PAYLOAD_HEAD, 10, 0, 0, 0, 0, 0, 0, 0xff};
  static const uint8_t above_pool[] = {FR_TEST_PAYLOAD_HEAD,  10, 0, 0,
                                       FR_TEST_U16(POOL + 1), 0,  0, 0xff};
  static const uint8_t sum_above_pool[] = {
      FR_TEST_PAYLOAD_HEAD, 10, 0, 0,   FR_TEST_U16(HALF), 0, 0, 10, 1, 0,
      FR_TEST_U16(HALF),    0,  0, 0xff};
  static const uint8_t same_id[] = {
      FR_TEST_PAYLOAD_HEAD, 10, 0, 0, 4, 0, 0, 0, 10, 0, 0, 4, 0, 0, 0, 0xff};
  static const uint8_t in_cells[] = {
      FR_TEST_PAYLOAD_HEAD, 10, 0, 0, 4, 0, 0, 0, 4, 1, 0, 1, 0, 6, 0, 0, 0xff};
  static const uint8_t in_record[] = {FR_TEST_PAYLOAD_HEAD,
                                      10,
                                      0,
                                      0,
                                      4,
                                      0,
                                      0,
                                      0,
                                      6,
                                      1,
                                      0,
                                      1,
                                      0,
                                      'P',
                                      1,
                                      0,
                                      1,
                                      0,
                                      'x',
                                      7,
                                      2,
                                      0,
                                      1,
                                      0,
                                      1,
                                      0,
                                      6,
                                      0,
                                      0,
                                      0xff};
  static const uint8_t equal_contents[] = {FR_TEST_PAYLOAD_HEAD,
                                           10,
                                           0,
                                           0,
                                           4,
                                           0,
                                           2,
                                           0,
                                           'h',
                                           'i',
                                           10,
                                           1,
                                           0,
                                           4,
                                           0,
                                           2,
                                           0,
                                           'h',
                                           'i',
                                           5,
                                           2,
                                           0,
                                           2,
                                           0,
                                           'h',
                                           'i',
                                           0xff};
  uint16_t base_pool = s_runtime.objects.base_used_text_bytes;
  /* One buffer that fills the pool after the base Text bytes. */
  const uint8_t whole_pool[] = {FR_TEST_PAYLOAD_HEAD,          10, 0, 0,
                                FR_TEST_U16(POOL - base_pool), 0,  0, 0xff};
  const uint8_t *bytes = NULL;
  uint16_t length = 0;
  uint16_t capacity = 0;
  runtime_marks_t before;
  fr_object_id_t kept = 0;

  expect_ok("keep is 7");
  expect_ok("kb is buffer: 4");
  expect_ok("buffer.set: kb, 12");
  kept = buffer_id("kb");
  before = runtime_marks();

  restore_refused(too_long, sizeof(too_long), FR_ERR_CORRUPT, before, kept);
  restore_refused(cut, sizeof(cut), FR_ERR_CORRUPT, before, kept);
  restore_refused(empty, sizeof(empty), FR_ERR_CORRUPT, before, kept);
  restore_refused(above_pool, sizeof(above_pool), FR_ERR_CORRUPT, before, kept);
  restore_refused(sum_above_pool, sizeof(sum_above_pool), FR_ERR_CAPACITY,
                  before, kept);
  restore_refused(same_id, sizeof(same_id), FR_ERR_CORRUPT, before, kept);
  restore_refused(in_cells, sizeof(in_cells), FR_ERR_CORRUPT, before, kept);
  restore_refused(in_record, sizeof(in_record), FR_ERR_CORRUPT, before, kept);
  /* Phase C counts the base Text bytes: one more base byte refuses the
   * buffer that fills the rest of the pool. */
  s_runtime.objects.base_used_text_bytes = (uint16_t)(base_pool + 1u);
  restore_refused(whole_pool, sizeof(whole_pool), FR_ERR_CAPACITY, before,
                  kept);
  s_runtime.objects.base_used_text_bytes = base_pool;

  TEST_ASSERT_EQUAL(FR_OK, fr_persist_payload_restore(&s_runtime, whole_pool,
                                                      sizeof(whole_pool)));
  TEST_ASSERT_EQUAL_UINT16(POOL, s_runtime.objects.used_text_bytes);

  TEST_ASSERT_EQUAL(FR_OK,
                    fr_persist_payload_restore(&s_runtime, good, sizeof(good)));
  TEST_ASSERT_EQUAL_UINT16(s_runtime.objects.base_count + 1u,
                           s_runtime.objects.count);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_buffer_view(&s_runtime, s_runtime.objects.base_count,
                                   &bytes, &length, &capacity));
  TEST_ASSERT_EQUAL_UINT16(2, length);
  TEST_ASSERT_EQUAL_UINT16(4, capacity);
  TEST_ASSERT_EQUAL_MEMORY("hi", bytes, 2);
  TEST_ASSERT_EQUAL_UINT16(base_pool + 4u, s_runtime.objects.used_text_bytes);

  TEST_ASSERT_EQUAL(FR_OK,
                    fr_persist_payload_restore(&s_runtime, equal_contents,
                                               sizeof(equal_contents)));
  TEST_ASSERT_EQUAL_UINT16(s_runtime.objects.base_count + 3u,
                           s_runtime.objects.count);
}

#else

/* Without the gate, the source form and the saved record are refused, and
 * nothing changes (decision 14). */
static void test_buffers_off_refuses_the_form_and_the_record(void) {
  static const uint8_t payload[] = {'F', 'R', 'P', 'O', 6,   10,  0,   0,
                                    4,   0,   2,   0,   'h', 'i', 0xff};
  runtime_marks_t before;

  expect_ok("keep is 7");
  before = runtime_marks();
  expect_refused("b is buffer: 4", FR_ERR_UNSUPPORTED,
                 "buffers are not enabled in this build\n");
  TEST_ASSERT_FALSE(name_exists("b"));
  expect_unchanged(before);
  TEST_ASSERT_EQUAL(FR_ERR_UNSUPPORTED,
                    fr_persist_payload_restore(&s_runtime, payload,
                                               (uint16_t)sizeof(payload)));
  expect_unchanged(before);
}

#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_buffer_is_a_reserved_word);
#if FR_FEATURE_BUFFERS
  RUN_TEST(test_buffer_words_read_and_write);
  RUN_TEST(test_buffer_takes_each_value_kind);
  RUN_TEST(test_buffer_fit_and_copy_rules);
  RUN_TEST(test_buffer_with_other_words);
  RUN_TEST(test_buffer_holders);
  RUN_TEST(test_buffer_definition_reuses_the_buffer);
  RUN_TEST(test_buffer_definition_with_a_full_object_table);
  RUN_TEST(test_buffer_definition_with_a_full_pool);
  RUN_TEST(test_buffer_definition_refusals);
  RUN_TEST(test_buffer_pool_accounting);
  RUN_TEST(test_buffer_lifetime);
  RUN_TEST(test_buffer_tiers);
  RUN_TEST(test_buffer_survives_a_failed_save);
  RUN_TEST(test_buffer_full_pool_saves_and_mounts);
  RUN_TEST(test_buffer_runtime_copy_rebases);
  RUN_TEST(test_buffer_many_passes_use_no_storage);
  RUN_TEST(test_buffer_overflow_in_an_event_body);
  RUN_TEST(test_buffer_parameters_are_the_ten_of_decision_5);
  RUN_TEST(test_buffer_records_are_checked);
#else
  RUN_TEST(test_buffers_off_refuses_the_form_and_the_record);
#endif
  return UNITY_END();
}
