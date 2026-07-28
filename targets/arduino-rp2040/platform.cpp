extern "C" {
#include "board.h"
#include "crc.h"
#include "froth.h"
#include "persist_format.h"
}

#include <Arduino.h>
#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/flash.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern "C" uint8_t _FS_start[];
extern "C" uint8_t _FS_end[];

#if FR_BOARD_HAS_NINA
extern "C" void fr_nina_poll(void);
#endif

enum {
  FR_RP2040_GPIO_COUNT = 30,
  FR_RP2040_ADC_FIRST_PIN = 26,
  FR_RP2040_ADC_LAST_PIN = 29,
  FR_RP2040_TYPEAHEAD_BYTES = 128,
  FR_RP2040_CTRL_C = 3,
  FR_RP2040_BACKSPACE = 8,
  FR_RP2040_DELETE = 127,
#if FR_FEATURE_PWM
  FR_RP2040_PWM_MAX = 8,
#endif
#if FR_FEATURE_I2C
  FR_RP2040_I2C_MAX = 2,
  FR_RP2040_I2C_MAX_HZ = 1000000,
  FR_RP2040_I2C_TIMEOUT_MS = 25,
#endif
#if FR_FEATURE_PERSISTENCE
  FR_RP2040_PERSIST_SLOT_COUNT = 2,
  FR_RP2040_PERSIST_SLOT_BYTES = 32768,
  FR_RP2040_FLASH_PAGE_BYTES = 256,
  FR_RP2040_FLASH_SECTOR_BYTES = 4096,
  FR_RP2040_PERSIST_REGION_BYTES =
      FR_RP2040_PERSIST_SLOT_COUNT * FR_RP2040_PERSIST_SLOT_BYTES,
#endif
};

#if FR_FEATURE_PERSISTENCE
static_assert((uint32_t)FR_PERSIST_STORAGE_BYTES <=
                  (uint32_t)FR_RP2040_PERSIST_SLOT_BYTES,
              "RP2040 persistence slot is too small");
static_assert((uint32_t)FR_PERSIST_HEADER_BYTES <=
                  (uint32_t)FR_RP2040_FLASH_PAGE_BYTES,
              "RP2040 persistence header must fit one flash page");
static_assert(FR_RP2040_PERSIST_SLOT_BYTES %
                      FR_RP2040_FLASH_SECTOR_BYTES ==
                  0,
              "RP2040 persistence slot must be sector aligned");
static_assert(FR_RP2040_PERSIST_REGION_BYTES %
                      FR_RP2040_FLASH_SECTOR_BYTES ==
                  0,
              "RP2040 persistence region must be sector aligned");
#endif

static bool fr_rp2040_initialized;
static uint8_t fr_rp2040_typeahead[FR_RP2040_TYPEAHEAD_BYTES];
static uint8_t fr_rp2040_typeahead_start;
static uint8_t fr_rp2040_typeahead_count;

#if FR_FEATURE_REPL
static fr_platform_idle_fn fr_rp2040_idle_handler;
static void *fr_rp2040_idle_context;
#endif

#if FR_FEATURE_PWM
typedef struct fr_rp2040_pwm_t {
  bool in_use;
  uint16_t pin;
  uint16_t freq;
  uint16_t wrap;
  uint8_t slice;
  uint8_t channel;
} fr_rp2040_pwm_t;

static fr_rp2040_pwm_t fr_rp2040_pwms[FR_RP2040_PWM_MAX];
#endif

#if FR_FEATURE_I2C
typedef struct fr_rp2040_i2c_t {
  bool in_use;
  uint16_t sda;
  uint16_t scl;
  uint32_t freq;
} fr_rp2040_i2c_t;

static fr_rp2040_i2c_t fr_rp2040_i2cs[FR_RP2040_I2C_MAX];
#endif

#if FR_FEATURE_PERSISTENCE
static const uint8_t *fr_rp2040_persist_mounted_bytes;
static const uint8_t *fr_rp2040_persist_candidate_bytes;
static uint16_t fr_rp2040_persist_mounted_length;
static uint16_t fr_rp2040_persist_candidate_length;
static bool fr_rp2040_persist_mounted;
static bool fr_rp2040_persist_candidate;

static struct {
  bool active;
  bool page_dirty;
  uint8_t slot;
  uint32_t cursor;
  uint32_t page_offset;
  uint32_t payload_length;
  uint32_t backend_generation;
  uint8_t page[FR_RP2040_FLASH_PAGE_BYTES];
} fr_rp2040_persist_stream;
#endif

static bool fr_rp2040_gpio_valid(uint16_t pin) {
  return pin < FR_RP2040_GPIO_COUNT;
}

#if FR_FEATURE_PWM
static fr_rp2040_pwm_t *fr_rp2040_pwm_entry(uint16_t index) {
  if (index >= FR_RP2040_PWM_MAX || !fr_rp2040_pwms[index].in_use) {
    return NULL;
  }
  return &fr_rp2040_pwms[index];
}

static bool fr_rp2040_pwm_pin_in_use(uint16_t pin) {
  for (uint16_t i = 0; i < FR_RP2040_PWM_MAX; i++) {
    if (fr_rp2040_pwms[i].in_use && fr_rp2040_pwms[i].pin == pin) {
      return true;
    }
  }
  return false;
}
#endif

#if FR_FEATURE_I2C
static bool fr_rp2040_i2c_pin_in_use(uint16_t pin) {
  for (uint16_t i = 0; i < FR_RP2040_I2C_MAX; i++) {
    if (fr_rp2040_i2cs[i].in_use &&
        (fr_rp2040_i2cs[i].sda == pin || fr_rp2040_i2cs[i].scl == pin)) {
      return true;
    }
  }
  return false;
}
#endif

static void fr_rp2040_typeahead_clear(void) {
  fr_rp2040_typeahead_start = 0;
  fr_rp2040_typeahead_count = 0;
}

static void fr_rp2040_typeahead_push(uint8_t byte) {
  uint8_t slot = 0;

  if (fr_rp2040_typeahead_count >= FR_RP2040_TYPEAHEAD_BYTES) {
    return;
  }
  slot = (uint8_t)((fr_rp2040_typeahead_start +
                    fr_rp2040_typeahead_count) %
                   FR_RP2040_TYPEAHEAD_BYTES);
  fr_rp2040_typeahead[slot] = byte;
  fr_rp2040_typeahead_count++;
}

