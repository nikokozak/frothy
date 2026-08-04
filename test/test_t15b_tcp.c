/*
 * Unity tests for T15b TCP natives.
 *
 * Drives the seven natives through fr_repl_eval_line against the host stub.
 * It covers listener ownership, idle accept, accepted client I/O, and cleanup.
 * It also covers DNS, timeout, refused, and disconnected errors.
 * The tests cover partial reads, EOF, bytes-ready, handle exhaustion,
 * Wi-Fi-down latching, and Ctrl-C interrupts.
 */

#include "base_image.h"
#include "handle.h"
#include "platform.h"
#include "repl.h"
#include "runtime.h"
#include "types.h"

#include "unity/unity.h"

#include <stdint.h>

void setUp(void) {
#if FR_FEATURE_NET
  fr_host_net_reset();
#endif
}

void tearDown(void) {}

#if FR_FEATURE_NET

static fr_runtime_t s_runtime;

static void install_base(void) {
  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
}

static void eval_ok(const char *line) {
  char out[128];
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&s_runtime, line, out, sizeof(out)));
}

static fr_err_t eval_err(const char *line) {
  char out[128];
  return fr_repl_eval_line(&s_runtime, line, out, sizeof(out));
}

static void eval_expect_output(const char *line, const char *expected) {
  char out[256];
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_repl_eval_line(&s_runtime, line, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(expected, out);
}

static void test_open_write_read_close_happy_path(void) {
  const uint8_t body[2] = {'h', 'i'};
  uint8_t drained[8] = {0};
  uint16_t drained_len = 0;

  install_base();
  fr_host_tcp_queue_response(0, body, 2);

  eval_ok("sock is tcp.open: \"example.com\", 80");
  eval_ok("tcp.write: sock, \"GET\"");
  eval_expect_output("text.pack: tcp.read: sock, 100", "\"hi\"\nok\n");

  TEST_ASSERT_EQUAL(FR_OK,
                    fr_host_tcp_drain_writes(0, drained, sizeof(drained),
                                             &drained_len));
  TEST_ASSERT_EQUAL_UINT16(3, drained_len);
  TEST_ASSERT_EQUAL_MEMORY("GET", drained, 3);

  eval_ok("tcp.close: sock");
}

static void test_listen_accept_lifecycle(void) {
  const uint8_t body[2] = {'h', 'i'};
  uint8_t drained[8] = {0};
  uint16_t drained_length = 0;

  install_base();
  eval_ok("server is tcp.listen: 80");
  TEST_ASSERT_EQUAL(FR_OK, fr_host_tcp_queue_incoming(body, sizeof(body)));
  eval_ok("client is tcp.accept: server");
  eval_expect_output("tcp.available: client", "2\nok\n");
  eval_expect_output("text.pack: tcp.read: client, 8", "\"hi\"\nok\n");
  eval_ok("tcp.write: client, \"OK\"");
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_host_tcp_drain_writes(0, drained, sizeof(drained),
                                             &drained_length));
  TEST_ASSERT_EQUAL_UINT16(2, drained_length);
  TEST_ASSERT_EQUAL_MEMORY("OK", drained, drained_length);
  eval_ok("tcp.close: client");
  eval_ok("tcp.close: server");
}

static void test_accept_returns_nil_when_idle(void) {
  fr_slot_id_t idle_slot = 0;
  fr_tagged_t idle = 0;

  install_base();
  eval_ok("server is tcp.listen: 80");
  eval_ok("idle is tcp.accept: server");
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_slot_id_for_name(&s_runtime, "idle", &idle_slot));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_read(&s_runtime, idle_slot, &idle));
  TEST_ASSERT_TRUE(fr_tagged_is_nil(idle));
}

