extern "C" {
#include "froth.h"
#include "platform.h"
#include "runtime.h"
}

#include <Arduino.h>
#if FR_FEATURE_BLE
#include <ArduinoBLE.h>
#include <utility/GAP.h>
#include <utility/HCI.h>
#endif
#if FR_FEATURE_NET || FR_FEATURE_BLE
#include <WiFiNINA.h>
#endif
#if FR_FEATURE_NET
#include <WiFiPreferences.h>
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  FR_NINA_WIFI_SSID_MAX = 32,
  FR_NINA_WIFI_PASS_MAX = 64,
  FR_NINA_WIFI_CONNECT_TIMEOUT_MS = 30000,
  FR_NINA_WIFI_CONNECT_SLICE_MS = 1000,
  FR_NINA_WIFI_STATUS_POLL_MS = 100,
  FR_NINA_HTTP_TIMEOUT_MS = 5000,
  FR_NINA_HTTP_LINE_BYTES = 256,
  FR_NINA_HOST_BYTES = 254,
  FR_NINA_TCP_OPEN_TIMEOUT_MS = 10000,
  FR_NINA_TCP_RW_TIMEOUT_MS = 5000,
  FR_NINA_TCP_CONNECT_SLICE_MS = 1000,
  /* Hardware calibration point: frequent enough to drain NINA HCI reports,
   * slow enough not to monopolize its shared SPI transport during TCP I/O. */
  FR_NINA_BLE_POLL_MS = 5,
  FR_NINA_BLE_RSSI_MIN = -127,
  FR_NINA_BLE_RSSI_MAX = 20,
  FR_NINA_BLE_SCAN_INTERVAL_MIN_MS = 3,
  FR_NINA_BLE_SCAN_INTERVAL_MAX_MS = 10240,
  FR_NINA_BLE_ADVERTISE_INTERVAL_MIN_MS = 20,
  FR_NINA_BLE_ADVERTISE_INTERVAL_MAX_MS = 10240,
  FR_NINA_FIRMWARE_BLE_MIN = 0x030000,
};

#if FR_FEATURE_NET
typedef struct fr_nina_credentials_t {
  uint8_t version;
  uint8_t ssid_length;
  uint8_t pass_length;
  uint8_t reserved;
  char ssid[FR_NINA_WIFI_SSID_MAX];
  char pass[FR_NINA_WIFI_PASS_MAX];
} fr_nina_credentials_t;

static_assert(sizeof(fr_nina_credentials_t) == 100,
              "NINA credential blob layout changed");

typedef struct fr_nina_tcp_t {
  WiFiClient client;
  bool in_use;
} fr_nina_tcp_t;

static fr_nina_tcp_t fr_nina_tcps[FR_TCP_HANDLE_COUNT];
#endif

#if FR_FEATURE_BLE
typedef struct fr_nina_ble_t {
  fr_ble_radio_state_t radio_state;
  uint32_t lifecycle_generation;
  bool own_address_valid;
  fr_ble_address_type_t own_address_type;
  uint8_t own_address[6];

#if FR_BLE_ENABLE_OBSERVER
  fr_ble_scan_state_t scan_state;
  uint32_t scan_generation;
  uint16_t requested_interval_ms;
  uint16_t requested_window_ms;
  uint32_t actual_interval_us;
  uint32_t actual_window_us;
  int8_t minimum_rssi;
  bool active_scan;
  bool repeats;
  fr_ble_scan_report_t queue[FR_BLE_SCAN_QUEUE_COUNT];
  uint8_t queue_head;
  uint8_t queue_count;
  uint8_t queue_high_water;
  uint32_t received;
  uint32_t accepted;
  uint32_t filtered_rssi;
  uint32_t dequeued;
  uint32_t dropped;
  uint32_t malformed;
  fr_ble_scan_report_t current;
  bool current_valid;
#endif

#if FR_BLE_ENABLE_BROADCASTER
  fr_ble_advertise_state_t advertise_state;
  uint16_t advertise_requested_interval_ms;
  uint32_t advertise_actual_interval_us;
  bool advertise_connectable;
  uint8_t advertising_data[FR_BLE_ADVERTISEMENT_DATA_BYTES];
  uint8_t advertising_data_length;
  uint8_t scan_response_data[FR_BLE_ADVERTISEMENT_DATA_BYTES];
  uint8_t scan_response_data_length;
  uint32_t advertise_starts;
  uint32_t advertise_stops;
#endif

  fr_ble_operation_t last_operation;
  fr_err_t last_result;
  int32_t last_platform_code;
  int32_t last_protocol_reason;
  uint32_t last_operation_ms;
  uint32_t reset_count;
  uint32_t late_callback_count;
} fr_nina_ble_t;

static fr_nina_ble_t fr_nina_ble;
static uint32_t fr_nina_ble_last_poll_ms;

static uint16_t fr_nina_gap_units(uint16_t milliseconds) {
  return (uint16_t)(((uint32_t)milliseconds * 1000u + 312u) / 625u);
}

static void fr_nina_ble_record(fr_ble_operation_t operation, fr_err_t result,
                               int32_t platform_code) {
  fr_nina_ble.last_operation = operation;
  fr_nina_ble.last_result = result;
  fr_nina_ble.last_platform_code = platform_code;
  fr_nina_ble.last_operation_ms = (uint32_t)millis();
}

#if FR_BLE_ENABLE_OBSERVER
static bool fr_nina_ble_address_type(uint8_t raw_type,
                                     fr_ble_address_type_t *out_type) {
  if (out_type == NULL || raw_type > FR_BLE_ADDRESS_RANDOM_ID) {
    return false;
  }
  *out_type = (fr_ble_address_type_t)raw_type;
  return true;
}

static bool fr_nina_ble_report_flags(uint8_t event_type, uint8_t *out_flags) {
  uint8_t flags = FR_BLE_REPORT_LEGACY;

  if (out_flags == NULL) {
    return false;
  }
  switch (event_type) {
  case 0:
    flags |= FR_BLE_REPORT_CONNECTABLE | FR_BLE_REPORT_SCANNABLE;
    break;
  case 1:
    flags |= FR_BLE_REPORT_CONNECTABLE | FR_BLE_REPORT_DIRECTED;
    break;
  case 2:
    flags |= FR_BLE_REPORT_SCANNABLE;
    break;
  case 3:
    break;
  case 4:
    flags |= FR_BLE_REPORT_SCANNABLE | FR_BLE_REPORT_SCAN_RESPONSE;
    break;
  default:
    return false;
  }
  *out_flags = flags;
  return true;
}

static void fr_nina_ble_clear_reports(void) {
  memset(fr_nina_ble.queue, 0, sizeof(fr_nina_ble.queue));
  fr_nina_ble.queue_head = 0;
  fr_nina_ble.queue_count = 0;
  fr_nina_ble.queue_high_water = 0;
  fr_nina_ble.received = 0;
  fr_nina_ble.accepted = 0;
  fr_nina_ble.filtered_rssi = 0;
  fr_nina_ble.dequeued = 0;
  fr_nina_ble.dropped = 0;
  fr_nina_ble.malformed = 0;
  memset(&fr_nina_ble.current, 0, sizeof(fr_nina_ble.current));
  fr_nina_ble.current_valid = false;
}