static fr_err_t fr_rp2040_console_driver_read(uint8_t *out_byte,
                                               uint32_t timeout_ms) {
  uint32_t started = millis();

  if (out_byte == NULL) {
    return FR_ERR_INVALID;
  }
  do {
#if FR_BOARD_HAS_NINA
    fr_nina_poll();
#endif
    if (Serial.available() > 0) {
      int value = Serial.read();

      if (value >= 0) {
        *out_byte = (uint8_t)value;
        return FR_OK;
      }
    }
    if (timeout_ms == 0) {
      break;
    }
    yield();
    delay(1);
  } while ((uint32_t)(millis() - started) < timeout_ms);
  return FR_ERR_NOT_FOUND;
}

static fr_err_t fr_rp2040_console_read(uint8_t *out_byte,
                                       uint32_t timeout_ms) {
  if (out_byte == NULL) {
    return FR_ERR_INVALID;
  }
  if (fr_rp2040_typeahead_count > 0) {
    *out_byte = fr_rp2040_typeahead[fr_rp2040_typeahead_start];
    fr_rp2040_typeahead_start =
        (uint8_t)((fr_rp2040_typeahead_start + 1) %
                  FR_RP2040_TYPEAHEAD_BYTES);
    fr_rp2040_typeahead_count--;
    return FR_OK;
  }
  return fr_rp2040_console_driver_read(out_byte, timeout_ms);
}

static fr_err_t fr_rp2040_console_write(const uint8_t *bytes,
                                        uint16_t length) {
  if (bytes == NULL && length > 0) {
    return FR_ERR_INVALID;
  }
  if (length == 0) {
    return FR_OK;
  }
  if (!Serial) {
    return FR_OK;
  }
  return Serial.write(bytes, length) == length ? FR_OK : FR_ERR_IO;
}

#if FR_FEATURE_PERSISTENCE
static uint32_t fr_rp2040_persist_slot_offset(uint8_t slot) {
  return (uint32_t)slot * FR_RP2040_PERSIST_SLOT_BYTES;
}

static const uint8_t *fr_rp2040_persist_slot_bytes(uint8_t slot) {
  return _FS_start + fr_rp2040_persist_slot_offset(slot);
}

static void fr_rp2040_flash_erase(uint32_t offset, size_t length) {
  while (length > 0) {
    uint32_t interrupt_state = save_and_disable_interrupts();

    rp2040.idleOtherCore();
    flash_range_erase(offset, FR_RP2040_FLASH_SECTOR_BYTES);
    rp2040.resumeOtherCore();
    restore_interrupts(interrupt_state);
    offset += FR_RP2040_FLASH_SECTOR_BYTES;
    length -= FR_RP2040_FLASH_SECTOR_BYTES;
    yield();
  }
}

static void fr_rp2040_flash_program(uint32_t offset, const uint8_t *bytes,
                                    size_t length) {
  uint32_t interrupt_state = save_and_disable_interrupts();

  rp2040.idleOtherCore();
  flash_range_program(offset, bytes, length);
  rp2040.resumeOtherCore();
  restore_interrupts(interrupt_state);
}

static fr_err_t
fr_rp2040_persist_slot_info(uint8_t slot, fr_persist_format_info_t *out) {
  const uint8_t *bytes = NULL;
  fr_persist_format_info_t info = {};

  if (slot >= FR_RP2040_PERSIST_SLOT_COUNT || out == NULL) {
    return FR_ERR_INVALID;
  }
  bytes = fr_rp2040_persist_slot_bytes(slot);
  {
    fr_err_t err = fr_persist_format_read_header(bytes, &info);
    if (err != FR_OK) {
      return err;
    }
  }
  if (info.total_length > FR_RP2040_PERSIST_SLOT_BYTES ||
      info.total_length > FR_PERSIST_STORAGE_BYTES) {
    return FR_ERR_CORRUPT;
  }
  {
    fr_err_t err = fr_persist_format_validate_header_payload_crc(
        bytes, fr_crc32(&bytes[FR_PERSIST_HEADER_BYTES],
                        (uint16_t)info.payload_length),
        &info);
    if (err != FR_OK) {
      return err;
    }
  }
  *out = info;
  return FR_OK;
}

static fr_err_t fr_rp2040_persist_pick_read_slot(
    uint8_t image_index, uint8_t *out_slot,
    fr_persist_format_info_t *out_info) {
  fr_persist_format_info_t info[FR_RP2040_PERSIST_SLOT_COUNT] = {};
  bool valid[FR_RP2040_PERSIST_SLOT_COUNT] = {false, false};
  bool saw_corrupt = false;
  uint8_t slots[FR_RP2040_PERSIST_SLOT_COUNT] = {0, 1};
  uint8_t valid_count = 0;

  if (out_slot == NULL || out_info == NULL) {
    return FR_ERR_INVALID;
  }
  for (uint8_t slot = 0; slot < FR_RP2040_PERSIST_SLOT_COUNT; slot++) {
    fr_err_t err = fr_rp2040_persist_slot_info(slot, &info[slot]);
    valid[slot] = err == FR_OK;
    if (err != FR_OK && err != FR_ERR_NOT_FOUND) {
      saw_corrupt = true;
    }
  }
  if (valid[0] && valid[1]) {
    if (info[1].backend_generation > info[0].backend_generation) {
      slots[0] = 1;
      slots[1] = 0;
    }
    valid_count = 2;
  } else if (valid[1]) {
    slots[0] = 1;
    valid_count = 1;
  } else if (valid[0]) {
    valid_count = 1;
  } else {
    return saw_corrupt ? FR_ERR_CORRUPT : FR_ERR_NOT_FOUND;
  }
  if (image_index >= valid_count) {
    return FR_ERR_NOT_FOUND;
  }
  *out_slot = slots[image_index];
  *out_info = info[*out_slot];
  return FR_OK;
}

