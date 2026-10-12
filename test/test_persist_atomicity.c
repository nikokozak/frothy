/*
 * Unity persistence durability tests.
 *
 * S1: the core commits one complete envelope, and platform durability must
 * return the newest good image while falling back to an older good image when
 * the newer one is torn or corrupt.
 *
 * T10c: `see <function>` on a function with a text literal must render the
 * canonical `argN` form after a save/restore round-trip (text feature only).
 */

#include "base_image.h"
#include "code.h"
#include "crc.h"
#include "event.h"
#include "instruction.h"
#include "persist.h"
#include "persist_format.h"
#include "platform.h"
#include "repl.h"
#include "runtime.h"
#include "slot.h"
#include "tagged.h"
#include "vm.h"

#include "unity/unity.h"

#include <stdint.h>
#include <string.h>

static fr_runtime_t s_runtime;

static fr_err_t test_persist_commit_image(const uint8_t *image,
                                          uint16_t image_length) {
  fr_err_t err = FR_OK;

  if (image == NULL) {
    return FR_ERR_INVALID;
  }
  if (image_length < FR_PERSIST_HEADER_BYTES) {
    return FR_ERR_CORRUPT;
  }
  err = fr_platform_persist_stream_begin();
  if (err != FR_OK) {
    return err;
  }
  err = fr_platform_persist_stream_write(
      &image[FR_PERSIST_HEADER_BYTES],
      (uint16_t)(image_length - FR_PERSIST_HEADER_BYTES));
  if (err == FR_OK) {
    err = fr_platform_persist_stream_finalize(image);
  }
  if (err != FR_OK) {
    fr_platform_persist_stream_abort();
  }
  return err;
}

static void save_boot_one_after_nil_save(void) {
  char buf[128];

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&s_runtime, "boot is fn [ one ]", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));
}

static void assert_restored_boot_is_one(void) {
  fr_runtime_t restore_runtime;
  fr_tagged_t tagged = 0;
  fr_int_t decoded = 0;

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&restore_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore(&restore_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_vm_run_boot(&restore_runtime, &tagged));
  TEST_ASSERT_EQUAL(FR_OK, fr_tagged_decode_int(tagged, &decoded));
  TEST_ASSERT_EQUAL(1, decoded);
}

static void assert_restored_boot_is_nil(void) {
  fr_runtime_t restore_runtime;
  fr_tagged_t tagged = 0;

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&restore_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore(&restore_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_vm_run_boot(&restore_runtime, &tagged));
  TEST_ASSERT_TRUE(fr_tagged_is_nil(tagged));
}

static void assert_all_code_headers_read(fr_runtime_t *runtime) {
  for (fr_code_object_id_t code_id = 0; code_id < runtime->code.count;
       code_id++) {
    fr_instruction_stream_t view;
    fr_instruction_header_t header = {0};

    TEST_ASSERT_EQUAL(FR_OK, fr_code_get_instructions(runtime, code_id, &view));
    TEST_ASSERT_EQUAL(FR_OK, fr_instruction_read_header(&view, &header));
  }
}

void setUp(void) { TEST_ASSERT_EQUAL(FR_OK, fr_platform_persist_clear()); }

void tearDown(void) {}

static void test_read_returns_newest_good_image(void) {
  save_boot_one_after_nil_save();
  assert_restored_boot_is_one();
}

static void test_mid_commit_power_loss_keeps_previous_good_image(void) {
  uint8_t image[FR_PROFILE_PERSISTENCE_BYTES];
  uint16_t image_length = 0;

  save_boot_one_after_nil_save();
  TEST_ASSERT_EQUAL(FR_OK, fr_platform_persist_read(
                               image, (uint16_t)sizeof(image), &image_length,
                               0));
  TEST_ASSERT_GREATER_THAN(FR_PERSIST_HEADER_BYTES + 1u, image_length);

  fr_host_persist_debug_interrupt_next_header_write();
  TEST_ASSERT_EQUAL(FR_ERR_IO, test_persist_commit_image(image, image_length));

  assert_restored_boot_is_one();
}