static void fr_nina_ble_clear_scan(void) {
  fr_nina_ble.scan_state = FR_BLE_SCAN_IDLE;
  fr_nina_ble.requested_interval_ms = 0;
  fr_nina_ble.requested_window_ms = 0;
  fr_nina_ble.actual_interval_us = 0;
  fr_nina_ble.actual_window_us = 0;
  fr_nina_ble.minimum_rssi = 0;
  fr_nina_ble.active_scan = false;
  fr_nina_ble.repeats = false;
  fr_nina_ble_clear_reports();
}

static void fr_nina_ble_receive_report(uint8_t event_type,
                                       uint8_t raw_address_type,
                                       const uint8_t raw_address[6],
                                       uint8_t data_length, const uint8_t *data,
                                       int8_t rssi) {
  fr_ble_scan_report_t report = {};
  uint8_t tail = 0;

  if (fr_nina_ble.scan_state != FR_BLE_SCAN_ACTIVE) {
    fr_nina_ble.late_callback_count += 1u;
    return;
  }

  fr_nina_ble.received += 1u;
  if (raw_address == NULL ||
      !fr_nina_ble_address_type(raw_address_type, &report.address_type) ||
      !fr_nina_ble_report_flags(event_type, &report.flags) ||
      rssi < FR_NINA_BLE_RSSI_MIN || rssi > FR_NINA_BLE_RSSI_MAX ||
      data_length > FR_BLE_SCAN_DATA_BYTES ||
      (data_length > 0 && data == NULL)) {
    fr_nina_ble.malformed += 1u;
    return;
  }
  if (rssi < fr_nina_ble.minimum_rssi) {
    fr_nina_ble.filtered_rssi += 1u;
    return;
  }

  for (uint8_t i = 0; i < sizeof(report.address); i++) {
    report.address[i] = raw_address[sizeof(report.address) - 1u - i];
  }
  report.rssi = rssi;
  report.data_length = data_length;
  if (data_length > 0) {
    memcpy(report.data, data, data_length);
  }
  report.timestamp_ms = (uint32_t)millis();

  if (fr_nina_ble.queue_count == FR_BLE_SCAN_QUEUE_COUNT) {
    fr_nina_ble.queue_head =
        (uint8_t)((fr_nina_ble.queue_head + 1u) % FR_BLE_SCAN_QUEUE_COUNT);
    fr_nina_ble.queue_count -= 1u;
    fr_nina_ble.dropped += 1u;
  }
  tail = (uint8_t)((fr_nina_ble.queue_head + fr_nina_ble.queue_count) %
                   FR_BLE_SCAN_QUEUE_COUNT);
  fr_nina_ble.queue[tail] = report;
  fr_nina_ble.queue_count += 1u;
  fr_nina_ble.accepted += 1u;
  if (fr_nina_ble.queue_count > fr_nina_ble.queue_high_water) {
    fr_nina_ble.queue_high_water = fr_nina_ble.queue_count;
  }
}
#endif

class FrothyGapClass : public GAPClass {
public:
#if FR_BLE_ENABLE_OBSERVER
  int startRawScan(bool active, uint16_t interval_units, uint16_t window_units,
                   bool repeats) {
    int result = 0;

    /* The NINA returns Command Disallowed when scanning is already off.
     * ArduinoBLE's own GAP::scan intentionally ignores this pre-stop too. */
    (void)HCI.leSetScanEnable(0, 0);
    result = HCI.leSetScanParameters(active ? 1 : 0, interval_units,
                                     window_units, 0, 0);
    if (result == 0) {
      result = HCI.leSetScanEnable(1, repeats ? 0 : 1);
    }
    return result;
  }

  int stopRawScan() { return HCI.leSetScanEnable(0, 0); }
#endif

protected:
  void handleLeAdvertisingReport(uint8_t event_type, uint8_t address_type,
                                 uint8_t address[6], uint8_t data_length,
                                 uint8_t data[], int8_t rssi) override {
#if FR_BLE_ENABLE_OBSERVER
    fr_nina_ble_receive_report(event_type, address_type, address, data_length,
                               data, rssi);
#else
    (void)event_type;
    (void)address_type;
    (void)address;
    (void)data_length;
    (void)data;
    (void)rssi;
#endif
  }
};

static FrothyGapClass fr_nina_gap;
GAPClass &GAP = fr_nina_gap;
#endif