static void test_relisten_same_port_returns_same_handle(void) {
  fr_slot_id_t first_slot = 0;
  fr_slot_id_t second_slot = 0;
  fr_tagged_t first = fr_tagged_nil();
  fr_tagged_t second = fr_tagged_nil();

  install_base();
  eval_ok("first is tcp.listen: 80");
  eval_ok("second is tcp.listen: 80");
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_slot_id_for_name(&s_runtime, "first", &first_slot));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_slot_id_for_name(&s_runtime, "second", &second_slot));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_read(&s_runtime, first_slot, &first));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_read(&s_runtime, second_slot, &second));
  TEST_ASSERT_EQUAL_HEX32(first, second);
}

static void test_listen_different_port_is_busy(void) {
  install_base();
  eval_ok("server is tcp.listen: 80");
  TEST_ASSERT_EQUAL(FR_ERR_BUSY, eval_err("tcp.listen: 81"));
}

static void test_handle_table_exhaustion_does_not_accept_client(void) {
  const uint8_t body = 'x';
  fr_handle_ref_t refs[FR_PROFILE_MAX_HANDLES - 1u];
  fr_tagged_t tagged = fr_tagged_nil();
  fr_slot_id_t client_slot = 0;

  install_base();
  eval_ok("server is tcp.listen: 80");
  TEST_ASSERT_EQUAL(FR_OK, fr_host_tcp_queue_incoming(&body, 1));
  for (uint16_t i = 0; i < FR_PROFILE_MAX_HANDLES - 1u; i++) {
    TEST_ASSERT_EQUAL(
        FR_OK,
        fr_handle_reserve(&s_runtime, FR_HANDLE_KIND_UART, &refs[i], &tagged));
  }
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY, eval_err("tcp.accept: server"));

  TEST_ASSERT_EQUAL(FR_OK, fr_handle_release_reserved(&s_runtime, refs[0]));
  eval_ok("client is tcp.accept: server");
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_slot_id_for_name(&s_runtime, "client", &client_slot));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_read(&s_runtime, client_slot, &tagged));
  TEST_ASSERT_EQUAL(FR_TAGGED_HANDLE, fr_tagged_kind(tagged));
  eval_ok("tcp.close: client");
  for (uint16_t i = 1; i < FR_PROFILE_MAX_HANDLES - 1u; i++) {
    TEST_ASSERT_EQUAL(FR_OK,
                      fr_handle_release_reserved(&s_runtime, refs[i]));
  }
}

static void test_close_handles_closes_server_handle(void) {
  fr_slot_id_t server_slot = 0;
  fr_slot_id_t replacement_slot = 0;
  fr_tagged_t server = fr_tagged_nil();
  fr_tagged_t replacement = fr_tagged_nil();

  install_base();
  eval_ok("server is tcp.listen: 80");
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_slot_id_for_name(&s_runtime, "server", &server_slot));
  TEST_ASSERT_EQUAL(FR_OK, fr_slot_read(&s_runtime, server_slot, &server));
  eval_expect_output("close-handles", "1\nok\n");
  TEST_ASSERT_EQUAL(FR_ERR_HANDLE, eval_err("tcp.accept: server"));
  eval_ok("replacement is tcp.listen: 80");
  TEST_ASSERT_EQUAL(
      FR_OK,
      fr_slot_id_for_name(&s_runtime, "replacement", &replacement_slot));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_slot_read(&s_runtime, replacement_slot, &replacement));
  TEST_ASSERT_NOT_EQUAL(server, replacement);
  eval_ok("tcp.close: replacement");
  eval_ok("other is tcp.listen: 81");
}

