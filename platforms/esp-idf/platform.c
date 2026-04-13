/* TODO: ESP-IDF platform implementation */
#include "platform.h"
#include "ffi.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "froth_types.h"
#include "froth_vm.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define FROTH_CONSOLE_UART_RX_BUFFER 256
#define FROTH_CONSOLE_TX_WAIT_MS 100

typedef struct {
  froth_cell_t port;
  froth_cell_t tx;
  froth_cell_t rx;
  froth_cell_t baud;
} esp32_console_route_t;

static const esp32_console_route_t default_console_route = {
    .port = FROTH_BOARD_CONSOLE_DEFAULT_PORT,
    .tx = FROTH_BOARD_CONSOLE_DEFAULT_TX_PIN,
    .rx = FROTH_BOARD_CONSOLE_DEFAULT_RX_PIN,
    .baud = FROTH_BOARD_CONSOLE_DEFAULT_BAUD,
};

static esp32_console_route_t active_console_route = {
    .port = FROTH_BOARD_CONSOLE_DEFAULT_PORT,
    .tx = FROTH_BOARD_CONSOLE_DEFAULT_TX_PIN,
    .rx = FROTH_BOARD_CONSOLE_DEFAULT_RX_PIN,
    .baud = FROTH_BOARD_CONSOLE_DEFAULT_BAUD,
};

static uint8_t console_pending_byte;
static bool console_pending_valid;
static platform_emit_hook_t emit_hook = NULL;
static void *emit_hook_context = NULL;

static bool console_route_valid(const esp32_console_route_t *route) {
  if (route->port < UART_NUM_0 || route->port > UART_NUM_2) {
    return false;
  }
  if (route->tx < 0 || route->rx < 0) {
    return false;
  }
  if (route->tx == route->rx) {
    return false;
  }
  if (route->tx >= GPIO_NUM_MAX || route->rx >= GPIO_NUM_MAX) {
    return false;
  }
  if (route->baud <= 0) {
    return false;
  }
  return true;
}

static bool console_route_equal(const esp32_console_route_t *a,
                                const esp32_console_route_t *b) {
  return a->port == b->port && a->tx == b->tx && a->rx == b->rx &&
         a->baud == b->baud;
}

static uart_port_t console_uart_port(froth_cell_t port) {
  return (uart_port_t)port;
}

static esp_err_t console_build_config(const esp32_console_route_t *route,
                                      uart_config_t *config) {
  if (!console_route_valid(route)) {
    return ESP_ERR_INVALID_ARG;
  }

  *config = (uart_config_t){
      .baud_rate = (int)route->baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  return ESP_OK;
}

static esp_err_t console_configure_route(const esp32_console_route_t *route) {
  uart_config_t config;
  esp_err_t err = console_build_config(route, &config);
  if (err != ESP_OK) {
    return err;
  }

  err = uart_param_config(console_uart_port(route->port), &config);
  if (err != ESP_OK) {
    return err;
  }

  err = uart_set_pin(console_uart_port(route->port), route->tx, route->rx,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    return err;
  }

  return uart_flush_input(console_uart_port(route->port));
}

static esp_err_t console_install_route(const esp32_console_route_t *route) {
  esp_err_t err = uart_driver_install(console_uart_port(route->port),
                                      FROTH_CONSOLE_UART_RX_BUFFER, 0, 0, NULL,
                                      0);
  if (err != ESP_OK) {
    return err;
  }

  err = console_configure_route(route);
  if (err != ESP_OK) {
    uart_driver_delete(console_uart_port(route->port));
    return err;
  }

  return ESP_OK;
}

static void console_restore_route_best_effort(const esp32_console_route_t *route) {
  (void)console_configure_route(route);
  console_pending_valid = false;
}

static esp_err_t console_wait_tx_done(const esp32_console_route_t *route) {
  return uart_wait_tx_done(console_uart_port(route->port),
                           pdMS_TO_TICKS(FROTH_CONSOLE_TX_WAIT_MS));
}

static int console_read_byte_nowait(uint8_t *byte) {
  if (console_pending_valid) {
    *byte = console_pending_byte;
    console_pending_valid = false;
    return 1;
  }

  return uart_read_bytes(console_uart_port(active_console_route.port), byte, 1,
                         0);
}

static bool console_key_ready_internal(void) {
  size_t len = 0;

  if (console_pending_valid) {
    return true;
  }

  if (uart_get_buffered_data_len(console_uart_port(active_console_route.port),
                                 &len) != ESP_OK) {
    return false;
  }

  return len > 0;
}

static froth_error_t console_write_raw(uint8_t byte) {
  int written = uart_write_bytes(console_uart_port(active_console_route.port),
                                 &byte, 1);
  if (written != 1) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

static bool boot_button_pressed(void) {
#ifdef FROTH_BOARD_BOOT_BUTTON_PIN
  return gpio_get_level(FROTH_BOARD_BOOT_BUTTON_PIN) == 0;
#else
  return false;
#endif
}

void platform_delay_ms(froth_cell_u_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms)); // sleep
}