static void test_bad_payload_newer_image_falls_back_to_older_good_image(void) {
  uint8_t image[FR_PROFILE_PERSISTENCE_BYTES];
  uint16_t image_length = 0;
  fr_persist_format_info_t info = {0};
  const uint16_t version_offset = (uint16_t)(FR_PERSIST_HEADER_BYTES + 4u);

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_platform_persist_read(
                               image, (uint16_t)sizeof(image), &image_length,
                               0));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_persist_format_validate(image, image_length, &info));
  TEST_ASSERT_GREATER_THAN(version_offset, image_length);

  image[version_offset] ^= 0x5au;
  TEST_ASSERT_EQUAL(
      FR_OK, fr_persist_format_build_header(
                 image, info.payload_length,
                 fr_crc32(&image[FR_PERSIST_HEADER_BYTES],
                          info.payload_length)));
  TEST_ASSERT_EQUAL(FR_OK, test_persist_commit_image(image, info.total_length));

  assert_restored_boot_is_nil();
}

static void test_corrupt_newer_image_falls_back_to_older_good_image(void) {
  uint8_t image[FR_PROFILE_PERSISTENCE_BYTES];
  uint16_t image_length = 0;
  uint16_t offset = (uint16_t)(FR_PERSIST_HEADER_BYTES + 5u);

  save_boot_one_after_nil_save();
  TEST_ASSERT_EQUAL(FR_OK, fr_platform_persist_read(
                               image, (uint16_t)sizeof(image), &image_length,
                               0));
  TEST_ASSERT_GREATER_THAN(offset, image_length);

  TEST_ASSERT_EQUAL(FR_OK,
                    fr_host_persist_debug_corrupt_newest(
                        offset, (uint8_t)(image[offset] ^ 0x5au)));

  assert_restored_boot_is_nil();
}

/* ADR 0090: the slot that a save writes. It is never the mounted slot. */
static void test_commit_slot_choice(void) {
  static const struct {
    bool valid0;
    bool valid1;
    uint32_t generation0;
    uint32_t generation1;
    uint8_t mounted_slot;
    uint8_t slot;
    uint32_t generation;
  } cases[] = {
      {false, false, 0, 0, FR_PERSIST_NO_MOUNTED_SLOT, 0, 1},
      {true, false, 5, 0, FR_PERSIST_NO_MOUNTED_SLOT, 1, 6},
      {false, true, 0, 5, FR_PERSIST_NO_MOUNTED_SLOT, 0, 6},
      {true, true, 3, 5, FR_PERSIST_NO_MOUNTED_SLOT, 0, 6},
      {true, true, 5, 3, FR_PERSIST_NO_MOUNTED_SLOT, 1, 6},
      {true, true, 4, 4, FR_PERSIST_NO_MOUNTED_SLOT, 0, 5},
      {true, true, 3, 5, 0, 1, 6},
      {true, true, 3, 5, 1, 0, 6},
      {true, false, 5, 0, 0, 1, 6},
      {false, true, 0, 5, 1, 0, 6},
  };

  for (uint8_t i = 0; i < (uint8_t)(sizeof(cases) / sizeof(cases[0])); i++) {
    bool valid[2] = {cases[i].valid0, cases[i].valid1};
    fr_persist_format_info_t info[2] = {{0}};
    uint8_t slot = 0xff;
    uint32_t generation = 0;

    info[0].backend_generation = cases[i].generation0;
    info[1].backend_generation = cases[i].generation1;
    fr_persist_format_pick_commit_slot(valid, info, cases[i].mounted_slot,
                                       &slot, &generation);
    TEST_ASSERT_EQUAL_UINT8(cases[i].slot, slot);
    TEST_ASSERT_EQUAL_UINT32(cases[i].generation, generation);
  }
}