extern "C" {

void fr_nina_poll(void) {
#if FR_FEATURE_BLE
  uint32_t now = (uint32_t)millis();

  if (fr_nina_ble.radio_state == FR_BLE_RADIO_READY &&
      (uint32_t)(now - fr_nina_ble_last_poll_ms) >= FR_NINA_BLE_POLL_MS) {
    fr_nina_ble_last_poll_ms = now;
    BLE.poll();
  }
#endif
}

#if FR_FEATURE_NET
static fr_err_t fr_nina_poll_interrupt(fr_runtime_t *runtime) {
  fr_err_t err = fr_platform_poll_interrupt(runtime);

  if (err != FR_OK) {
    return err;
  }
  return fr_runtime_is_interrupted(runtime) ? FR_ERR_INTERRUPTED : FR_OK;
}

static bool fr_nina_wifi_ready(void) { return WiFi.status() == WL_CONNECTED; }

static fr_err_t fr_nina_credentials_read(fr_nina_credentials_t *out) {
  Preferences preferences;
  size_t length = 0;

  if (out == NULL) {
    return FR_ERR_INVALID;
  }
  memset(out, 0, sizeof(*out));
  if (!preferences.begin("frothy_wifi", true)) {
    return FR_ERR_NET_DISCONNECTED;
  }
  length = preferences.getBytesLength("credentials");
  if (length != sizeof(*out) ||
      preferences.getBytes("credentials", out, sizeof(*out)) != sizeof(*out)) {
    return FR_ERR_NET_DISCONNECTED;
  }
  if (out->version != 1 || out->reserved != 0 || out->ssid_length == 0 ||
      out->ssid_length > FR_NINA_WIFI_SSID_MAX ||
      out->pass_length > FR_NINA_WIFI_PASS_MAX) {
    memset(out, 0, sizeof(*out));
    return FR_ERR_CORRUPT;
  }
  return FR_OK;
}

fr_err_t fr_platform_wifi_save(const char *ssid, const char *pass) {
  Preferences preferences;
  fr_nina_credentials_t credentials = {};
  size_t ssid_length = 0;
  size_t pass_length = 0;
  size_t written = 0;

  if (ssid == NULL || pass == NULL) {
    return FR_ERR_INVALID;
  }
  ssid_length = strlen(ssid);
  pass_length = strlen(pass);
  if (ssid_length == 0 || ssid_length > FR_NINA_WIFI_SSID_MAX ||
      pass_length > FR_NINA_WIFI_PASS_MAX) {
    return FR_ERR_DOMAIN;
  }

  credentials.version = 1;
  credentials.ssid_length = (uint8_t)ssid_length;
  credentials.pass_length = (uint8_t)pass_length;
  memcpy(credentials.ssid, ssid, ssid_length);
  memcpy(credentials.pass, pass, pass_length);

  if (!preferences.begin("frothy_wifi", false)) {
    return FR_ERR_IO;
  }
  written =
      preferences.putBytes("credentials", &credentials, sizeof(credentials));
  return written == sizeof(credentials) ? FR_OK : FR_ERR_IO;
}

fr_err_t fr_platform_wifi_connect(fr_runtime_t *runtime) {
  fr_nina_credentials_t credentials = {};
  char ssid[FR_NINA_WIFI_SSID_MAX + 1] = {};
  char pass[FR_NINA_WIFI_PASS_MAX + 1] = {};
  uint32_t started = 0;
  fr_err_t err = FR_OK;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  err = fr_nina_credentials_read(&credentials);
  if (err != FR_OK) {
    return err == FR_ERR_CORRUPT ? err : FR_ERR_NET_DISCONNECTED;
  }
  memcpy(ssid, credentials.ssid, credentials.ssid_length);
  memcpy(pass, credentials.pass, credentials.pass_length);

  WiFi.setTimeout(FR_NINA_WIFI_CONNECT_SLICE_MS);
  (void)WiFi.disconnect();
  started = (uint32_t)millis();
  /* WiFi.begin sends the association command before waiting for its timeout.
   * Send it once: repeating begin restarts association before DHCP finishes. */
  int status = credentials.pass_length == 0 ? WiFi.begin(ssid)
                                            : WiFi.begin(ssid, pass);
  for (;;) {
    if (status == WL_CONNECTED || fr_nina_wifi_ready()) {
      return FR_OK;
    }
    err = fr_nina_poll_interrupt(runtime);
    if (err != FR_OK) {
      return err;
    }
    if ((uint32_t)(millis() - started) >= FR_NINA_WIFI_CONNECT_TIMEOUT_MS) {
      return FR_ERR_NET_TIMEOUT;
    }
    delay(FR_NINA_WIFI_STATUS_POLL_MS);
    status = WiFi.status();
  }
}

fr_err_t fr_platform_wifi_ready(bool *out_ready) {
  if (out_ready == NULL) {
    return FR_ERR_INVALID;
  }
  *out_ready = fr_nina_wifi_ready();
  return FR_OK;
}

fr_err_t fr_platform_event_wifi_install(fr_event_kind_t kind,
                                        uint16_t binding_index,
                                        uint16_t generation) {
  (void)kind;
  (void)binding_index;
  (void)generation;
  return FR_ERR_UNSUPPORTED;
}

fr_err_t fr_platform_event_wifi_remove(uint16_t binding_index) {
  (void)binding_index;
  return FR_OK;
}

typedef struct fr_nina_url_t {
  bool tls;
  char host[FR_NINA_HOST_BYTES];
  const char *path;
  uint16_t port;
} fr_nina_url_t;

static fr_err_t fr_nina_url_parse(const char *url, fr_nina_url_t *out) {
  const char *host_start = NULL;
  const char *cursor = NULL;
  const char *path = NULL;
  const char *port_separator = NULL;
  size_t authority_length = 0;
  size_t host_length = 0;
  unsigned long port = 0;
  char *port_end = NULL;

  if (url == NULL || out == NULL) {
    return FR_ERR_INVALID;
  }
  for (const unsigned char *byte = (const unsigned char *)url; *byte != 0;
       byte++) {
    if (*byte <= 0x20 || *byte == 0x7f || *byte == '#') {
      return FR_ERR_NET_PROTOCOL;
    }
  }
  memset(out, 0, sizeof(*out));
  if (strncmp(url, "http://", 7) == 0) {
    out->tls = false;
    out->port = 80;
    host_start = url + 7;
  } else if (strncmp(url, "https://", 8) == 0) {
    out->tls = true;
    out->port = 443;
    host_start = url + 8;
  } else {
    return FR_ERR_NET_PROTOCOL;
  }

  cursor = host_start;
  while (*cursor != '\0' && *cursor != '/' && *cursor != '?' &&
         *cursor != '#') {
    if (*cursor == ':') {
      if (port_separator != NULL) {
        return FR_ERR_NET_PROTOCOL;
      }
      port_separator = cursor;
    }
    cursor++;
  }
  authority_length = (size_t)(cursor - host_start);
  if (authority_length == 0) {
    return FR_ERR_NET_PROTOCOL;
  }
  host_length = port_separator == NULL ? authority_length
                                       : (size_t)(port_separator - host_start);
  if (host_length == 0 || host_length >= sizeof(out->host)) {
    return FR_ERR_NET_PROTOCOL;
  }
  for (size_t i = 0; i < host_length; i++) {
    unsigned char byte = (unsigned char)host_start[i];

    if (!isalnum(byte) && byte != '.' && byte != '-') {
      return FR_ERR_NET_PROTOCOL;
    }
  }
  memcpy(out->host, host_start, host_length);
  out->host[host_length] = '\0';

  if (port_separator != NULL) {
    const char *port_start = port_separator + 1;

    if (port_start >= cursor) {
      return FR_ERR_NET_PROTOCOL;
    }
    port = strtoul(port_start, &port_end, 10);
    if (port_end != cursor || port == 0 || port > UINT16_MAX) {
      return FR_ERR_NET_PROTOCOL;
    }
    out->port = (uint16_t)port;
  }

  path = cursor;
  if (*path == '\0') {
    out->path = "/";
  } else {
    out->path = path;
  }
  return FR_OK;
}

static bool fr_nina_ascii_prefix(const char *text, const char *prefix) {
  while (*prefix != '\0') {
    if (*text == '\0' ||
        tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) {
      return false;
    }
    text++;
    prefix++;
  }
  return true;
}

static bool fr_nina_ascii_contains(const char *text, const char *needle) {
  size_t needle_length = strlen(needle);

  if (needle_length == 0) {
    return true;
  }
  for (; *text != '\0'; text++) {
    size_t i = 0;

    while (i < needle_length && text[i] != '\0' &&
           tolower((unsigned char)text[i]) ==
               tolower((unsigned char)needle[i])) {
      i++;
    }
    if (i == needle_length) {
      return true;
    }
  }
  return false;
}

static fr_err_t fr_nina_http_read_byte(WiFiClient *client, uint32_t started,
                                       uint8_t *out_byte, bool *out_eof) {
  if (client == NULL || out_byte == NULL || out_eof == NULL) {
    return FR_ERR_INVALID;
  }
  *out_eof = false;
  for (;;) {
    int available = client->available();

    if (available > 0) {
      int value = client->read();

      if (value < 0) {
        return FR_ERR_NET_PROTOCOL;
      }
      *out_byte = (uint8_t)value;
      return FR_OK;
    }
    if (!fr_nina_wifi_ready()) {
      return FR_ERR_NET_DISCONNECTED;
    }
    if (!client->connected()) {
      *out_eof = true;
      return FR_OK;
    }
    if ((uint32_t)(millis() - started) >= FR_NINA_HTTP_TIMEOUT_MS) {
      return FR_ERR_NET_TIMEOUT;
    }
    fr_nina_poll();
    delay(1);
  }
}

static fr_err_t fr_nina_http_read_line(WiFiClient *client, uint32_t started,
                                       char *line, size_t capacity) {
  size_t length = 0;

  if (line == NULL || capacity == 0) {
    return FR_ERR_INVALID;
  }
  for (;;) {
    uint8_t byte = 0;
    bool eof = false;
    fr_err_t err = fr_nina_http_read_byte(client, started, &byte, &eof);

    if (err != FR_OK) {
      return err;
    }
    if (eof) {
      return FR_ERR_NET_PROTOCOL;
    }
    if (byte == '\n') {
      if (length > 0 && line[length - 1] == '\r') {
        length--;
      }
      line[length] = '\0';
      return FR_OK;
    }
    if (byte == 0 || length + 1 >= capacity) {
      return FR_ERR_NET_PROTOCOL;
    }
    line[length++] = (char)byte;
  }
}

static fr_err_t fr_nina_http_read_exact(WiFiClient *client, uint32_t started,
                                        uint8_t *out, uint32_t length) {
  for (uint32_t i = 0; i < length; i++) {
    uint8_t byte = 0;
    bool eof = false;
    fr_err_t err = fr_nina_http_read_byte(client, started, &byte, &eof);

    if (err != FR_OK) {
      return err;
    }
    if (eof) {
      return FR_ERR_NET_PROTOCOL;
    }
    out[i] = byte;
  }
  return FR_OK;
}

static fr_err_t fr_nina_http_exchange(WiFiClient *client,
                                      const fr_nina_url_t *url,
                                      uint8_t *out_body, uint16_t capacity,
                                      uint16_t *out_length) {
  char line[FR_NINA_HTTP_LINE_BYTES];
  uint32_t started = (uint32_t)millis();
  uint32_t content_length = 0;
  uint32_t written = 0;
  int status = 0;
  bool content_length_known = false;
  bool chunked = false;

  client->setConnectionTimeout(FR_NINA_HTTP_TIMEOUT_MS);
  if (!(url->tls ? client->connectSSL(url->host, url->port)
                 : client->connect(url->host, url->port))) {
    return url->tls ? FR_ERR_NET_PROTOCOL : FR_ERR_NET_REFUSED;
  }

  if (client->print("GET ") == 0 ||
      (url->path[0] == '?' && client->print("/") == 0) ||
      client->print(url->path) == 0 ||
      client->print(" HTTP/1.1\r\nHost: ") == 0 ||
      client->print(url->host) == 0 ||
      ((url->tls ? url->port != 443 : url->port != 80) &&
       (client->print(":") == 0 || client->print(url->port) == 0)) ||
      client->print("\r\nConnection: close\r\nUser-Agent: Frothy\r\n\r\n") ==
          0) {
    return FR_ERR_NET_REFUSED;
  }

  {
    fr_err_t err = fr_nina_http_read_line(client, started, line, sizeof(line));
    if (err != FR_OK) {
      return err;
    }
  }
  if (sscanf(line, "HTTP/%*u.%*u %d", &status) != 1) {
    return FR_ERR_NET_PROTOCOL;
  }

  for (;;) {
    char *value = NULL;
    fr_err_t err = fr_nina_http_read_line(client, started, line, sizeof(line));

    if (err != FR_OK) {
      return err;
    }
    if (line[0] == '\0') {
      break;
    }
    if (fr_nina_ascii_prefix(line, "content-length:")) {
      char *end = NULL;
      unsigned long parsed = 0;

      value = line + strlen("content-length:");
      while (*value == ' ' || *value == '\t') {
        value++;
      }
      parsed = strtoul(value, &end, 10);
      while (*end == ' ' || *end == '\t') {
        end++;
      }
      if (end == value || *end != '\0' || parsed > UINT32_MAX) {
        return FR_ERR_NET_PROTOCOL;
      }
      content_length = (uint32_t)parsed;
      content_length_known = true;
    } else if (fr_nina_ascii_prefix(line, "transfer-encoding:")) {
      value = line + strlen("transfer-encoding:");
      chunked = fr_nina_ascii_contains(value, "chunked");
    }
  }

  if (status < 200 || status >= 300) {
    return FR_ERR_NET_REFUSED;
  }
  if (chunked) {
    for (;;) {
      char *end = NULL;
      unsigned long chunk_length = 0;
      fr_err_t err =
          fr_nina_http_read_line(client, started, line, sizeof(line));

      if (err != FR_OK) {
        return err;
      }
      chunk_length = strtoul(line, &end, 16);
      if (end == line ||
          (*end != '\0' && *end != ';' && !isspace((unsigned char)*end)) ||
          chunk_length > UINT32_MAX) {
        return FR_ERR_NET_PROTOCOL;
      }
      if (chunk_length == 0) {
        do {
          err = fr_nina_http_read_line(client, started, line, sizeof(line));
          if (err != FR_OK) {
            return err;
          }
        } while (line[0] != '\0');
        break;
      }
      if (chunk_length > (uint32_t)capacity - written) {
        return FR_ERR_NET_TOO_LARGE;
      }
      err = fr_nina_http_read_exact(client, started, out_body + written,
                                    (uint32_t)chunk_length);
      if (err != FR_OK) {
        return err;
      }
      written += (uint32_t)chunk_length;
      err = fr_nina_http_read_line(client, started, line, sizeof(line));
      if (err != FR_OK || line[0] != '\0') {
        return err == FR_OK ? FR_ERR_NET_PROTOCOL : err;
      }
    }
  } else if (content_length_known) {
    if (content_length > capacity) {
      return FR_ERR_NET_TOO_LARGE;
    }
    {
      fr_err_t err =
          fr_nina_http_read_exact(client, started, out_body, content_length);
      if (err != FR_OK) {
        return err;
      }
    }
    written = content_length;
  } else {
    for (;;) {
      uint8_t byte = 0;
      bool eof = false;
      fr_err_t err = fr_nina_http_read_byte(client, started, &byte, &eof);

      if (err != FR_OK) {
        return err;
      }
      if (eof) {
        break;
      }
      if (written == capacity) {
        return FR_ERR_NET_TOO_LARGE;
      }
      out_body[written++] = byte;
    }
  }

  *out_length = (uint16_t)written;
  return FR_OK;
}

fr_err_t fr_platform_http_get(const char *url, uint8_t *out_body,
                              uint16_t capacity, uint16_t *out_length) {
  fr_nina_url_t parsed = {};
  IPAddress address;
  WiFiClient client;
  fr_err_t result = FR_OK;

  if (url == NULL || url[0] == '\0' || out_body == NULL || out_length == NULL) {
    return FR_ERR_INVALID;
  }
  *out_length = 0;
  if (!fr_nina_wifi_ready()) {
    return FR_ERR_NET_DISCONNECTED;
  }
  result = fr_nina_url_parse(url, &parsed);
  if (result == FR_OK && !WiFi.hostByName(parsed.host, address)) {
    result = FR_ERR_NET_DNS;
  }
  if (result == FR_OK) {
    result =
        fr_nina_http_exchange(&client, &parsed, out_body, capacity, out_length);
  }
  client.stop();
  if (result != FR_OK) {
    *out_length = 0;
  }
  return result;
}

static fr_err_t fr_nina_tcp_entry(uint16_t platform_index,
                                  fr_nina_tcp_t **out_entry) {
  if (out_entry == NULL) {
    return FR_ERR_INVALID;
  }
  if (platform_index >= FR_TCP_HANDLE_COUNT ||
      !fr_nina_tcps[platform_index].in_use) {
    return FR_ERR_HANDLE;
  }
  *out_entry = &fr_nina_tcps[platform_index];
  return FR_OK;
}

static fr_err_t fr_nina_tcp_check_alive(fr_runtime_t *runtime,
                                        uint16_t platform_index) {
  if (runtime == NULL || platform_index >= FR_TCP_HANDLE_COUNT) {
    return FR_ERR_INVALID;
  }
  if (!fr_nina_wifi_ready()) {
    runtime->tcp_handles[platform_index].failed = true;
  }
  return runtime->tcp_handles[platform_index].failed ? FR_ERR_NET_DISCONNECTED
                                                     : FR_OK;
}

fr_err_t fr_platform_tcp_open(fr_runtime_t *runtime, const char *host,
                              uint16_t port, uint16_t *out_platform_index) {
  IPAddress address;
  uint16_t slot = FR_TCP_HANDLE_COUNT;
  uint32_t started = 0;
  fr_err_t err = FR_OK;

  if (runtime == NULL || host == NULL || host[0] == '\0' || port == 0 ||
      out_platform_index == NULL) {
    return FR_ERR_INVALID;
  }
  if (strlen(host) >= FR_NINA_HOST_BYTES) {
    return FR_ERR_NET_DNS;
  }
  if (!fr_nina_wifi_ready()) {
    return FR_ERR_NET_DISCONNECTED;
  }
  if (!WiFi.hostByName(host, address)) {
    return FR_ERR_NET_DNS;
  }
  for (uint16_t i = 0; i < FR_TCP_HANDLE_COUNT; i++) {
    if (!fr_nina_tcps[i].in_use) {
      slot = i;
      break;
    }
  }
  if (slot == FR_TCP_HANDLE_COUNT) {
    return FR_ERR_CAPACITY;
  }

  started = (uint32_t)millis();
  fr_nina_tcps[slot].client.setConnectionTimeout(FR_NINA_TCP_CONNECT_SLICE_MS);
  while (!fr_nina_tcps[slot].client.connect(address, port)) {
    fr_nina_tcps[slot].client.stop();
    if (!fr_nina_wifi_ready()) {
      return FR_ERR_NET_DISCONNECTED;
    }
    err = fr_nina_poll_interrupt(runtime);
    if (err != FR_OK) {
      return err;
    }
    if ((uint32_t)(millis() - started) >= FR_NINA_TCP_OPEN_TIMEOUT_MS) {
      return FR_ERR_NET_TIMEOUT;
    }
    delay(10);
  }

  fr_nina_tcps[slot].in_use = true;
  runtime->tcp_handles[slot].failed = false;
  *out_platform_index = slot;
  return FR_OK;
}

fr_err_t fr_platform_tcp_read(fr_runtime_t *runtime, uint16_t platform_index,
                              uint8_t *out_bytes, uint16_t capacity,
                              uint16_t *out_length) {
  fr_nina_tcp_t *entry = NULL;
  uint32_t started = 0;

  if (out_bytes == NULL || out_length == NULL || capacity == 0) {
    return FR_ERR_INVALID;
  }
  *out_length = 0;
  {
    fr_err_t err = fr_nina_tcp_check_alive(runtime, platform_index);
    if (err != FR_OK) {
      return err;
    }
    err = fr_nina_tcp_entry(platform_index, &entry);
    if (err != FR_OK) {
      return err;
    }
  }

  started = (uint32_t)millis();
  for (;;) {
    int available = entry->client.available();

    if (available > 0) {
      size_t wanted = (size_t)(available < capacity ? available : capacity);
      int read = entry->client.read(out_bytes, wanted);

      if (read <= 0) {
        return FR_ERR_NET_REFUSED;
      }
      *out_length = (uint16_t)read;
      return FR_OK;
    }
    if (!fr_nina_wifi_ready()) {
      runtime->tcp_handles[platform_index].failed = true;
      return FR_ERR_NET_DISCONNECTED;
    }
    if (!entry->client.connected()) {
      return FR_OK;
    }
    {
      fr_err_t err = fr_nina_poll_interrupt(runtime);
      if (err != FR_OK) {
        return err;
      }
    }
    if ((uint32_t)(millis() - started) >= FR_NINA_TCP_RW_TIMEOUT_MS) {
      return FR_ERR_NET_TIMEOUT;
    }
    delay(1);
  }
}

fr_err_t fr_platform_tcp_write(fr_runtime_t *runtime, uint16_t platform_index,
                               const uint8_t *bytes, uint16_t length) {
  fr_nina_tcp_t *entry = NULL;
  uint16_t written = 0;
  uint32_t started = 0;

  if (bytes == NULL && length > 0) {
    return FR_ERR_INVALID;
  }
  {
    fr_err_t err = fr_nina_tcp_check_alive(runtime, platform_index);
    if (err != FR_OK) {
      return err;
    }
    err = fr_nina_tcp_entry(platform_index, &entry);
    if (err != FR_OK) {
      return err;
    }
  }
  if (length == 0) {
    return FR_OK;
  }

  started = (uint32_t)millis();
  while (written < length) {
    uint16_t remaining = (uint16_t)(length - written);
    uint16_t chunk = remaining > 256 ? 256 : remaining;
    size_t sent = entry->client.write(bytes + written, chunk);

    if (sent > 0) {
      written = (uint16_t)(written + (uint16_t)sent);
      continue;
    }
    if (!fr_nina_wifi_ready()) {
      runtime->tcp_handles[platform_index].failed = true;
      return FR_ERR_NET_DISCONNECTED;
    }
    if (!entry->client.connected()) {
      return FR_ERR_NET_REFUSED;
    }
    {
      fr_err_t err = fr_nina_poll_interrupt(runtime);
      if (err != FR_OK) {
        return err;
      }
    }
    if ((uint32_t)(millis() - started) >= FR_NINA_TCP_RW_TIMEOUT_MS) {
      return FR_ERR_NET_TIMEOUT;
    }
    delay(1);
  }
  return FR_OK;
}

fr_err_t fr_platform_tcp_close(uint16_t platform_index) {
  fr_nina_tcp_t *entry = NULL;
  fr_err_t err = fr_nina_tcp_entry(platform_index, &entry);

  if (err != FR_OK) {
    return err;
  }
  entry->client.stop();
  entry->in_use = false;
  return FR_OK;
}

fr_err_t fr_platform_tcp_bytes_ready(fr_runtime_t *runtime,
                                     uint16_t platform_index,
                                     uint16_t *out_count) {
  fr_nina_tcp_t *entry = NULL;
  int available = 0;

  if (out_count == NULL) {
    return FR_ERR_INVALID;
  }
  *out_count = 0;
  {
    fr_err_t err = fr_nina_tcp_check_alive(runtime, platform_index);
    if (err != FR_OK) {
      return err;
    }
    err = fr_nina_tcp_entry(platform_index, &entry);
    if (err != FR_OK) {
      return err;
    }
  }
  available = entry->client.available();
  if (available < 0) {
    available = 0;
  }
  if (available > UINT16_MAX) {
    available = UINT16_MAX;
  }
  *out_count = (uint16_t)available;
  return FR_OK;
}
#endif

#if FR_FEATURE_BLE
const char *fr_platform_ble_backend_name(void) { return "nina-arduinoble"; }

fr_err_t fr_platform_ble_on(fr_runtime_t *runtime) {
  uint8_t raw_address[6] = {};
  uint32_t firmware_version = 0;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  if (fr_nina_ble.radio_state == FR_BLE_RADIO_READY) {
    fr_nina_ble_record(FR_BLE_OP_ON, FR_OK, 0);
    return FR_OK;
  }
  if (fr_nina_ble.radio_state == FR_BLE_RADIO_STARTING ||
      fr_nina_ble.radio_state == FR_BLE_RADIO_STOPPING) {
    fr_nina_ble_record(FR_BLE_OP_ON, FR_ERR_BLE_BUSY, 0);
    return FR_ERR_BLE_BUSY;
  }

  fr_nina_ble.lifecycle_generation += 1u;
  fr_nina_ble.radio_state = FR_BLE_RADIO_STARTING;
  fr_nina_ble.own_address_valid = false;
  firmware_version = WiFi.firmwareVersionU32();
  if (firmware_version < FR_NINA_FIRMWARE_BLE_MIN) {
    fr_nina_ble.radio_state = FR_BLE_RADIO_FAILED;
    fr_nina_ble_record(FR_BLE_OP_ON, FR_ERR_UNSUPPORTED,
                       (int32_t)firmware_version);
    return FR_ERR_UNSUPPORTED;
  }
  if (!BLE.begin()) {
    fr_nina_ble.radio_state = FR_BLE_RADIO_FAILED;
    fr_nina_ble_record(FR_BLE_OP_ON, FR_ERR_IO, 0);
    return FR_ERR_IO;
  }
  if (HCI.readBdAddr(raw_address) != 0) {
    BLE.end();
    fr_nina_ble.radio_state = FR_BLE_RADIO_FAILED;
    fr_nina_ble_record(FR_BLE_OP_ON, FR_ERR_IO, 0);
    return FR_ERR_IO;
  }
  for (uint8_t i = 0; i < sizeof(raw_address); i++) {
    fr_nina_ble.own_address[i] = raw_address[sizeof(raw_address) - 1u - i];
  }
  fr_nina_ble.own_address_type = FR_BLE_ADDRESS_PUBLIC;
  fr_nina_ble.own_address_valid = true;
  fr_nina_ble.radio_state = FR_BLE_RADIO_READY;
  fr_nina_ble_last_poll_ms = (uint32_t)millis();
  fr_nina_ble_record(FR_BLE_OP_ON, FR_OK, 0);
  return FR_OK;
}

fr_err_t fr_platform_ble_off(fr_runtime_t *runtime) {
  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }

#if FR_BLE_ENABLE_OBSERVER
  if (fr_nina_ble.scan_state != FR_BLE_SCAN_IDLE) {
    (void)fr_nina_gap.stopRawScan();
    fr_nina_ble.scan_generation += 1u;
  }
  fr_nina_ble_clear_scan();
#endif
#if FR_BLE_ENABLE_BROADCASTER
  if (fr_nina_ble.advertise_state != FR_BLE_ADVERTISE_IDLE) {
    fr_nina_gap.stopAdvertise();
  }
  fr_nina_ble.advertise_state = FR_BLE_ADVERTISE_IDLE;
  fr_nina_ble.advertise_requested_interval_ms = 0;
  fr_nina_ble.advertise_actual_interval_us = 0;
  fr_nina_ble.advertise_connectable = false;
  fr_nina_ble.advertising_data_length = 0;
  fr_nina_ble.scan_response_data_length = 0;
  memset(fr_nina_ble.advertising_data, 0, sizeof(fr_nina_ble.advertising_data));
  memset(fr_nina_ble.scan_response_data, 0,
         sizeof(fr_nina_ble.scan_response_data));
#endif
  if (fr_nina_ble.radio_state == FR_BLE_RADIO_READY) {
    fr_nina_ble.radio_state = FR_BLE_RADIO_STOPPING;
    BLE.end();
  }
  fr_nina_ble.radio_state = FR_BLE_RADIO_OFF;
  fr_nina_ble.own_address_valid = false;
  memset(fr_nina_ble.own_address, 0, sizeof(fr_nina_ble.own_address));
  fr_nina_ble_record(FR_BLE_OP_OFF, FR_OK, 0);
  return FR_OK;
}