uint32_t platform_uptime_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

void platform_reset_runtime_state(void) {
  froth_board_reset_runtime_state();
}

froth_error_t platform_init(void) {
  esp_err_t err = console_install_route(&default_console_route);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  active_console_route = default_console_route;
  console_pending_valid = false;
  vTaskDelay(pdMS_TO_TICKS(50));
  uart_flush_input(console_uart_port(active_console_route.port));

#ifdef FROTH_BOARD_BOOT_BUTTON_PIN
  err = gpio_config(&(gpio_config_t){
      .pin_bit_mask = 1ULL << FROTH_BOARD_BOOT_BUTTON_PIN,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  });
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
#endif

  // Set up the NVS partition system
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  return FROTH_OK;
}

froth_error_t platform_emit(uint8_t byte) {
  if (emit_hook != NULL) {
    return emit_hook(emit_hook_context, byte);
  }
  /* 0x00 is the COBS frame delimiter on the wire. Console output
     containing NUL would corrupt the host's frame parser. Skip it. */
  if (byte == 0x00)
    return FROTH_OK;
  /* Terminal expects \r\n. VFS conversion is off (binary safety for COBS),
     so we prepend \r before \n here for REPL/console output. */
  if (byte == '\n')
    FROTH_TRY(console_write_raw('\r'));
  return console_write_raw(byte);
}

froth_error_t platform_emit_raw(uint8_t byte) {
  return console_write_raw(byte);
}

void platform_set_emit_hook(platform_emit_hook_t hook, void *context) {
  emit_hook = hook;
  emit_hook_context = context;
}

void platform_clear_emit_hook(void) {
  emit_hook = NULL;
  emit_hook_context = NULL;
}