static void test_platform_accept_drops_client_when_tcp_slots_full(void) {
  const uint8_t body = 'x';
  uint16_t listener_index = 0;
  uint16_t connection_indices[FR_TCP_HANDLE_COUNT];
  uint16_t accepted_index = 0;
  bool accepted = true;

  install_base();
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_platform_tcp_listen(&s_runtime, 80, &listener_index));
  for (uint16_t i = 0; i < FR_TCP_HANDLE_COUNT; i++) {
    fr_host_tcp_queue_response(i, &body, 1);
    TEST_ASSERT_EQUAL(FR_OK,
                      fr_platform_tcp_open(&s_runtime, "example.com", 80,
                                           &connection_indices[i]));
  }
  TEST_ASSERT_EQUAL(FR_OK, fr_host_tcp_queue_incoming(&body, 1));
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY,
                    fr_platform_tcp_accept(&s_runtime, listener_index,
                                           &accepted_index, &accepted));
  TEST_ASSERT_FALSE(accepted);

  TEST_ASSERT_EQUAL(FR_OK, fr_platform_tcp_close(connection_indices[0]));
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_platform_tcp_accept(&s_runtime, listener_index,
                                           &accepted_index, &accepted));
  TEST_ASSERT_FALSE(accepted);
  for (uint16_t i = 1; i < FR_TCP_HANDLE_COUNT; i++) {
    TEST_ASSERT_EQUAL(FR_OK,
                      fr_platform_tcp_close(connection_indices[i]));
  }
  TEST_ASSERT_EQUAL(FR_OK, fr_platform_tcp_server_close(listener_index));
}

static void test_open_dns_failure(void) {
  install_base();
  TEST_ASSERT_EQUAL(FR_ERR_NET_DNS,
                    eval_err("tcp.open: \"fr.test.dns\", 80"));
}

static void test_open_timeout(void) {
  install_base();
  TEST_ASSERT_EQUAL(FR_ERR_NET_TIMEOUT,
                    eval_err("tcp.open: \"fr.test.timeout\", 80"));
}

static void test_open_refused_without_queue(void) {
  install_base();
  TEST_ASSERT_EQUAL(FR_ERR_NET_REFUSED,
                    eval_err("tcp.open: \"example.com\", 80"));
}

static void test_partial_read_returns_available_bytes(void) {
  const uint8_t body[3] = {'a', 'b', 'c'};

  install_base();
  fr_host_tcp_queue_response(0, body, 3);
  eval_ok("sock is tcp.open: \"example.com\", 80");
  eval_expect_output("text.pack: tcp.read: sock, 1024", "\"abc\"\nok\n");
}

static void test_eof_returns_empty_text(void) {
  const uint8_t body[2] = {'h', 'i'};

  install_base();
  fr_host_tcp_queue_response(0, body, 2);
  eval_ok("sock is tcp.open: \"example.com\", 80");
  eval_expect_output("text.pack: tcp.read: sock, 100", "\"hi\"\nok\n");
  eval_expect_output("text.pack: tcp.read: sock, 100", "\"\"\nok\n");
}

static void test_bytes_ready_tracks_queue_drain(void) {
  const uint8_t body[5] = {'a', 'b', 'c', 'd', 'e'};

  install_base();
  fr_host_tcp_queue_response(0, body, 5);
  eval_ok("sock is tcp.open: \"example.com\", 80");
  eval_expect_output("tcp.available: sock", "5\nok\n");
  eval_expect_output("text.pack: tcp.read: sock, 2", "\"ab\"\nok\n");
  eval_expect_output("tcp.available: sock", "3\nok\n");
}

static void test_force_disconnect_surfaces_on_next_op(void) {
  const uint8_t body[2] = {'h', 'i'};

  install_base();
  fr_host_tcp_queue_response(0, body, 2);
  eval_ok("sock is tcp.open: \"example.com\", 80");
  fr_host_tcp_force_disconnect(0);
  TEST_ASSERT_EQUAL(FR_ERR_NET_DISCONNECTED,
                    eval_err("tcp.read: sock, 100"));
  /* D12: failed flag latches so a later call still surfaces disconnected
   * even though the host stub's wifi_down has not been cleared. */
  TEST_ASSERT_TRUE(s_runtime.tcp_handles[0].failed);
  TEST_ASSERT_EQUAL(FR_ERR_NET_DISCONNECTED,
                    eval_err("tcp.available: sock"));
}