static fr_err_t fr_rp2040_persist_pick_commit_slot(
    uint8_t *out_slot, uint32_t *out_generation) {
  fr_persist_format_info_t info[FR_RP2040_PERSIST_SLOT_COUNT] = {};
  bool valid[FR_RP2040_PERSIST_SLOT_COUNT] = {false, false};

  if (out_slot == NULL || out_generation == NULL) {
    return FR_ERR_INVALID;
  }
  for (uint8_t slot = 0; slot < FR_RP2040_PERSIST_SLOT_COUNT; slot++) {
    valid[slot] = fr_rp2040_persist_slot_info(slot, &info[slot]) == FR_OK;
  }
  if (valid[0] && valid[1]) {
    if (info[0].backend_generation <= info[1].backend_generation) {
      *out_slot = 0;
      *out_generation = info[1].backend_generation + 1u;
    } else {
      *out_slot = 1;
      *out_generation = info[0].backend_generation + 1u;
    }
  } else if (valid[0]) {
    *out_slot = 1;
    *out_generation = info[0].backend_generation + 1u;
  } else if (valid[1]) {
    *out_slot = 0;
    *out_generation = info[1].backend_generation + 1u;
  } else {
    *out_slot = 0;
    *out_generation = 1;
  }
  return FR_OK;
}

static bool fr_rp2040_pointer_in_mount(const uint8_t *bytes,
                                       uint16_t bytes_length, bool active,
                                       const void *ptr, uint16_t length) {
  uintptr_t pointer = (uintptr_t)ptr;
  uintptr_t base = (uintptr_t)bytes;

  if (!active || bytes == NULL) {
    return false;
  }
  if (length == 0) {
    return true;
  }
  if (ptr == NULL || pointer < base) {
    return false;
  }
  return (uint32_t)(pointer - base) + length <= bytes_length;
}

static fr_err_t fr_rp2040_offset_in_mount(
    const uint8_t *bytes, uint16_t bytes_length, bool active, const void *ptr,
    uint16_t length, uint16_t *out_offset) {
  if (out_offset == NULL) {
    return FR_ERR_INVALID;
  }
  if (!fr_rp2040_pointer_in_mount(bytes, bytes_length, active, ptr, length)) {
    return FR_ERR_NOT_FOUND;
  }
  *out_offset = (uint16_t)((uintptr_t)ptr - (uintptr_t)bytes);
  return FR_OK;
}

static fr_err_t fr_rp2040_persist_flush_page(void) {
  uint32_t flash_offset = 0;

  if (!fr_rp2040_persist_stream.active) {
    return FR_ERR_INVALID;
  }
  if (!fr_rp2040_persist_stream.page_dirty) {
    return FR_OK;
  }
  flash_offset =
      (uint32_t)((uintptr_t)_FS_start - XIP_BASE) +
      fr_rp2040_persist_slot_offset(fr_rp2040_persist_stream.slot) +
      fr_rp2040_persist_stream.page_offset;
  fr_rp2040_flash_program(flash_offset, fr_rp2040_persist_stream.page,
                          sizeof(fr_rp2040_persist_stream.page));
  fr_rp2040_persist_stream.page_dirty = false;
  return FR_OK;
}
#endif

extern "C" {

fr_err_t fr_rp2040_platform_init(void) {
#if FR_FEATURE_PERSISTENCE
  uintptr_t fs_start = (uintptr_t)_FS_start;
  uintptr_t fs_end = (uintptr_t)_FS_end;
#endif

  if (fr_rp2040_initialized) {
    return FR_OK;
  }
#if FR_FEATURE_PERSISTENCE
  if (fs_end < fs_start ||
      fs_end - fs_start < FR_RP2040_PERSIST_REGION_BYTES ||
      ((fs_start - XIP_BASE) % FR_RP2040_FLASH_SECTOR_BYTES) != 0) {
    return FR_ERR_CAPACITY;
  }
#endif
  Serial.begin(115200);
  fr_rp2040_typeahead_clear();
  fr_rp2040_initialized = true;
  return FR_OK;
}

fr_err_t fr_platform_delay_ms(uint16_t ms) {
  delay(ms);
#if FR_BOARD_HAS_NINA
  fr_nina_poll();
#endif
  return FR_OK;
}

fr_err_t fr_platform_delay_us(uint16_t us) {
  delayMicroseconds(us);
  return FR_OK;
}

fr_err_t fr_platform_millis(uint32_t *out_ms) {
  if (out_ms == NULL) {
    return FR_ERR_INVALID;
  }
  *out_ms = (uint32_t)::millis();
  return FR_OK;
}

fr_err_t fr_platform_micros(uint32_t *out_us) {
  if (out_us == NULL) {
    return FR_ERR_INVALID;
  }
  *out_us = (uint32_t)::micros();
  return FR_OK;
}

void fr_platform_yield(void) {
#if FR_BOARD_HAS_NINA
  fr_nina_poll();
#endif
  ::yield();
}

fr_err_t fr_platform_restart(void) {
  rp2040.reboot();
  return FR_ERR_IO;
}

fr_err_t fr_platform_gpio_mode(uint16_t pin, uint16_t mode) {
  if (!fr_rp2040_gpio_valid(pin)) {
    return FR_ERR_DOMAIN;
  }
  if (mode > 2) {
    return FR_ERR_DOMAIN;
  }
#if FR_FEATURE_PWM
  if (fr_rp2040_pwm_pin_in_use(pin)) {
    return FR_ERR_BUSY;
  }
#endif
#if FR_FEATURE_I2C
  if (fr_rp2040_i2c_pin_in_use(pin)) {
    return FR_ERR_BUSY;
  }
#endif

  gpio_init(pin);
  gpio_disable_pulls(pin);
  if (mode == 1) {
    gpio_set_dir(pin, GPIO_OUT);
  } else {
    gpio_set_dir(pin, GPIO_IN);
    if (mode == 2) {
      gpio_pull_up(pin);
    }
  }
  return FR_OK;
}

fr_err_t fr_platform_gpio_write(uint16_t pin, uint16_t value) {
  if (!fr_rp2040_gpio_valid(pin)) {
    return FR_ERR_DOMAIN;
  }
#if FR_FEATURE_PWM
  if (fr_rp2040_pwm_pin_in_use(pin)) {
    return FR_ERR_BUSY;
  }
#endif
#if FR_FEATURE_I2C
  if (fr_rp2040_i2c_pin_in_use(pin)) {
    return FR_ERR_BUSY;
  }
#endif
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, value == 0 ? 0 : 1);
  return FR_OK;
}