fr_err_t fr_platform_ble_project_clear(void) {
#if FR_BLE_ENABLE_OBSERVER
  if (fr_nina_ble.scan_state != FR_BLE_SCAN_IDLE) {
    (void)fr_nina_gap.stopRawScan();
  }
#endif
#if FR_BLE_ENABLE_BROADCASTER
  if (fr_nina_ble.advertise_state != FR_BLE_ADVERTISE_IDLE) {
    fr_nina_gap.stopAdvertise();
  }
#endif
  if (fr_nina_ble.radio_state == FR_BLE_RADIO_READY) {
    BLE.end();
  }
  memset(&fr_nina_ble, 0, sizeof(fr_nina_ble));
  fr_nina_ble.radio_state = FR_BLE_RADIO_OFF;
#if FR_BLE_ENABLE_OBSERVER
  fr_nina_ble.scan_state = FR_BLE_SCAN_IDLE;
#endif
#if FR_BLE_ENABLE_BROADCASTER
  fr_nina_ble.advertise_state = FR_BLE_ADVERTISE_IDLE;
#endif
  fr_nina_ble.last_result = FR_OK;
  fr_nina_ble.last_operation_ms = (uint32_t)millis();
  return FR_OK;
}

fr_err_t fr_platform_ble_status(fr_ble_status_t *out_status) {
  if (out_status == NULL) {
    return FR_ERR_INVALID;
  }
  memset(out_status, 0, sizeof(*out_status));
  out_status->radio_state = fr_nina_ble.radio_state;
#if FR_BLE_ENABLE_OBSERVER
  out_status->scan_state = fr_nina_ble.scan_state;
#else
  out_status->scan_state = FR_BLE_SCAN_IDLE;
#endif
#if FR_BLE_ENABLE_BROADCASTER
  out_status->advertise_state = fr_nina_ble.advertise_state;
#else
  out_status->advertise_state = FR_BLE_ADVERTISE_IDLE;
#endif
  out_status->roles =
#if FR_BLE_ENABLE_OBSERVER
      FR_BLE_ROLE_OBSERVER |
#endif
#if FR_BLE_ENABLE_BROADCASTER
      FR_BLE_ROLE_BROADCASTER |
#endif
      0;
  out_status->coexistence_enabled = true;
  out_status->lifecycle_generation = fr_nina_ble.lifecycle_generation;
  out_status->own_address_type = fr_nina_ble.own_address_type;
  memcpy(out_status->own_address, fr_nina_ble.own_address,
         sizeof(out_status->own_address));
  out_status->own_address_valid = fr_nina_ble.own_address_valid;
  out_status->late_callback_count = fr_nina_ble.late_callback_count;
#if FR_BLE_ENABLE_OBSERVER
  out_status->scan_generation = fr_nina_ble.scan_generation;
  out_status->requested_interval_ms = fr_nina_ble.requested_interval_ms;
  out_status->requested_window_ms = fr_nina_ble.requested_window_ms;
  out_status->actual_interval_us = fr_nina_ble.actual_interval_us;
  out_status->actual_window_us = fr_nina_ble.actual_window_us;
  out_status->minimum_rssi = fr_nina_ble.minimum_rssi;
  out_status->active_scan = fr_nina_ble.active_scan;
  out_status->repeats = fr_nina_ble.repeats;
  out_status->queue_count = fr_nina_ble.queue_count;
  out_status->queue_capacity = FR_BLE_SCAN_QUEUE_COUNT;
  out_status->queue_high_water = fr_nina_ble.queue_high_water;
  out_status->received = fr_nina_ble.received;
  out_status->accepted = fr_nina_ble.accepted;
  out_status->filtered_rssi = fr_nina_ble.filtered_rssi;
  out_status->dequeued = fr_nina_ble.dequeued;
  out_status->dropped = fr_nina_ble.dropped;
  out_status->malformed = fr_nina_ble.malformed;
  out_status->current_valid = fr_nina_ble.current_valid;
  out_status->current_rssi = fr_nina_ble.current.rssi;
  out_status->current_flags = fr_nina_ble.current.flags;
  out_status->current_data_length = fr_nina_ble.current.data_length;
#endif
#if FR_BLE_ENABLE_BROADCASTER
  out_status->advertise_requested_interval_ms =
      fr_nina_ble.advertise_requested_interval_ms;
  out_status->advertise_actual_interval_us =
      fr_nina_ble.advertise_actual_interval_us;
  out_status->advertise_connectable = fr_nina_ble.advertise_connectable;
  out_status->advertising_data_length = fr_nina_ble.advertising_data_length;
  out_status->scan_response_data_length = fr_nina_ble.scan_response_data_length;
  out_status->advertise_starts = fr_nina_ble.advertise_starts;
  out_status->advertise_stops = fr_nina_ble.advertise_stops;
#endif
  out_status->last_operation = fr_nina_ble.last_operation;
  out_status->last_result = fr_nina_ble.last_result;
  out_status->last_platform_code = fr_nina_ble.last_platform_code;
  out_status->last_protocol_reason = fr_nina_ble.last_protocol_reason;
  out_status->last_operation_ms = fr_nina_ble.last_operation_ms;
  out_status->reset_count = fr_nina_ble.reset_count;
  return FR_OK;
}