#if FR_FEATURE_COMPILER && FR_PROFILE_MAX_OVERLAY_NAMES > 0
/* ADR 0090: after a fallback to an older image, a save reads the records
 * that it keeps from the mounted image and writes the other slot, so the
 * mounted image stays. Before, the save read the newer bad image and wrote
 * the slot that the runtime was mounted on. */
static void test_save_after_fallback_keeps_the_mounted_image(void) {
  uint8_t image[FR_PROFILE_PERSISTENCE_BYTES];
  uint8_t mounted[FR_PROFILE_PERSISTENCE_BYTES];
  uint8_t older[FR_PROFILE_PERSISTENCE_BYTES];
  uint16_t image_length = 0;
  uint16_t older_length = 0;
  fr_persist_format_info_t info = {0};
  const uint16_t version_offset = (uint16_t)(FR_PERSIST_HEADER_BYTES + 4u);
  char buf[128];

  save_boot_one_after_nil_save();
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_platform_persist_read(image, (uint16_t)sizeof(image),
                                             &image_length, 0));
  memcpy(mounted, image, image_length);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_persist_format_validate(image, image_length, &info));
  image[version_offset] ^= 0x5au;
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_format_build_header(
                               image, info.payload_length,
                               fr_crc32(&image[FR_PERSIST_HEADER_BYTES],
                                        info.payload_length)));
  TEST_ASSERT_EQUAL(FR_OK, test_persist_commit_image(image, info.total_length));

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_repl_eval_line(&s_runtime, "two is fn [ 2 ]", buf,
                                             (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));

  TEST_ASSERT_EQUAL(FR_OK,
                    fr_platform_persist_read(older, (uint16_t)sizeof(older),
                                             &older_length, 1));
  TEST_ASSERT_EQUAL_UINT16(image_length, older_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mounted, older, image_length);
  assert_restored_boot_is_one();
  TEST_ASSERT_EQUAL(
      FR_OK, fr_repl_eval_line(&s_runtime, "two:", buf, (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("2\nok\n", buf);
}

/* ADR 0090: a save whose remount fails returns that error, and the ordinary
 * loop leaves a mounted program. Before, the runtime was left cleared. */
static void test_failed_save_remount_returns_its_error(void) {
  char buf[128];
  fr_tagged_t tagged = 0;
  fr_int_t decoded = 0;

  save_boot_one_after_nil_save();
  TEST_ASSERT_EQUAL(FR_OK, fr_repl_eval_line(&s_runtime, "two is fn [ 2 ]", buf,
                                             (uint16_t)sizeof(buf)));
  fr_host_persist_debug_fail_next_mount_commit();
  TEST_ASSERT_EQUAL(FR_ERR_IO, fr_persist_save(&s_runtime));

  TEST_ASSERT_EQUAL(
      FR_OK, fr_repl_eval_line(&s_runtime, "two:", buf, (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("2\nok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK, fr_vm_run_boot(&s_runtime, &tagged));
  TEST_ASSERT_EQUAL(FR_OK, fr_tagged_decode_int(tagged, &decoded));
  TEST_ASSERT_EQUAL(1, decoded);
}
#endif

#if FR_FEATURE_COMPILER && FR_FEATURE_EVENTS && FR_PROFILE_MAX_OVERLAY_NAMES > 0
static void save_counter_event_image(fr_runtime_t *runtime) {
  char buf[128];

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(runtime));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(runtime, "counter is 0", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(
                        runtime,
                        "tick is fn [ set counter to counter + 1 ]", buf,
                        (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(runtime,
                                      "boot is fn [ every 50 [ tick: ] ]", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(runtime, "boot:", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(runtime));
}

static void test_failed_restore_drops_candidate_code_without_fallback(void) {
  fr_runtime_t restore_runtime;
  uint16_t base_code_count = 0;
  char buf[128];

  save_counter_event_image(&s_runtime);

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&restore_runtime));
  base_code_count = restore_runtime.code.base_image_count;
  fr_host_event_debug_fail_next_timer_install();
  TEST_ASSERT_EQUAL(FR_ERR_IO, fr_persist_restore(&restore_runtime));
  TEST_ASSERT_EQUAL(base_code_count, restore_runtime.code.count);
  TEST_ASSERT_EQUAL(base_code_count, restore_runtime.code.image_count);
  assert_all_code_headers_read(&restore_runtime);
  TEST_ASSERT_EQUAL(FR_ERR_NOT_FOUND,
                    fr_repl_eval_line(&restore_runtime, "tick:", buf,
                                      (uint16_t)sizeof(buf)));
}

static void test_failed_restore_cleans_candidate_code_before_fallback(void) {
  fr_runtime_t restore_runtime;
  fr_event_binding_t *entry = NULL;
  char buf[128];

  save_counter_event_image(&s_runtime);

  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&s_runtime, "newer is fn [ 9 ]", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&restore_runtime));
  fr_host_event_debug_fail_next_timer_install();
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore(&restore_runtime));
  assert_all_code_headers_read(&restore_runtime);
  TEST_ASSERT_EQUAL(FR_ERR_NOT_FOUND,
                    fr_repl_eval_line(&restore_runtime, "newer:", buf,
                                      (uint16_t)sizeof(buf)));

  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&restore_runtime, "tick:", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&restore_runtime, "counter", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("1\nok\n", buf);
  entry = &restore_runtime.events.entries[0];
  TEST_ASSERT_EQUAL(FR_EVENT_KIND_EVERY, entry->kind);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_platform_event_post_test_candidate(0, entry->generation,
                                                          50));
  TEST_ASSERT_EQUAL(FR_OK, fr_event_drain(&restore_runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_event_dispatch(&restore_runtime));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&restore_runtime, "counter", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("2\nok\n", buf);
}
#endif

#if FR_FEATURE_COMPILER && FR_PROFILE_MAX_OVERLAY_NAMES > 0
static void save_library_and_user_words(fr_runtime_t *runtime) {
  char buf[128];

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(runtime));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(runtime, "install-library", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(runtime, "lib_word is fn [ one ]", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(runtime, "install-user", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(runtime,
                                      "usr_word is fn [ lib_word: ]", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(runtime));
}

static void test_restore_repoints_library_name_after_remount(void) {
  fr_runtime_t runtime;
  fr_slot_id_t lib_slot = 0;
  fr_slot_id_t usr_slot = 0;
  fr_slot_id_t resolved = 0;
  char buf[160];

  fr_platform_persist_clear();
  save_library_and_user_words(&runtime);
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&runtime, "lib_word",
                                               &lib_slot));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&runtime, "usr_word",
                                               &usr_slot));

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore_library(&runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore_user(&runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&runtime, "lib_word",
                                               &resolved));
  TEST_ASSERT_EQUAL(lib_slot, resolved);

  fr_host_persist_debug_shadow_mounts(true);
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore(&runtime));
  fr_host_persist_debug_shadow_mounts(false);

  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&runtime, "lib_word",
                                               &resolved));
  TEST_ASSERT_EQUAL(lib_slot, resolved);
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&runtime, "usr_word",
                                               &resolved));
  TEST_ASSERT_EQUAL(usr_slot, resolved);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&runtime, "see lib_word", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_NOT_NULL(strstr(buf, "to lib_word [ one ]"));
}