fr_err_t fr_platform_gpio_read(uint16_t pin, uint16_t *out_value) {
  if (out_value == NULL) {
    return FR_ERR_INVALID;
  }
  if (!fr_rp2040_gpio_valid(pin)) {
    return FR_ERR_DOMAIN;
  }
  *out_value = gpio_get(pin) == 0 ? 0 : 1;
  return FR_OK;
}

fr_err_t fr_platform_adc_read(uint16_t pin, uint16_t *out_value) {
  static bool initialized;

  if (out_value == NULL) {
    return FR_ERR_INVALID;
  }
  if (pin < FR_RP2040_ADC_FIRST_PIN || pin > FR_RP2040_ADC_LAST_PIN) {
    return FR_ERR_DOMAIN;
  }
#if FR_FEATURE_PWM
  if (fr_rp2040_pwm_pin_in_use(pin)) {
    return FR_ERR_BUSY;
  }
#endif
#if FR_FEATURE_I2C
  if (fr_rp2040_i2c_pin_in_use(pin)) {
    return FR_ERR_BUSY;
  }
#endif
  if (!initialized) {
    adc_init();
    initialized = true;
  }
  adc_gpio_init(pin);
  adc_select_input((uint)pin - FR_RP2040_ADC_FIRST_PIN);
  *out_value = adc_read();
  return FR_OK;
}

fr_err_t fr_platform_poll_interrupt(fr_runtime_t *runtime) {
  uint8_t byte = 0;
  fr_err_t err = FR_OK;

  if (runtime == NULL) {
    return FR_ERR_INVALID;
  }
#if FR_BOARD_HAS_NINA
  fr_nina_poll();
#endif
  err = fr_rp2040_console_driver_read(&byte, 0);
  while (err == FR_OK) {
    if (byte == FR_RP2040_CTRL_C) {
      fr_rp2040_typeahead_clear();
      fr_runtime_interrupt(runtime);
      return FR_OK;
    }
    fr_rp2040_typeahead_push(byte);
    err = fr_rp2040_console_driver_read(&byte, 0);
  }
  return err == FR_ERR_NOT_FOUND ? FR_OK : err;
}

fr_err_t fr_platform_heap_free(uint32_t *out_bytes) {
  int free_heap = 0;

  if (out_bytes == NULL) {
    return FR_ERR_INVALID;
  }
  free_heap = rp2040.getFreeHeap();
  *out_bytes = free_heap > 0 ? (uint32_t)free_heap : 0;
  return FR_OK;
}

fr_err_t fr_platform_heap_largest(uint32_t *out_bytes) {
  if (out_bytes == NULL) {
    return FR_ERR_INVALID;
  }
  *out_bytes = 0;
  return FR_OK;
}

fr_err_t fr_platform_handle_close(fr_handle_kind_t kind,
                                  uint16_t platform_index) {
#if FR_FEATURE_PWM
  if (kind == FR_HANDLE_KIND_PWM) {
    return fr_platform_pwm_close(platform_index);
  }
#endif
#if FR_FEATURE_I2C
  if (kind == FR_HANDLE_KIND_I2C_BUS) {
    return fr_platform_i2c_close(platform_index);
  }
#endif
#if FR_FEATURE_NET
  if (kind == FR_HANDLE_KIND_TCP) {
    return fr_platform_tcp_close(platform_index);
  }
#endif
  (void)kind;
  (void)platform_index;
  return FR_OK;
}

fr_err_t fr_platform_event_gpio_install(fr_event_kind_t kind, uint16_t pin,
                                        uint16_t binding_index,
                                        uint16_t generation) {
  (void)kind;
  (void)pin;
  (void)binding_index;
  (void)generation;
  return FR_ERR_UNSUPPORTED;
}

fr_err_t fr_platform_event_gpio_remove(uint16_t pin) {
  (void)pin;
  return FR_OK;
}

fr_err_t fr_platform_event_timer_install(fr_event_kind_t kind, uint32_t ms,
                                         uint16_t binding_index,
                                         uint16_t generation) {
  (void)kind;
  (void)ms;
  (void)binding_index;
  (void)generation;
  return FR_ERR_UNSUPPORTED;
}

fr_err_t fr_platform_event_timer_remove(uint16_t binding_index) {
  (void)binding_index;
  return FR_OK;
}

fr_err_t fr_platform_event_drain(fr_event_candidate_t *out_events,
                                 uint8_t out_cap, uint8_t *out_count,
                                 uint32_t *overflow_delta) {
  if ((out_events == NULL && out_cap > 0) || out_count == NULL ||
      overflow_delta == NULL) {
    return FR_ERR_INVALID;
  }
  *out_count = 0;
  *overflow_delta = 0;
  return FR_OK;
}

fr_err_t fr_platform_event_post_test_candidate(uint16_t binding_index,
                                               uint16_t generation,
                                               uint32_t timestamp_ms) {
  (void)binding_index;
  (void)generation;
  (void)timestamp_ms;
  return FR_ERR_UNSUPPORTED;
}

#if FR_FEATURE_REPL
void fr_platform_set_idle_handler(fr_platform_idle_fn handler, void *context) {
  fr_rp2040_idle_handler = handler;
  fr_rp2040_idle_context = context;
}