#if FR_BLE_ENABLE_OBSERVER
fr_err_t fr_platform_ble_scan_start(uint16_t interval_ms, uint16_t window_ms,
                                    bool active, bool repeats,
                                    int8_t minimum_rssi) {
  uint16_t interval_units = 0;
  uint16_t window_units = 0;
  int result = 0;

  if (interval_ms < FR_NINA_BLE_SCAN_INTERVAL_MIN_MS ||
      interval_ms > FR_NINA_BLE_SCAN_INTERVAL_MAX_MS ||
      window_ms < FR_NINA_BLE_SCAN_INTERVAL_MIN_MS || window_ms > interval_ms ||
      minimum_rssi < FR_NINA_BLE_RSSI_MIN ||
      minimum_rssi > FR_NINA_BLE_RSSI_MAX) {
    return FR_ERR_RANGE;
  }
  if (fr_nina_ble.radio_state != FR_BLE_RADIO_READY) {
    fr_nina_ble_record(FR_BLE_OP_SCAN_START, FR_ERR_BLE_NOT_READY, 0);
    return FR_ERR_BLE_NOT_READY;
  }
  if (fr_nina_ble.scan_state != FR_BLE_SCAN_IDLE
#if FR_BLE_ENABLE_BROADCASTER
      || fr_nina_ble.advertise_state != FR_BLE_ADVERTISE_IDLE
#endif
  ) {
    fr_nina_ble_record(FR_BLE_OP_SCAN_START, FR_ERR_BLE_BUSY, 0);
    return FR_ERR_BLE_BUSY;
  }

  interval_units = fr_nina_gap_units(interval_ms);
  window_units = fr_nina_gap_units(window_ms);
  if (window_units > interval_units) {
    return FR_ERR_RANGE;
  }
  fr_nina_ble_clear_reports();
  fr_nina_ble.requested_interval_ms = interval_ms;
  fr_nina_ble.requested_window_ms = window_ms;
  fr_nina_ble.actual_interval_us = (uint32_t)interval_units * 625u;
  fr_nina_ble.actual_window_us = (uint32_t)window_units * 625u;
  fr_nina_ble.minimum_rssi = minimum_rssi;
  fr_nina_ble.active_scan = active;
  fr_nina_ble.repeats = repeats;
  fr_nina_ble.scan_generation += 1u;
  fr_nina_ble.scan_state = FR_BLE_SCAN_ACTIVE;
  result =
      fr_nina_gap.startRawScan(active, interval_units, window_units, repeats);
  if (result != 0) {
    fr_nina_ble.scan_state = FR_BLE_SCAN_IDLE;
    fr_nina_ble_record(FR_BLE_OP_SCAN_START, FR_ERR_IO, result);
    return FR_ERR_IO;
  }
  fr_nina_ble_record(FR_BLE_OP_SCAN_START, FR_OK, 0);
  return FR_OK;
}