static void test_boot_user_slot_name_survives_clear(void) {
  fr_runtime_t runtime;
  fr_slot_id_t usr_slot = 0;
  fr_slot_id_t resolved = 0;
  char buf[128];

  fr_platform_persist_clear();
  save_library_and_user_words(&runtime);
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&runtime, "usr_word",
                                               &usr_slot));

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore_library(&runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore_user(&runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_runtime_clear_project(&runtime));

  TEST_ASSERT_EQUAL(FR_OK, fr_slot_id_for_name(&runtime, "usr_word",
                                               &resolved));
  TEST_ASSERT_EQUAL(usr_slot, resolved);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&runtime, "usr_word:", buf,
                                      (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("1\nok\n", buf);
}

static void test_failed_mount_commit_drops_candidate_slot_names(void) {
  fr_runtime_t runtime;
  char buf[128];
  fr_slot_id_t resolved = 0;

  fr_platform_persist_clear();
  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&s_runtime, "leak_word is fn [ one ]",
                                      buf, (uint16_t)sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("ok\n", buf);
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&s_runtime));

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&runtime));
  fr_host_persist_debug_fail_next_mount_commit();
  TEST_ASSERT_EQUAL(FR_ERR_IO, fr_persist_restore(&runtime));
  TEST_ASSERT_EQUAL(FR_ERR_NOT_FOUND,
                    fr_slot_id_for_name(&runtime, "leak_word", &resolved));
  TEST_ASSERT_EQUAL_UINT16(0, runtime.slots.overlay_name_count);
}
#endif