static fr_err_t fr_rp2040_read_edited_line(char *line, uint16_t cap,
                                           bool program_input, bool *out_eof,
                                           uint16_t *out_length) {
  uint16_t used = 0;

  if (line == NULL || cap == 0 || out_eof == NULL || out_length == NULL) {
    return FR_ERR_INVALID;
  }
  *out_eof = false;
  *out_length = 0;
  line[0] = '\0';

  for (;;) {
    uint8_t byte = 0;
    fr_err_t err = fr_rp2040_console_read(&byte, 20);

    if (err == FR_ERR_NOT_FOUND) {
      if (!program_input && fr_rp2040_idle_handler != NULL) {
        (void)fr_rp2040_idle_handler(fr_rp2040_idle_context);
      }
      continue;
    }
    if (err != FR_OK) {
      return err;
    }
    if (byte == '\r' || byte == '\n') {
      line[used] = '\0';
      *out_length = used;
      (void)fr_platform_write_text("\n");
      return FR_OK;
    }
    if (byte == FR_RP2040_CTRL_C) {
      line[0] = '\0';
      (void)fr_platform_write_text("^C\n");
      return program_input ? FR_ERR_INTERRUPTED : FR_OK;
    }
    if (byte == FR_RP2040_BACKSPACE || byte == FR_RP2040_DELETE) {
      if (used > 0) {
        used--;
        line[used] = '\0';
        (void)fr_platform_write_text("\b \b");
      }
      continue;
    }
    if (byte < 32 || byte > 126) {
      continue;
    }
    if ((uint16_t)(used + 1) >= cap) {
      return FR_ERR_RANGE;
    }
    line[used++] = (char)byte;
    line[used] = '\0';
    (void)fr_rp2040_console_write(&byte, 1);
  }
}

fr_err_t fr_platform_read_line(char *line, uint16_t cap, bool *out_eof) {
  uint16_t length = 0;

  return fr_rp2040_read_edited_line(line, cap, false, out_eof, &length);
}

fr_err_t fr_platform_console_read_line(uint8_t *bytes, uint16_t cap,
                                       uint16_t *out_length) {
  bool eof = false;

  return fr_rp2040_read_edited_line((char *)bytes, cap, true, &eof,
                                    out_length);
}

fr_err_t fr_platform_write_text(const char *text) {
  if (text == NULL) {
    return FR_ERR_INVALID;
  }
  while (*text != '\0') {
    if (*text == '\n') {
      const uint8_t carriage_return = '\r';
      fr_err_t err = fr_rp2040_console_write(&carriage_return, 1);
      if (err != FR_OK) {
        return err;
      }
    }
    {
      fr_err_t err =
          fr_rp2040_console_write((const uint8_t *)text, 1);
      if (err != FR_OK) {
        return err;
      }
    }
    text++;
  }
  return FR_OK;
}
#endif

#if FR_FEATURE_REPL || FR_FEATURE_PAD
fr_err_t fr_platform_write_bytes(const uint8_t *bytes, uint16_t length) {
  return fr_rp2040_console_write(bytes, length);
}
#endif

#if FR_FEATURE_RANDOM
static uint32_t fr_rp2040_random_state = 1;

uint32_t fr_platform_random_next(void) {
  uint32_t state = fr_rp2040_random_state;

  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  fr_rp2040_random_state = state;
  return state;
}

void fr_platform_random_seed(uint32_t seed) {
  fr_rp2040_random_state = seed == 0 ? 1 : seed;
}
#endif

#if FR_FEATURE_PWM
static fr_err_t fr_rp2040_pwm_timing(uint16_t freq, uint16_t *out_div16,
                                     uint16_t *out_wrap) {
  uint64_t clock_hz = clock_get_hz(clk_sys);
  uint64_t denominator = (uint64_t)freq * 65536u;
  uint64_t div16 = 0;
  uint64_t period = 0;

  if (freq == 0 || out_div16 == NULL || out_wrap == NULL) {
    return FR_ERR_INVALID;
  }
  div16 = (clock_hz * 16u + denominator - 1u) / denominator;
  if (div16 < 16u) {
    div16 = 16u;
  }
  if (div16 > 4095u) {
    return FR_ERR_DOMAIN;
  }
  period = (clock_hz * 16u + ((uint64_t)freq * div16) / 2u) /
           ((uint64_t)freq * div16);
  if (period < 2u || period > 65536u) {
    return FR_ERR_DOMAIN;
  }
  *out_div16 = (uint16_t)div16;
  *out_wrap = (uint16_t)(period - 1u);
  return FR_OK;
}

fr_err_t fr_platform_pwm_open(uint16_t pin, uint16_t freq,
                              uint16_t *out_platform_index) {
  uint16_t div16 = 0;
  uint16_t wrap = 0;
  uint slice = 0;
  uint channel = 0;
  bool slice_in_use = false;
  uint16_t free_index = FR_RP2040_PWM_MAX;

  if (out_platform_index == NULL) {
    return FR_ERR_INVALID;
  }
  if (!fr_rp2040_gpio_valid(pin) || freq == 0) {
    return FR_ERR_DOMAIN;
  }
  {
    fr_err_t err = fr_rp2040_pwm_timing(freq, &div16, &wrap);
    if (err != FR_OK) {
      return err;
    }
  }
#if FR_FEATURE_I2C
  if (fr_rp2040_i2c_pin_in_use(pin)) {
    return FR_ERR_BUSY;
  }
#endif
  slice = pwm_gpio_to_slice_num(pin);
  channel = pwm_gpio_to_channel(pin);

  for (uint16_t i = 0; i < FR_RP2040_PWM_MAX; i++) {
    const fr_rp2040_pwm_t *pwm = &fr_rp2040_pwms[i];

    if (!pwm->in_use) {
      if (free_index == FR_RP2040_PWM_MAX) {
        free_index = i;
      }
      continue;
    }
    if (pwm->pin == pin ||
        (pwm->slice == slice && pwm->channel == channel)) {
      return FR_ERR_BUSY;
    }
    if (pwm->slice == slice) {
      if (pwm->freq != freq || pwm->wrap != wrap) {
        return FR_ERR_BUSY;
      }
      slice_in_use = true;
    }
  }
  if (free_index == FR_RP2040_PWM_MAX) {
    return FR_ERR_CAPACITY;
  }
  if (!slice_in_use) {
    pwm_config config = pwm_get_default_config();

    pwm_config_set_clkdiv_int_frac(&config, (uint8_t)(div16 / 16u),
                                   (uint8_t)(div16 % 16u));
    pwm_config_set_wrap(&config, wrap);
    pwm_init(slice, &config, true);
  }
  gpio_set_function(pin, GPIO_FUNC_PWM);
  pwm_set_chan_level(slice, channel, 0);
  fr_rp2040_pwms[free_index] = {
      true, pin, freq, wrap, (uint8_t)slice, (uint8_t)channel};
  *out_platform_index = free_index;
  return FR_OK;
}