static void test_close_then_reopen_clears_failed(void) {
  const uint8_t body[2] = {'h', 'i'};

  install_base();
  fr_host_tcp_queue_response(0, body, 2);
  eval_ok("s1 is tcp.open: \"example.com\", 80");
  fr_host_tcp_force_disconnect(0);
  TEST_ASSERT_EQUAL(FR_ERR_NET_DISCONNECTED,
                    eval_err("tcp.read: s1, 100"));
  eval_ok("tcp.close: s1");
  fr_host_tcp_queue_response(0, body, 2);
  eval_ok("s2 is tcp.open: \"example.com\", 80");
  TEST_ASSERT_FALSE(s_runtime.tcp_handles[0].failed);
  eval_expect_output("text.pack: tcp.read: s2, 100", "\"hi\"\nok\n");
  eval_ok("tcp.close: s2");
}

static void test_handle_exhaustion_returns_capacity(void) {
  const uint8_t one = '.';

  install_base();
  for (uint16_t i = 0; i < FR_TCP_HANDLE_COUNT; i++) {
    fr_host_tcp_queue_response(i, &one, 1);
  }
  eval_ok("a is tcp.open: \"example.com\", 80");
  eval_ok("b is tcp.open: \"example.com\", 80");
  eval_ok("c is tcp.open: \"example.com\", 80");
  eval_ok("d is tcp.open: \"example.com\", 80");
  TEST_ASSERT_EQUAL(FR_ERR_CAPACITY,
                    eval_err("tcp.open: \"example.com\", 80"));
}

static void test_interrupt_during_read_returns_interrupted(void) {
  const uint8_t body[2] = {'h', 'i'};
  uint16_t platform_index = 0;
  uint8_t buf[16] = {0};
  uint16_t length = 0;

  TEST_ASSERT_EQUAL(FR_OK, fr_base_image_install(&s_runtime));
  fr_host_tcp_queue_response(0, body, 2);
  TEST_ASSERT_EQUAL(FR_OK,
                    fr_platform_tcp_open(&s_runtime, "example.com", 80,
                                         &platform_index));
  fr_runtime_interrupt(&s_runtime);
  TEST_ASSERT_EQUAL(FR_ERR_INTERRUPTED,
                    fr_platform_tcp_read(&s_runtime, platform_index, buf,
                                         sizeof(buf), &length));
  fr_runtime_clear_interrupt(&s_runtime);
  TEST_ASSERT_EQUAL(FR_OK, fr_platform_tcp_close(platform_index));
}

#endif /* FR_FEATURE_NET */

int main(void) {
  UNITY_BEGIN();
#if FR_FEATURE_NET
  RUN_TEST(test_open_write_read_close_happy_path);
  RUN_TEST(test_listen_accept_lifecycle);
  RUN_TEST(test_accept_returns_nil_when_idle);
  RUN_TEST(test_relisten_same_port_returns_same_handle);
  RUN_TEST(test_listen_different_port_is_busy);
  RUN_TEST(test_handle_table_exhaustion_does_not_accept_client);
  RUN_TEST(test_close_handles_closes_server_handle);
  RUN_TEST(test_platform_accept_drops_client_when_tcp_slots_full);
  RUN_TEST(test_open_dns_failure);
  RUN_TEST(test_open_timeout);
  RUN_TEST(test_open_refused_without_queue);
  RUN_TEST(test_partial_read_returns_available_bytes);
  RUN_TEST(test_eof_returns_empty_text);
  RUN_TEST(test_bytes_ready_tracks_queue_drain);
  RUN_TEST(test_force_disconnect_surfaces_on_next_op);
  RUN_TEST(test_close_then_reopen_clears_failed);
  RUN_TEST(test_handle_exhaustion_returns_capacity);
  RUN_TEST(test_interrupt_during_read_returns_interrupted);
#endif
  return UNITY_END();
}