static void test_clear_reports_not_found(void) {
  fr_runtime_t restore_runtime;

  save_boot_one_after_nil_save();
  TEST_ASSERT_EQUAL(FR_OK, fr_platform_persist_clear());

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&restore_runtime));
  TEST_ASSERT_EQUAL(FR_ERR_NOT_FOUND, fr_persist_restore(&restore_runtime));
}

#if FR_FEATURE_TEXT
/* T10c: restore drops stored param names, so the renderer must fall back to
 * canonical argN display. The full REPL path catches both the renderer
 * result and the "source reconstruction unavailable" fallback line. */
static void test_see_after_restore_renders_canonical_arg_names(void) {
  fr_runtime_t runtime;
  char buf[128];

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&runtime));
  TEST_ASSERT_EQUAL(
      FR_OK, fr_repl_eval_line(
                 &runtime,
                 "to greet with x [ text.concat: \"led=\", text.from-int: x ]",
                 buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_save(&runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&runtime));
  TEST_ASSERT_EQUAL(FR_OK, fr_persist_restore(&runtime));
  TEST_ASSERT_EQUAL(
      FR_OK, fr_repl_eval_line(&runtime, "see greet", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING(
      "overlay code\n"
      "to greet with arg0 [ text.concat: \"led=\", text.from-int: arg0 ]\n"
      "ok\n",
      buf);
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_read_returns_newest_good_image);
  RUN_TEST(test_mid_commit_power_loss_keeps_previous_good_image);
  RUN_TEST(test_bad_payload_newer_image_falls_back_to_older_good_image);
  RUN_TEST(test_corrupt_newer_image_falls_back_to_older_good_image);
  RUN_TEST(test_commit_slot_choice);
#if FR_FEATURE_COMPILER && FR_PROFILE_MAX_OVERLAY_NAMES > 0
  RUN_TEST(test_save_after_fallback_keeps_the_mounted_image);
  RUN_TEST(test_failed_save_remount_returns_its_error);
#endif
#if FR_FEATURE_COMPILER && FR_FEATURE_EVENTS && FR_PROFILE_MAX_OVERLAY_NAMES > 0
  RUN_TEST(test_failed_restore_drops_candidate_code_without_fallback);
  RUN_TEST(test_failed_restore_cleans_candidate_code_before_fallback);
#endif
#if FR_FEATURE_COMPILER && FR_PROFILE_MAX_OVERLAY_NAMES > 0
  RUN_TEST(test_restore_repoints_library_name_after_remount);
  RUN_TEST(test_boot_user_slot_name_survives_clear);
  RUN_TEST(test_failed_mount_commit_drops_candidate_slot_names);
#endif
  RUN_TEST(test_clear_reports_not_found);
#if FR_FEATURE_TEXT
  RUN_TEST(test_see_after_restore_renders_canonical_arg_names);
#endif
  return UNITY_END();
}