fr_err_t fr_platform_ble_scan_stop(fr_runtime_t *runtime) {
  int result = 0;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
  fr_nina_ble.current_valid = false;
  memset(&fr_nina_ble.current, 0, sizeof(fr_nina_ble.current));
  if (fr_nina_ble.scan_state == FR_BLE_SCAN_IDLE) {
    fr_nina_ble_record(FR_BLE_OP_SCAN_STOP, FR_OK, 0);
    return FR_OK;
  }
  fr_nina_ble.scan_state = FR_BLE_SCAN_STOPPING;
  result = fr_nina_gap.stopRawScan();
  if (result != 0) {
    fr_nina_ble_record(FR_BLE_OP_SCAN_STOP, FR_ERR_IO, result);
    return FR_ERR_IO;
  }
  fr_nina_ble.scan_state = FR_BLE_SCAN_IDLE;
  fr_nina_ble_record(FR_BLE_OP_SCAN_STOP, FR_OK, 0);
  return FR_OK;
}

fr_err_t fr_platform_ble_scan_next(bool *out_has_report) {
  if (out_has_report == NULL) {
    return FR_ERR_INVALID;
  }
  fr_nina_poll();
  if (fr_nina_ble.queue_count == 0) {
    fr_nina_ble.current_valid = false;
    memset(&fr_nina_ble.current, 0, sizeof(fr_nina_ble.current));
    *out_has_report = false;
    return FR_OK;
  }

  fr_nina_ble.current = fr_nina_ble.queue[fr_nina_ble.queue_head];
  memset(&fr_nina_ble.queue[fr_nina_ble.queue_head], 0,
         sizeof(fr_nina_ble.queue[fr_nina_ble.queue_head]));
  fr_nina_ble.current_valid = true;
  fr_nina_ble.queue_head =
      (uint8_t)((fr_nina_ble.queue_head + 1u) % FR_BLE_SCAN_QUEUE_COUNT);
  fr_nina_ble.queue_count -= 1u;
  fr_nina_ble.dequeued += 1u;
  *out_has_report = true;
  return FR_OK;
}