froth_error_t platform_key(uint8_t *byte) {
  if (console_pending_valid) {
    *byte = console_pending_byte;
    console_pending_valid = false;
    return FROTH_OK;
  }

  int read = uart_read_bytes(console_uart_port(active_console_route.port), byte,
                             1, portMAX_DELAY);
  if (read != 1) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

bool platform_input_closed(void) { return false; }

bool platform_should_echo_input(void) { return true; }

bool platform_key_ready(void) {
  return console_key_ready_internal();
}

void platform_check_interrupt(struct froth_vm_t *vm) {
  if (boot_button_pressed()) {
    vm->interrupted = 1;
    return;
  }

  if (!console_key_ready_internal()) {
    return;
  }

  uint8_t byte = 0;
  int read = console_read_byte_nowait(&byte);
  if (read <= 0) {
    return;
  }

  if (byte == 0x03) {
    vm->interrupted = 1;
  } else {
    console_pending_byte = byte;
    console_pending_valid = true;
  }
}

froth_error_t platform_console_uart_bind(froth_cell_t port, froth_cell_t tx,
                                         froth_cell_t rx, froth_cell_t baud) {
  esp32_console_route_t target = {.port = port, .tx = tx, .rx = rx, .baud = baud};
  esp32_console_route_t old = active_console_route;
  esp_err_t err;

  if (!console_route_valid(&target)) {
    return FROTH_ERROR_BOUNDS;
  }

  if (console_route_equal(&target, &old)) {
    return FROTH_OK;
  }

  if (target.port == old.port) {
    err = console_wait_tx_done(&old);
    if (err != ESP_OK) {
      return FROTH_ERROR_IO;
    }

    err = console_configure_route(&target);
    if (err != ESP_OK) {
      console_restore_route_best_effort(&old);
      return err == ESP_ERR_INVALID_ARG ? FROTH_ERROR_BOUNDS : FROTH_ERROR_IO;
    }

    active_console_route = target;
    console_pending_valid = false;
    return FROTH_OK;
  }

  err = console_install_route(&target);
  if (err != ESP_OK) {
    return err == ESP_ERR_INVALID_ARG ? FROTH_ERROR_BOUNDS : FROTH_ERROR_IO;
  }

  err = console_wait_tx_done(&old);
  if (err != ESP_OK) {
    uart_driver_delete(console_uart_port(target.port));
    return FROTH_ERROR_IO;
  }

  active_console_route = target;
  console_pending_valid = false;

  /* Cleanup of the old driver is best-effort. Keeping the new console active
     is more important than failing the switch after the handoff succeeded. */
  (void)uart_driver_delete(console_uart_port(old.port));

  return FROTH_OK;
}

froth_error_t platform_console_uart_default(void) {
  return platform_console_uart_bind(default_console_route.port,
                                    default_console_route.tx,
                                    default_console_route.rx,
                                    default_console_route.baud);
}

froth_error_t platform_console_uart_info(platform_console_uart_info_t *info) {
  if (info == NULL) {
    return FROTH_ERROR_BOUNDS;
  }

  info->port = active_console_route.port;
  info->tx = active_console_route.tx;
  info->rx = active_console_route.rx;
  info->baud = active_console_route.baud;
  return FROTH_OK;
}

_Noreturn void platform_fatal(void) {
  // esp_restart(); // Avoid this, it will result in an infinite loop.
  while (1) {
  };
}

/* Shared NVS staging buffer. Static to keep it off the task stack (2KB). */
static uint8_t nvs_staging[FROTH_SNAPSHOT_BLOCK_SIZE];

froth_error_t platform_snapshot_write(uint8_t slot, uint32_t offset,
                                      const uint8_t *buf, uint32_t len) {
  nvs_handle_t handle;
  const char *key = slot == 0 ? "snap_a" : "snap_b";

  if (offset + len > FROTH_SNAPSHOT_BLOCK_SIZE) {
    return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  }

  esp_err_t err = nvs_open("froth", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  // Read existing blob so we can merge, or start from zeroes
  size_t existing_len = FROTH_SNAPSHOT_BLOCK_SIZE;
  err = nvs_get_blob(handle, key, nvs_staging, &existing_len);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    memset(nvs_staging, 0, sizeof(nvs_staging));
    existing_len = 0;
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  // Patch in the new bytes at the requested offset
  memcpy(nvs_staging + offset, buf, len);

  // New blob size is the larger of existing data and the write extent
  size_t new_len = existing_len;
  if (offset + len > new_len) {
    new_len = offset + len;
  }

  err = nvs_set_blob(handle, key, nvs_staging, new_len);
  if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  nvs_close(handle);
  return FROTH_OK;
}

froth_error_t platform_snapshot_read(uint8_t slot, uint32_t offset,
                                     uint8_t *buf, uint32_t len) {
  nvs_handle_t handle;
  const char *key = slot == 0 ? "snap_a" : "snap_b";

  esp_err_t err = nvs_open("froth", NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  size_t stored_len = FROTH_SNAPSHOT_BLOCK_SIZE;
  err = nvs_get_blob(handle, key, nvs_staging, &stored_len);
  if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  if (offset + len > stored_len) {
    nvs_close(handle);
    return FROTH_ERROR_SNAPSHOT_FORMAT;
  }

  memcpy(buf, nvs_staging + offset, len);

  nvs_close(handle);
  return FROTH_OK;
}

froth_error_t platform_snapshot_erase(uint8_t slot) {
  nvs_handle_t handle;

  esp_err_t err = nvs_open("froth", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  const char *key = slot == 0 ? "snap_a" : "snap_b";
  err = nvs_erase_key(handle, key);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Nothing to erase, that's fine
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    nvs_close(handle);
    return FROTH_ERROR_IO;
  }

  nvs_close(handle);
  return FROTH_OK;
}