fr_err_t fr_platform_pwm_find(uint16_t pin, uint16_t freq,
                              uint16_t *out_platform_index) {
  if (out_platform_index == NULL || freq == 0) {
    return FR_ERR_INVALID;
  }
  for (uint16_t i = 0; i < FR_RP2040_PWM_MAX; i++) {
    const fr_rp2040_pwm_t *pwm = &fr_rp2040_pwms[i];

    if (!pwm->in_use || pwm->pin != pin) {
      continue;
    }
    if (pwm->freq != freq) {
      return FR_ERR_BUSY;
    }
    *out_platform_index = i;
    return FR_OK;
  }
  return FR_ERR_NOT_FOUND;
}

fr_err_t fr_platform_pwm_write(uint16_t platform_index, uint16_t duty) {
  fr_rp2040_pwm_t *pwm = fr_rp2040_pwm_entry(platform_index);
  uint16_t level = 0;

  if (pwm == NULL) {
    return FR_ERR_HANDLE;
  }
  if (duty > 10000u) {
    return FR_ERR_DOMAIN;
  }
  level = (uint16_t)(((uint32_t)duty * pwm->wrap) / 10000u);
  pwm_set_chan_level(pwm->slice, pwm->channel, level);
  return FR_OK;
}

fr_err_t fr_platform_pwm_close(uint16_t platform_index) {
  fr_rp2040_pwm_t *pwm = fr_rp2040_pwm_entry(platform_index);
  uint8_t slice = 0;
  uint16_t pin = 0;
  bool slice_still_used = false;

  if (pwm == NULL) {
    return FR_ERR_HANDLE;
  }
  slice = pwm->slice;
  pin = pwm->pin;
  pwm_set_chan_level(pwm->slice, pwm->channel, 0);
  memset(pwm, 0, sizeof(*pwm));

  for (uint16_t i = 0; i < FR_RP2040_PWM_MAX; i++) {
    if (fr_rp2040_pwms[i].in_use &&
        fr_rp2040_pwms[i].slice == slice) {
      slice_still_used = true;
      break;
    }
  }
  if (!slice_still_used) {
    pwm_set_enabled(slice, false);
  }
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, 0);
  return FR_OK;
}
#endif

#if FR_FEATURE_I2C
static i2c_inst_t *fr_rp2040_i2c_instance(uint16_t port) {
  return port == 0 ? i2c0 : i2c1;
}

static bool fr_rp2040_i2c_pins_valid(uint16_t port, uint16_t sda,
                                     uint16_t scl) {
  if (port >= FR_RP2040_I2C_MAX || !fr_rp2040_gpio_valid(sda) ||
      !fr_rp2040_gpio_valid(scl) || sda == scl) {
    return false;
  }
  if (port == 0) {
    return sda % 4u == 0 && scl % 4u == 1;
  }
  return sda % 4u == 2 && scl % 4u == 3;
}

static fr_rp2040_i2c_t *fr_rp2040_i2c_entry(uint16_t index) {
  if (index >= FR_RP2040_I2C_MAX || !fr_rp2040_i2cs[index].in_use) {
    return NULL;
  }
  return &fr_rp2040_i2cs[index];
}

fr_err_t fr_platform_i2c_open(uint16_t port, uint16_t sda, uint16_t scl,
                              uint32_t freq,
                              uint16_t *out_platform_index) {
  if (out_platform_index == NULL) {
    return FR_ERR_INVALID;
  }
  if (!fr_rp2040_i2c_pins_valid(port, sda, scl) || freq == 0 ||
      freq > FR_RP2040_I2C_MAX_HZ || fr_rp2040_i2cs[port].in_use) {
    return FR_ERR_DOMAIN;
  }
#if FR_FEATURE_PWM
  if (fr_rp2040_pwm_pin_in_use(sda) ||
      fr_rp2040_pwm_pin_in_use(scl)) {
    return FR_ERR_BUSY;
  }
#endif
  i2c_init(fr_rp2040_i2c_instance(port), freq);
  gpio_set_function(sda, GPIO_FUNC_I2C);
  gpio_set_function(scl, GPIO_FUNC_I2C);
  gpio_pull_up(sda);
  gpio_pull_up(scl);
  fr_rp2040_i2cs[port] = {true, sda, scl, freq};
  *out_platform_index = port;
  return FR_OK;
}

fr_err_t fr_platform_i2c_write(uint16_t platform_index, uint8_t addr,
                               const uint8_t *bytes, uint16_t length) {
  fr_rp2040_i2c_t *bus = fr_rp2040_i2c_entry(platform_index);
  int written = 0;

  if (bus == NULL) {
    return FR_ERR_HANDLE;
  }
  if (addr > 0x7fu) {
    return FR_ERR_DOMAIN;
  }
  if (bytes == NULL && length > 0) {
    return FR_ERR_INVALID;
  }
  if (length == 0) {
    return FR_OK;
  }
  written = i2c_write_blocking_until(
      fr_rp2040_i2c_instance(platform_index), addr, bytes, length, false,
      make_timeout_time_ms(FR_RP2040_I2C_TIMEOUT_MS));
  return written == length ? FR_OK : FR_ERR_IO;
}

fr_err_t fr_platform_i2c_read(uint16_t platform_index, uint8_t addr,
                              uint8_t *bytes, uint16_t length) {
  fr_rp2040_i2c_t *bus = fr_rp2040_i2c_entry(platform_index);
  int read = 0;

  if (bus == NULL) {
    return FR_ERR_HANDLE;
  }
  if (addr > 0x7fu) {
    return FR_ERR_DOMAIN;
  }
  if (bytes == NULL && length > 0) {
    return FR_ERR_INVALID;
  }
  if (length == 0) {
    return FR_OK;
  }
  read = i2c_read_blocking_until(
      fr_rp2040_i2c_instance(platform_index), addr, bytes, length, false,
      make_timeout_time_ms(FR_RP2040_I2C_TIMEOUT_MS));
  return read == length ? FR_OK : FR_ERR_IO;
}