fr_err_t fr_platform_ble_scan_current(fr_ble_scan_report_t *out_report) {
  if (out_report == NULL) {
    return FR_ERR_INVALID;
  }
  if (!fr_nina_ble.current_valid) {
    return FR_ERR_NOT_FOUND;
  }
  *out_report = fr_nina_ble.current;
  return FR_OK;
}
#endif

#if FR_BLE_ENABLE_BROADCASTER
fr_err_t fr_platform_ble_advertise_start(const uint8_t *advertising_data,
                                         uint8_t advertising_data_length,
                                         const uint8_t *scan_response_data,
                                         uint8_t scan_response_data_length,
                                         uint16_t interval_ms,
                                         bool connectable) {
  uint16_t interval_units = 0;
  int result = 0;

  if ((advertising_data_length > 0 && advertising_data == NULL) ||
      (scan_response_data_length > 0 && scan_response_data == NULL)) {
    return FR_ERR_INVALID;
  }
  if (advertising_data_length > FR_BLE_ADVERTISEMENT_DATA_BYTES ||
      scan_response_data_length > FR_BLE_ADVERTISEMENT_DATA_BYTES) {
    return FR_ERR_CAPACITY;
  }
  if (interval_ms < FR_NINA_BLE_ADVERTISE_INTERVAL_MIN_MS ||
      interval_ms > FR_NINA_BLE_ADVERTISE_INTERVAL_MAX_MS) {
    return FR_ERR_RANGE;
  }
  if (fr_nina_ble.radio_state != FR_BLE_RADIO_READY) {
    fr_nina_ble_record(FR_BLE_OP_ADVERTISE_START, FR_ERR_BLE_NOT_READY, 0);
    return FR_ERR_BLE_NOT_READY;
  }
#if FR_BLE_ENABLE_OBSERVER
  if (fr_nina_ble.scan_state != FR_BLE_SCAN_IDLE) {
    fr_nina_ble_record(FR_BLE_OP_ADVERTISE_START, FR_ERR_BLE_BUSY, 0);
    return FR_ERR_BLE_BUSY;
  }
#endif
  if (fr_nina_ble.advertise_state != FR_BLE_ADVERTISE_IDLE) {
    fr_nina_ble_record(FR_BLE_OP_ADVERTISE_START, FR_ERR_BLE_BUSY, 0);
    return FR_ERR_BLE_BUSY;
  }

  memset(fr_nina_ble.advertising_data, 0, sizeof(fr_nina_ble.advertising_data));
  memset(fr_nina_ble.scan_response_data, 0,
         sizeof(fr_nina_ble.scan_response_data));
  if (advertising_data_length > 0) {
    memcpy(fr_nina_ble.advertising_data, advertising_data,
           advertising_data_length);
  }
  if (scan_response_data_length > 0) {
    memcpy(fr_nina_ble.scan_response_data, scan_response_data,
           scan_response_data_length);
  }
  interval_units = fr_nina_gap_units(interval_ms);
  fr_nina_gap.setAdvertisingInterval(interval_units);
  fr_nina_gap.setConnectable(connectable);
  fr_nina_ble.advertise_state = FR_BLE_ADVERTISE_ACTIVE;
  result = fr_nina_gap.advertise(
      fr_nina_ble.advertising_data, advertising_data_length,
      fr_nina_ble.scan_response_data, scan_response_data_length);
  if (!result) {
    fr_nina_ble.advertise_state = FR_BLE_ADVERTISE_IDLE;
    fr_nina_ble_record(FR_BLE_OP_ADVERTISE_START, FR_ERR_IO, result);
    return FR_ERR_IO;
  }

  fr_nina_ble.advertise_requested_interval_ms = interval_ms;
  fr_nina_ble.advertise_actual_interval_us = (uint32_t)interval_units * 625u;
  fr_nina_ble.advertise_connectable = connectable;
  fr_nina_ble.advertising_data_length = advertising_data_length;
  fr_nina_ble.scan_response_data_length = scan_response_data_length;
  fr_nina_ble.advertise_starts += 1u;
  fr_nina_ble_record(FR_BLE_OP_ADVERTISE_START, FR_OK, 0);
  return FR_OK;
}

fr_err_t fr_platform_ble_advertise_stop(void) {
  if (fr_nina_ble.advertise_state == FR_BLE_ADVERTISE_IDLE) {
    fr_nina_ble_record(FR_BLE_OP_ADVERTISE_STOP, FR_OK, 0);
    return FR_OK;
  }
  fr_nina_ble.advertise_state = FR_BLE_ADVERTISE_STOPPING;
  fr_nina_gap.stopAdvertise();
  fr_nina_ble.advertise_state = FR_BLE_ADVERTISE_IDLE;
  fr_nina_ble.advertise_stops += 1u;
  fr_nina_ble_record(FR_BLE_OP_ADVERTISE_STOP, FR_OK, 0);
  return FR_OK;
}
#endif
#endif

} /* extern "C" */