fr_err_t fr_platform_i2c_write_read(uint16_t platform_index, uint8_t addr,
                                    const uint8_t *write_bytes,
                                    uint16_t write_length,
                                    uint8_t *read_bytes,
                                    uint16_t read_length) {
  fr_rp2040_i2c_t *bus = fr_rp2040_i2c_entry(platform_index);
  absolute_time_t deadline =
      make_timeout_time_ms(FR_RP2040_I2C_TIMEOUT_MS);

  if (bus == NULL) {
    return FR_ERR_HANDLE;
  }
  if (addr > 0x7fu) {
    return FR_ERR_DOMAIN;
  }
  if ((write_bytes == NULL && write_length > 0) ||
      (read_bytes == NULL && read_length > 0)) {
    return FR_ERR_INVALID;
  }
  if (write_length > 0) {
    int written = i2c_write_blocking_until(
        fr_rp2040_i2c_instance(platform_index), addr, write_bytes,
        write_length, read_length > 0, deadline);
    if (written != write_length) {
      return FR_ERR_IO;
    }
  }
  if (read_length > 0) {
    int read = i2c_read_blocking_until(
        fr_rp2040_i2c_instance(platform_index), addr, read_bytes,
        read_length, false, deadline);
    if (read != read_length) {
      return FR_ERR_IO;
    }
  }
  return FR_OK;
}

fr_err_t fr_platform_i2c_close(uint16_t platform_index) {
  fr_rp2040_i2c_t *bus = fr_rp2040_i2c_entry(platform_index);
  uint16_t sda = 0;
  uint16_t scl = 0;

  if (bus == NULL) {
    return FR_ERR_HANDLE;
  }
  sda = bus->sda;
  scl = bus->scl;
  i2c_deinit(fr_rp2040_i2c_instance(platform_index));
  memset(bus, 0, sizeof(*bus));
  gpio_init(sda);
  gpio_init(scl);
  gpio_set_dir(sda, GPIO_IN);
  gpio_set_dir(scl, GPIO_IN);
  gpio_disable_pulls(sda);
  gpio_disable_pulls(scl);
  return FR_OK;
}
#endif

#if FR_FEATURE_PERSISTENCE
fr_err_t fr_platform_persist_read(uint8_t *bytes, uint16_t cap,
                                  uint16_t *out_length,
                                  uint8_t image_index) {
  uint8_t slot = 0;
  fr_persist_format_info_t info = {};
  fr_err_t err = FR_OK;

  if (bytes == NULL || out_length == NULL) {
    return FR_ERR_INVALID;
  }
  err = fr_rp2040_persist_pick_read_slot(image_index, &slot, &info);
  if (err != FR_OK) {
    return err;
  }
  if (cap < info.total_length) {
    return FR_ERR_CAPACITY;
  }
  memcpy(bytes, fr_rp2040_persist_slot_bytes(slot), info.total_length);
  *out_length = (uint16_t)info.total_length;
  return FR_OK;
}

fr_err_t fr_platform_persist_mount(uint8_t image_index,
                                   const uint8_t **out_bytes,
                                   uint16_t *out_length) {
  uint8_t slot = 0;
  fr_persist_format_info_t info = {};
  fr_err_t err = FR_OK;

  if (out_bytes == NULL || out_length == NULL) {
    return FR_ERR_INVALID;
  }
  fr_platform_persist_mount_discard();
  err = fr_rp2040_persist_pick_read_slot(image_index, &slot, &info);
  if (err != FR_OK) {
    return err;
  }
  fr_rp2040_persist_candidate_bytes =
      fr_rp2040_persist_slot_bytes(slot);
  fr_rp2040_persist_candidate_length = (uint16_t)info.total_length;
  fr_rp2040_persist_candidate = true;
  *out_bytes = fr_rp2040_persist_candidate_bytes;
  *out_length = fr_rp2040_persist_candidate_length;
  return FR_OK;
}

fr_err_t fr_platform_persist_mount_commit(void) {
  if (!fr_rp2040_persist_candidate) {
    return FR_ERR_INVALID;
  }
  fr_rp2040_persist_mounted_bytes = fr_rp2040_persist_candidate_bytes;
  fr_rp2040_persist_mounted_length =
      fr_rp2040_persist_candidate_length;
  fr_rp2040_persist_mounted = true;
  fr_platform_persist_mount_discard();
  return FR_OK;
}

void fr_platform_persist_mount_discard(void) {
  fr_rp2040_persist_candidate_bytes = NULL;
  fr_rp2040_persist_candidate_length = 0;
  fr_rp2040_persist_candidate = false;
}

void fr_platform_persist_unmount(void) {
  fr_platform_persist_mount_discard();
  fr_rp2040_persist_mounted_bytes = NULL;
  fr_rp2040_persist_mounted_length = 0;
  fr_rp2040_persist_mounted = false;
}

bool fr_platform_persist_pointer_is_mounted(const void *ptr,
                                            uint16_t length) {
  return fr_rp2040_pointer_in_mount(
      fr_rp2040_persist_mounted_bytes, fr_rp2040_persist_mounted_length,
      fr_rp2040_persist_mounted, ptr, length);
}

bool fr_platform_persist_code_pointer_is_direct(const void *ptr,
                                                uint16_t length) {
  return fr_rp2040_pointer_in_mount(
             fr_rp2040_persist_candidate_bytes,
             fr_rp2040_persist_candidate_length,
             fr_rp2040_persist_candidate, ptr, length) ||
         fr_platform_persist_pointer_is_mounted(ptr, length);
}

fr_err_t fr_platform_persist_mounted_offset(const void *ptr,
                                            uint16_t length,
                                            uint16_t *out_offset) {
  fr_err_t err = fr_rp2040_offset_in_mount(
      fr_rp2040_persist_candidate_bytes,
      fr_rp2040_persist_candidate_length, fr_rp2040_persist_candidate, ptr,
      length, out_offset);

  if (err == FR_OK) {
    return FR_OK;
  }
  return fr_rp2040_offset_in_mount(
      fr_rp2040_persist_mounted_bytes, fr_rp2040_persist_mounted_length,
      fr_rp2040_persist_mounted, ptr, length, out_offset);
}

fr_err_t fr_platform_persist_read_mounted(uint16_t offset, uint8_t *dst,
                                          uint16_t length) {
  const uint8_t *bytes = NULL;
  uint16_t bytes_length = 0;

  if (dst == NULL && length > 0) {
    return FR_ERR_INVALID;
  }
  if (fr_rp2040_persist_candidate) {
    bytes = fr_rp2040_persist_candidate_bytes;
    bytes_length = fr_rp2040_persist_candidate_length;
  } else if (fr_rp2040_persist_mounted) {
    bytes = fr_rp2040_persist_mounted_bytes;
    bytes_length = fr_rp2040_persist_mounted_length;
  } else {
    return FR_ERR_NOT_FOUND;
  }
  if (offset > bytes_length || length > bytes_length - offset) {
    return FR_ERR_RANGE;
  }
  if (length > 0) {
    memcpy(dst, &bytes[offset], length);
  }
  return FR_OK;
}

fr_err_t fr_platform_persist_stream_begin(void) {
  uint8_t slot = 0;
  uint32_t generation = 0;
  uint32_t flash_offset = 0;
  fr_err_t err = FR_OK;

  fr_platform_persist_stream_abort();
  err = fr_rp2040_persist_pick_commit_slot(&slot, &generation);
  if (err != FR_OK) {
    return err;
  }
  flash_offset = (uint32_t)((uintptr_t)_FS_start - XIP_BASE) +
                 fr_rp2040_persist_slot_offset(slot);
  fr_rp2040_flash_erase(flash_offset, FR_RP2040_PERSIST_SLOT_BYTES);

  memset(&fr_rp2040_persist_stream, 0,
         sizeof(fr_rp2040_persist_stream));
  fr_rp2040_persist_stream.active = true;
  fr_rp2040_persist_stream.slot = slot;
  fr_rp2040_persist_stream.cursor = FR_PERSIST_HEADER_BYTES;
  fr_rp2040_persist_stream.backend_generation = generation;
  memset(fr_rp2040_persist_stream.page, 0xff,
         sizeof(fr_rp2040_persist_stream.page));
  return FR_OK;
}

fr_err_t fr_platform_persist_stream_write(const uint8_t *bytes,
                                          uint16_t length) {
  const uint8_t *cursor = bytes;
  uint16_t remaining = length;
  uint32_t next_payload_length = 0;

  if (bytes == NULL && length > 0) {
    return FR_ERR_INVALID;
  }
  if (!fr_rp2040_persist_stream.active) {
    return FR_ERR_INVALID;
  }
  next_payload_length =
      fr_rp2040_persist_stream.payload_length + length;
  if (next_payload_length > FR_PERSIST_PAYLOAD_BYTES) {
    return FR_ERR_CAPACITY;
  }
  fr_rp2040_persist_stream.payload_length = next_payload_length;

  while (remaining > 0) {
    uint16_t used = (uint16_t)(fr_rp2040_persist_stream.cursor -
                               fr_rp2040_persist_stream.page_offset);
    uint16_t available =
        (uint16_t)(FR_RP2040_FLASH_PAGE_BYTES - used);
    uint16_t copied = remaining < available ? remaining : available;

    memcpy(&fr_rp2040_persist_stream.page[used], cursor, copied);
    fr_rp2040_persist_stream.page_dirty = true;
    fr_rp2040_persist_stream.cursor += copied;
    cursor += copied;
    remaining = (uint16_t)(remaining - copied);

    if (copied == available) {
      fr_err_t err = fr_rp2040_persist_flush_page();
      if (err != FR_OK) {
        return err;
      }
      fr_rp2040_persist_stream.page_offset +=
          FR_RP2040_FLASH_PAGE_BYTES;
      memset(fr_rp2040_persist_stream.page, 0xff,
             sizeof(fr_rp2040_persist_stream.page));
    }
  }
  return FR_OK;
}

fr_err_t fr_platform_persist_stream_finalize(
    const uint8_t header[FR_PERSIST_HEADER_BYTES]) {
  uint8_t stamped[FR_PERSIST_HEADER_BYTES];
  uint32_t flash_offset = 0;
  uint8_t slot = 0;
  fr_persist_format_info_t info = {};
  fr_err_t err = FR_OK;

  if (header == NULL) {
    return FR_ERR_INVALID;
  }
  if (!fr_rp2040_persist_stream.active) {
    return FR_ERR_INVALID;
  }
  err = fr_rp2040_persist_flush_page();
  if (err != FR_OK) {
    return err;
  }

  memcpy(stamped, header, sizeof(stamped));
  err = fr_persist_format_stamp_generation(
      stamped, fr_rp2040_persist_stream.backend_generation);
  if (err != FR_OK) {
    return err;
  }
  slot = fr_rp2040_persist_stream.slot;
  memcpy(fr_rp2040_persist_stream.page,
         fr_rp2040_persist_slot_bytes(slot),
         sizeof(fr_rp2040_persist_stream.page));
  memcpy(fr_rp2040_persist_stream.page, stamped, sizeof(stamped));
  flash_offset = (uint32_t)((uintptr_t)_FS_start - XIP_BASE) +
                 fr_rp2040_persist_slot_offset(slot);
  fr_rp2040_flash_program(flash_offset, fr_rp2040_persist_stream.page,
                          sizeof(fr_rp2040_persist_stream.page));
  fr_rp2040_persist_stream.active = false;

  err = fr_rp2040_persist_slot_info(slot, &info);
  return err == FR_OK ? FR_OK : FR_ERR_IO;
}

void fr_platform_persist_stream_abort(void) {
  memset(&fr_rp2040_persist_stream, 0,
         sizeof(fr_rp2040_persist_stream));
}

fr_err_t fr_platform_persist_clear(void) {
  uint32_t flash_offset =
      (uint32_t)((uintptr_t)_FS_start - XIP_BASE);

  fr_platform_persist_unmount();
  fr_platform_persist_stream_abort();
  fr_rp2040_flash_erase(flash_offset, FR_RP2040_PERSIST_REGION_BYTES);
  return FR_OK;
}
#endif

}
