#include "ffi.h"

#include "froth_console.h"
#include "froth_vm.h"
#include "frothy_tm1629.h"
#include "platform.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"
#else
#include <unistd.h>
#endif

#define BOARD_PIN_LED_BUILTIN 2
#define BOARD_PIN_BOOT_BUTTON 0
#define BOARD_PIN_A0 34
#define BOARD_PIN_UART_TX 17
#define BOARD_PIN_UART_RX 16
#define BOARD_PIN_TM1629_STB 18
#define BOARD_PIN_TM1629_CLK 19
#define BOARD_PIN_TM1629_DIO 23
#define BOARD_PIN_POT_LEFT 33
#define BOARD_PIN_POT_RIGHT 32
#define BOARD_PIN_JOY_1 13
#define BOARD_PIN_JOY_2 25
#define BOARD_PIN_JOY_3 16
#define BOARD_PIN_JOY_4 17
#define BOARD_PIN_JOY_6 14
#define BOARD_PIN_BUTTON_1 34
#define BOARD_PIN_BUTTON_2 35
#define BOARD_PIN_BUTTON_3 36

#ifndef ESP_PLATFORM
#define BOARD_POSIX_GPIO_MAX 40
#endif

#if defined(ESP_PLATFORM) && defined(ADC_ATTEN_DB_12)
#define BOARD_ADC_ATTEN ADC_ATTEN_DB_12
#elif defined(ESP_PLATFORM)
#define BOARD_ADC_ATTEN ADC_ATTEN_DB_11
#endif

static frothy_tm1629_t board_tm1629;
static bool board_runtime_initialized = false;
static uint32_t board_random_state = 1;

#ifdef ESP_PLATFORM
static uint8_t board_gpio_output_shadow_valid[GPIO_NUM_MAX];
static int32_t board_gpio_output_shadow_levels[GPIO_NUM_MAX];
#else
static uint8_t board_gpio_known[BOARD_POSIX_GPIO_MAX];
static int32_t board_gpio_levels[BOARD_POSIX_GPIO_MAX];
#endif

static froth_error_t board_throw_program_interrupted(froth_vm_t *froth_vm) {
  froth_vm->interrupted = 0;
  froth_vm->thrown = FROTH_OK;
  return FROTH_ERROR_PROGRAM_INTERRUPTED;
}

static froth_error_t board_poll_interruptible_wait(froth_vm_t *froth_vm) {
  froth_console_poll(froth_vm);
  if (froth_vm->interrupted) {
    return board_throw_program_interrupted(froth_vm);
  }
  return FROTH_OK;
}

static froth_error_t board_delay_interruptible_ms(froth_vm_t *froth_vm,
                                                  int32_t delay_ms) {
  while (delay_ms > 0) {
    int32_t chunk = delay_ms > 10 ? 10 : delay_ms;

#ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(chunk));
#else
    usleep((useconds_t)chunk * 1000);
#endif
    delay_ms -= chunk;
    FROTH_TRY(board_poll_interruptible_wait(froth_vm));
  }

  return FROTH_OK;
}

#ifdef ESP_PLATFORM
static bool board_adc1_channel_for_pin(int32_t pin, adc1_channel_t *channel_out) {
  switch (pin) {
  case 32:
    *channel_out = ADC1_CHANNEL_4;
    return true;
  case 33:
    *channel_out = ADC1_CHANNEL_5;
    return true;
  case 34:
    *channel_out = ADC1_CHANNEL_6;
    return true;
  case 35:
    *channel_out = ADC1_CHANNEL_7;
    return true;
  case 36:
    *channel_out = ADC1_CHANNEL_0;
    return true;
  case 37:
    *channel_out = ADC1_CHANNEL_1;
    return true;
  case 38:
    *channel_out = ADC1_CHANNEL_2;
    return true;
  case 39:
    *channel_out = ADC1_CHANNEL_3;
    return true;
  default:
    return false;
  }
}

static bool board_gpio_pin_valid(int32_t pin) {
  return pin >= 0 && GPIO_IS_VALID_GPIO((gpio_num_t)pin);
}

static bool board_adc_pin_valid(int32_t pin) {
  adc1_channel_t channel;

  return board_adc1_channel_for_pin(pin, &channel);
}

static void board_gpio_write_fast(int32_t pin, bool high) {
  if (pin < 32) {
    if (high) {
      GPIO.out_w1ts = (uint32_t)(1u << pin);
    } else {
      GPIO.out_w1tc = (uint32_t)(1u << pin);
    }
    return;
  }

  if (high) {
    GPIO.out1_w1ts.val = (uint32_t)(1u << (pin - 32));
  } else {
    GPIO.out1_w1tc.val = (uint32_t)(1u << (pin - 32));
  }
}

static bool board_tm1629_pin_mode(void *context, int32_t pin, bool output) {
  (void)context;
  if (!board_gpio_pin_valid(pin)) {
    return false;
  }

  if (gpio_set_direction((gpio_num_t)pin,
                         output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT) !=
      ESP_OK) {
    return false;
  }
  if (output) {
    board_gpio_output_shadow_valid[pin] = 1;
    board_gpio_output_shadow_levels[pin] = gpio_get_level((gpio_num_t)pin) ? 1 : 0;
  } else {
    board_gpio_output_shadow_valid[pin] = 0;
  }
  return true;
}

static void board_tm1629_pin_write(void *context, int32_t pin, bool high) {
  (void)context;
  if (!board_gpio_pin_valid(pin)) {
    return;
  }

  board_gpio_write_fast(pin, high);
  board_gpio_output_shadow_valid[pin] = 1;
  board_gpio_output_shadow_levels[pin] = high ? 1 : 0;
}

static void board_tm1629_delay_us(void *context, uint32_t usec) {
  (void)context;
  esp_rom_delay_us(usec);
}
#else
static bool board_gpio_pin_valid(int32_t pin) {
  return pin == BOARD_PIN_LED_BUILTIN || pin == BOARD_PIN_BOOT_BUTTON ||
         pin == BOARD_PIN_A0 || pin == BOARD_PIN_UART_TX ||
         pin == BOARD_PIN_UART_RX || pin == BOARD_PIN_TM1629_STB ||
         pin == BOARD_PIN_TM1629_CLK || pin == BOARD_PIN_TM1629_DIO ||
         pin == BOARD_PIN_POT_LEFT || pin == BOARD_PIN_POT_RIGHT ||
         pin == BOARD_PIN_JOY_1 || pin == BOARD_PIN_JOY_2 ||
         pin == BOARD_PIN_JOY_3 || pin == BOARD_PIN_JOY_4 ||
         pin == BOARD_PIN_JOY_6 || pin == BOARD_PIN_BUTTON_1 ||
         pin == BOARD_PIN_BUTTON_2 || pin == BOARD_PIN_BUTTON_3;
}

static bool board_adc_pin_valid(int32_t pin) {
  return pin == BOARD_PIN_A0 || pin == BOARD_PIN_POT_LEFT ||
         pin == BOARD_PIN_POT_RIGHT || pin == BOARD_PIN_BUTTON_1 ||
         pin == BOARD_PIN_BUTTON_2 || pin == BOARD_PIN_BUTTON_3;
}

static bool board_tm1629_pin_mode(void *context, int32_t pin, bool output) {
  (void)context;
  (void)output;
  if (!board_gpio_pin_valid(pin) || pin >= BOARD_POSIX_GPIO_MAX) {
    return false;
  }

  board_gpio_known[pin] = 1;
  return true;
}

static void board_tm1629_pin_write(void *context, int32_t pin, bool high) {
  (void)context;
  if (!board_gpio_pin_valid(pin) || pin >= BOARD_POSIX_GPIO_MAX) {
    return;
  }

  board_gpio_known[pin] = 1;
  board_gpio_levels[pin] = high ? 1 : 0;
}

static void board_tm1629_delay_us(void *context, uint32_t usec) {
  (void)context;
  (void)usec;
}
#endif

static bool board_tm1629_pins_distinct(int32_t stb_pin, int32_t clk_pin,
                                       int32_t dio_pin) {
  return stb_pin != clk_pin && stb_pin != dio_pin && clk_pin != dio_pin;
}

static void board_init_runtime_state(void) {
  frothy_tm1629_hal_t hal = {
      .context = NULL,
      .pin_mode = board_tm1629_pin_mode,
      .pin_write = board_tm1629_pin_write,
      .delay_us = board_tm1629_delay_us,
  };

#ifdef ESP_PLATFORM
  memset(board_gpio_output_shadow_valid, 0, sizeof(board_gpio_output_shadow_valid));
  memset(board_gpio_output_shadow_levels, 0,
         sizeof(board_gpio_output_shadow_levels));
#else
  memset(board_gpio_known, 0, sizeof(board_gpio_known));
  memset(board_gpio_levels, 0, sizeof(board_gpio_levels));
#endif
  board_random_state = frothy_ffi_random_seed(1);
  frothy_tm1629_init(&board_tm1629, &hal);
  board_runtime_initialized = true;
}

static void board_ensure_runtime_state(void) {
  if (!board_runtime_initialized) {
    board_init_runtime_state();
  }
}

void froth_board_reset_runtime_state(void) {
  if (board_runtime_initialized) {
    frothy_tm1629_factory_reset(&board_tm1629);
  }
  board_init_runtime_state();
}

static froth_error_t board_gpio_mode_cb(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;
  int32_t mode = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &mode));
  if (!board_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

#ifdef ESP_PLATFORM
  {
    esp_err_t err = gpio_set_direction((gpio_num_t)pin,
                                       mode == 1 ? GPIO_MODE_OUTPUT
                                                 : GPIO_MODE_INPUT);

    if (err != ESP_OK) {
      return FROTH_ERROR_IO;
    }
    if (mode == 1) {
      board_gpio_output_shadow_valid[pin] = 1;
      board_gpio_output_shadow_levels[pin] =
          gpio_get_level((gpio_num_t)pin) ? 1 : 0;
    } else {
      board_gpio_output_shadow_valid[pin] = 0;
    }
  }
#else
  if (pin >= BOARD_POSIX_GPIO_MAX) {
    return FROTH_ERROR_BOUNDS;
  }
  board_gpio_known[pin] = 1;
#endif

  return frothy_ffi_return_nil(out);
}

static froth_error_t board_gpio_write_cb(frothy_runtime_t *runtime,
                                         const void *context,
                                         const frothy_value_t *args,
                                         size_t arg_count,
                                         frothy_value_t *out) {
  int32_t pin = 0;
  int32_t level = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &level));
  if (!board_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

#ifdef ESP_PLATFORM
  if (gpio_set_level((gpio_num_t)pin, level ? 1 : 0) != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  board_gpio_output_shadow_valid[pin] = 1;
  board_gpio_output_shadow_levels[pin] = level ? 1 : 0;
#else
  if (pin >= BOARD_POSIX_GPIO_MAX) {
    return FROTH_ERROR_BOUNDS;
  }
  board_gpio_known[pin] = 1;
  board_gpio_levels[pin] = level ? 1 : 0;
#endif

  return frothy_ffi_return_nil(out);
}

static froth_error_t board_gpio_read_cb(frothy_runtime_t *runtime,
                                        const void *context,
                                        const frothy_value_t *args,
                                        size_t arg_count, frothy_value_t *out) {
  int32_t pin = 0;
  int32_t level = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  if (!board_gpio_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

#ifdef ESP_PLATFORM
  if (board_gpio_output_shadow_valid[pin]) {
    level = board_gpio_output_shadow_levels[pin];
  } else {
    level = gpio_get_level((gpio_num_t)pin) ? 1 : 0;
  }
#else
  if (pin >= BOARD_POSIX_GPIO_MAX) {
    return FROTH_ERROR_BOUNDS;
  }
  if (!board_gpio_known[pin]) {
    board_gpio_known[pin] = 1;
    board_gpio_levels[pin] = 0;
  }
  level = board_gpio_levels[pin];
#endif

  return frothy_ffi_return_int(level, out);
}

static froth_error_t board_ms_cb(frothy_runtime_t *runtime, const void *context,
                                 const frothy_value_t *args, size_t arg_count,
                                 frothy_value_t *out) {
  int32_t delay_ms = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  FROTH_TRY(frothy_ffi_expect_int(args, 0, &delay_ms));
  if (delay_ms > 0) {
    FROTH_TRY(board_delay_interruptible_ms(&froth_vm, delay_ms));
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_millis_cb(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  return frothy_ffi_return_int(
      (int32_t)frothy_ffi_wrap_uptime_ms(platform_uptime_ms()), out);
}

static froth_error_t board_adc_read_cb(frothy_runtime_t *runtime,
                                       const void *context,
                                       const frothy_value_t *args,
                                       size_t arg_count,
                                       frothy_value_t *out) {
  int32_t pin = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  if (!board_adc_pin_valid(pin)) {
    return FROTH_ERROR_BOUNDS;
  }

#ifdef ESP_PLATFORM
  {
    adc1_channel_t channel;
    int sample = 0;

    if (!board_adc1_channel_for_pin(pin, &channel)) {
      return FROTH_ERROR_BOUNDS;
    }
    if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) {
      return FROTH_ERROR_IO;
    }
    if (adc1_config_channel_atten(channel, BOARD_ADC_ATTEN) != ESP_OK) {
      return FROTH_ERROR_IO;
    }
    sample = adc1_get_raw(channel);
    if (sample < 0) {
      return FROTH_ERROR_IO;
    }
    return frothy_ffi_return_int(sample, out);
  }
#else
  return frothy_ffi_return_int(2048 + (pin & 0xff), out);
#endif
}

static froth_error_t board_random_seed_cb(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  int32_t seed = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &seed));
  board_random_state = frothy_ffi_random_seed((uint32_t)seed);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_random_seed_from_millis_cb(
    frothy_runtime_t *runtime, const void *context, const frothy_value_t *args,
    size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  board_random_state = frothy_ffi_random_seed(platform_uptime_ms());
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_random_next_cb(frothy_runtime_t *runtime,
                                          const void *context,
                                          const frothy_value_t *args,
                                          size_t arg_count,
                                          frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  return frothy_ffi_return_int(frothy_ffi_random_next_int(&board_random_state),
                               out);
}

static froth_error_t board_random_below_cb(frothy_runtime_t *runtime,
                                           const void *context,
                                           const frothy_value_t *args,
                                           size_t arg_count,
                                           frothy_value_t *out) {
  int32_t limit = 0;
  uint32_t value = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &limit));
  if (limit <= 0) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_TRY(
      frothy_ffi_random_below(&board_random_state, (uint32_t)limit, &value));
  return frothy_ffi_return_int((int32_t)value, out);
}

static froth_error_t board_random_range_cb(frothy_runtime_t *runtime,
                                           const void *context,
                                           const frothy_value_t *args,
                                           size_t arg_count,
                                           frothy_value_t *out) {
  int32_t lo = 0;
  int32_t hi = 0;
  int64_t span = 0;
  uint32_t offset = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &lo));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &hi));
  if (lo > hi) {
    int32_t tmp = lo;
    lo = hi;
    hi = tmp;
  }

  span = (int64_t)hi - (int64_t)lo + 1;
  if (span <= 0) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_TRY(
      frothy_ffi_random_below(&board_random_state, (uint32_t)span, &offset));
  return frothy_ffi_return_int((int32_t)((int64_t)lo + (int64_t)offset), out);
}

static froth_error_t board_tm1629_raw_init_cb(frothy_runtime_t *runtime,
                                              const void *context,
                                              const frothy_value_t *args,
                                              size_t arg_count,
                                              frothy_value_t *out) {
  int32_t stb_pin = 0;
  int32_t clk_pin = 0;
  int32_t dio_pin = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &stb_pin));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &clk_pin));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &dio_pin));
  if (!board_gpio_pin_valid(stb_pin) || !board_gpio_pin_valid(clk_pin) ||
      !board_gpio_pin_valid(dio_pin) ||
      !board_tm1629_pins_distinct(stb_pin, clk_pin, dio_pin)) {
    return FROTH_ERROR_BOUNDS;
  }
  if (!frothy_tm1629_configure(&board_tm1629, stb_pin, clk_pin, dio_pin)) {
    return FROTH_ERROR_IO;
  }
  return frothy_ffi_return_nil(out);
}

static froth_error_t
board_tm1629_raw_brightness_cb(frothy_runtime_t *runtime, const void *context,
                               const frothy_value_t *args, size_t arg_count,
                               frothy_value_t *out) {
  int32_t level = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &level));
  frothy_tm1629_set_brightness(&board_tm1629, level);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_show_cb(frothy_runtime_t *runtime,
                                              const void *context,
                                              const frothy_value_t *args,
                                              size_t arg_count,
                                              frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_show(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_clear_cb(frothy_runtime_t *runtime,
                                               const void *context,
                                               const frothy_value_t *args,
                                               size_t arg_count,
                                               frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_clear(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_fill_cb(frothy_runtime_t *runtime,
                                              const void *context,
                                              const frothy_value_t *args,
                                              size_t arg_count,
                                              frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_fill(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_row_get_cb(frothy_runtime_t *runtime,
                                                 const void *context,
                                                 const frothy_value_t *args,
                                                 size_t arg_count,
                                                 frothy_value_t *out) {
  int32_t row = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &row));
  return frothy_ffi_return_int((int32_t)frothy_tm1629_row_get(&board_tm1629, row),
                               out);
}

static froth_error_t board_tm1629_raw_row_set_cb(frothy_runtime_t *runtime,
                                                 const void *context,
                                                 const frothy_value_t *args,
                                                 size_t arg_count,
                                                 frothy_value_t *out) {
  int32_t mask = 0;
  int32_t row = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &mask));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &row));
  frothy_tm1629_row_set(&board_tm1629, row, mask);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_next_get_cb(frothy_runtime_t *runtime,
                                                  const void *context,
                                                  const frothy_value_t *args,
                                                  size_t arg_count,
                                                  frothy_value_t *out) {
  int32_t row = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &row));
  return frothy_ffi_return_int(
      (int32_t)frothy_tm1629_next_get(&board_tm1629, row), out);
}

static froth_error_t board_tm1629_raw_next_set_cb(frothy_runtime_t *runtime,
                                                  const void *context,
                                                  const frothy_value_t *args,
                                                  size_t arg_count,
                                                  frothy_value_t *out) {
  int32_t mask = 0;
  int32_t row = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &mask));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &row));
  frothy_tm1629_next_set(&board_tm1629, row, mask);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_next_clear_cb(frothy_runtime_t *runtime,
                                                    const void *context,
                                                    const frothy_value_t *args,
                                                    size_t arg_count,
                                                    frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_next_clear(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_commit_next_cb(
    frothy_runtime_t *runtime, const void *context, const frothy_value_t *args,
    size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_commit_next(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_pixel_get_cb(frothy_runtime_t *runtime,
                                                   const void *context,
                                                   const frothy_value_t *args,
                                                   size_t arg_count,
                                                   frothy_value_t *out) {
  int32_t x = 0;
  int32_t y = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &x));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &y));
  return frothy_ffi_return_bool(frothy_tm1629_pixel_get(&board_tm1629, x, y),
                                out);
}

static froth_error_t board_tm1629_raw_pixel_set_cb(frothy_runtime_t *runtime,
                                                   const void *context,
                                                   const frothy_value_t *args,
                                                   size_t arg_count,
                                                   frothy_value_t *out) {
  int32_t x = 0;
  int32_t y = 0;
  bool flag = false;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &x));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &y));
  FROTH_TRY(frothy_ffi_expect_bool(args, 2, &flag));
  frothy_tm1629_pixel_set(&board_tm1629, x, y, flag);
  return frothy_ffi_return_nil(out);
}

static froth_error_t
board_tm1629_raw_next_pixel_set_cb(frothy_runtime_t *runtime,
                                   const void *context,
                                   const frothy_value_t *args,
                                   size_t arg_count, frothy_value_t *out) {
  int32_t x = 0;
  int32_t y = 0;
  bool flag = false;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &x));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &y));
  FROTH_TRY(frothy_ffi_expect_bool(args, 2, &flag));
  frothy_tm1629_next_pixel_set(&board_tm1629, x, y, flag);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_invert_cb(frothy_runtime_t *runtime,
                                                const void *context,
                                                const frothy_value_t *args,
                                                size_t arg_count,
                                                frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_invert(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_shift_left_cb(frothy_runtime_t *runtime,
                                                    const void *context,
                                                    const frothy_value_t *args,
                                                    size_t arg_count,
                                                    frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_shift_left(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_shift_right_cb(
    frothy_runtime_t *runtime, const void *context, const frothy_value_t *args,
    size_t arg_count, frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_shift_right(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_shift_up_cb(frothy_runtime_t *runtime,
                                                  const void *context,
                                                  const frothy_value_t *args,
                                                  size_t arg_count,
                                                  frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_shift_up(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_shift_down_cb(frothy_runtime_t *runtime,
                                                    const void *context,
                                                    const frothy_value_t *args,
                                                    size_t arg_count,
                                                    frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  (void)arg_count;

  board_ensure_runtime_state();
  frothy_tm1629_shift_down(&board_tm1629);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_line_cb(frothy_runtime_t *runtime,
                                              const void *context,
                                              const frothy_value_t *args,
                                              size_t arg_count,
                                              frothy_value_t *out) {
  int32_t x0 = 0;
  int32_t y0 = 0;
  int32_t x1 = 0;
  int32_t y1 = 0;
  bool flag = false;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &x0));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &y0));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &x1));
  FROTH_TRY(frothy_ffi_expect_int(args, 3, &y1));
  FROTH_TRY(frothy_ffi_expect_bool(args, 4, &flag));
  frothy_tm1629_line(&board_tm1629, x0, y0, x1, y1, flag);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_rect_cb(frothy_runtime_t *runtime,
                                              const void *context,
                                              const frothy_value_t *args,
                                              size_t arg_count,
                                              frothy_value_t *out) {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
  bool flag = false;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &x));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &y));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &width));
  FROTH_TRY(frothy_ffi_expect_int(args, 3, &height));
  FROTH_TRY(frothy_ffi_expect_bool(args, 4, &flag));
  frothy_tm1629_rect(&board_tm1629, x, y, width, height, flag);
  return frothy_ffi_return_nil(out);
}

static froth_error_t board_tm1629_raw_fill_rect_cb(frothy_runtime_t *runtime,
                                                   const void *context,
                                                   const frothy_value_t *args,
                                                   size_t arg_count,
                                                   frothy_value_t *out) {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
  bool flag = false;

  (void)runtime;
  (void)context;
  (void)arg_count;

  board_ensure_runtime_state();
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &x));
  FROTH_TRY(frothy_ffi_expect_int(args, 1, &y));
  FROTH_TRY(frothy_ffi_expect_int(args, 2, &width));
  FROTH_TRY(frothy_ffi_expect_int(args, 3, &height));
  FROTH_TRY(frothy_ffi_expect_bool(args, 4, &flag));
  frothy_tm1629_fill_rect(&board_tm1629, x, y, width, height, flag);
  return frothy_ffi_return_nil(out);
}

static const frothy_ffi_param_t board_pin_mode_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
    FROTHY_FFI_PARAM_INT("mode"),
};

static const frothy_ffi_param_t board_pin_level_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
    FROTHY_FFI_PARAM_INT("level"),
};

static const frothy_ffi_param_t board_pin_params[] = {
    FROTHY_FFI_PARAM_INT("pin"),
};

static const frothy_ffi_param_t board_delay_params[] = {
    FROTHY_FFI_PARAM_INT("ms"),
};

static const frothy_ffi_param_t board_random_seed_params[] = {
    FROTHY_FFI_PARAM_INT("seed"),
};

static const frothy_ffi_param_t board_random_below_params[] = {
    FROTHY_FFI_PARAM_INT("limit"),
};

static const frothy_ffi_param_t board_random_range_params[] = {
    FROTHY_FFI_PARAM_INT("lo"),
    FROTHY_FFI_PARAM_INT("hi"),
};

static const frothy_ffi_param_t board_tm1629_init_params[] = {
    FROTHY_FFI_PARAM_INT("stb"),
    FROTHY_FFI_PARAM_INT("clk"),
    FROTHY_FFI_PARAM_INT("dio"),
};

static const frothy_ffi_param_t board_tm1629_level_params[] = {
    FROTHY_FFI_PARAM_INT("level"),
};

static const frothy_ffi_param_t board_tm1629_row_params[] = {
    FROTHY_FFI_PARAM_INT("y"),
};

static const frothy_ffi_param_t board_tm1629_mask_row_params[] = {
    FROTHY_FFI_PARAM_INT("mask"),
    FROTHY_FFI_PARAM_INT("y"),
};

static const frothy_ffi_param_t board_tm1629_xy_params[] = {
    FROTHY_FFI_PARAM_INT("x"),
    FROTHY_FFI_PARAM_INT("y"),
};

static const frothy_ffi_param_t board_tm1629_xy_flag_params[] = {
    FROTHY_FFI_PARAM_INT("x"),
    FROTHY_FFI_PARAM_INT("y"),
    FROTHY_FFI_PARAM_BOOL("flag"),
};

static const frothy_ffi_param_t board_tm1629_line_params[] = {
    FROTHY_FFI_PARAM_INT("x0"),
    FROTHY_FFI_PARAM_INT("y0"),
    FROTHY_FFI_PARAM_INT("x1"),
    FROTHY_FFI_PARAM_INT("y1"),
    FROTHY_FFI_PARAM_BOOL("flag"),
};

static const frothy_ffi_param_t board_tm1629_rect_params[] = {
    FROTHY_FFI_PARAM_INT("x"),
    FROTHY_FFI_PARAM_INT("y"),
    FROTHY_FFI_PARAM_INT("width"),
    FROTHY_FFI_PARAM_INT("height"),
    FROTHY_FFI_PARAM_BOOL("flag"),
};

#define BOARD_ENTRY(name_text, params_value, param_count_value, arity_value,   \
                    result_value, help_text, callback_value, effect_text)      \
  {                                                                            \
      .name = name_text,                                                       \
      .params = params_value,                                                  \
      .param_count = param_count_value,                                        \
      .arity = arity_value,                                                    \
      .result_type = result_value,                                             \
      .help = help_text,                                                       \
      .flags = FROTHY_FFI_FLAG_NONE,                                           \
      .callback = callback_value,                                              \
      .context = NULL,                                                         \
      .stack_effect = effect_text,                                             \
  }

const frothy_ffi_entry_t frothy_board_bindings[] = {
    BOARD_ENTRY("gpio.mode", board_pin_mode_params,
                FROTHY_FFI_PARAM_COUNT(board_pin_mode_params), 2,
                FROTHY_FFI_VALUE_NIL, "Set pin mode (1=output).",
                board_gpio_mode_cb,
                "( pin mode -- )"),
    BOARD_ENTRY("gpio.write", board_pin_level_params,
                FROTHY_FFI_PARAM_COUNT(board_pin_level_params), 2,
                FROTHY_FFI_VALUE_NIL, "Set pin level (1=high).",
                board_gpio_write_cb,
                "( pin level -- )"),
    BOARD_ENTRY("gpio.read", board_pin_params,
                FROTHY_FFI_PARAM_COUNT(board_pin_params), 1,
                FROTHY_FFI_VALUE_INT, "Read the current GPIO level.",
                board_gpio_read_cb,
                "( pin -- level )"),
    BOARD_ENTRY("ms", board_delay_params,
                FROTHY_FFI_PARAM_COUNT(board_delay_params), 1,
                FROTHY_FFI_VALUE_NIL,
                "Sleep for a given number of milliseconds.", board_ms_cb,
                "( ms -- )"),
    BOARD_ENTRY("millis", NULL, 0, 0, FROTHY_FFI_VALUE_INT,
                "Return wrapped monotonic uptime in milliseconds.",
                board_millis_cb, "( -- n )"),
    BOARD_ENTRY("adc.read", board_pin_params,
                FROTHY_FFI_PARAM_COUNT(board_pin_params), 1,
                FROTHY_FFI_VALUE_INT, "Read a 12-bit ADC sample from a pin.",
                board_adc_read_cb,
                "( pin -- value )"),
    BOARD_ENTRY("random.seed!", board_random_seed_params,
                FROTHY_FFI_PARAM_COUNT(board_random_seed_params), 1,
                FROTHY_FFI_VALUE_NIL,
                "Seed the board pseudo-random generator.",
                board_random_seed_cb, "( seed -- )"),
    BOARD_ENTRY("random.seedFromMillis!", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Seed the board pseudo-random generator from millis.",
                board_random_seed_from_millis_cb, "( -- )"),
    BOARD_ENTRY("random.next", NULL, 0, 0, FROTHY_FFI_VALUE_INT,
                "Return the next non-negative pseudo-random integer.",
                board_random_next_cb, "( -- n )"),
    BOARD_ENTRY("random.below", board_random_below_params,
                FROTHY_FFI_PARAM_COUNT(board_random_below_params), 1,
                FROTHY_FFI_VALUE_INT,
                "Return a pseudo-random integer in [0, limit).",
                board_random_below_cb, "( limit -- n )"),
    BOARD_ENTRY("random.range", board_random_range_params,
                FROTHY_FFI_PARAM_COUNT(board_random_range_params), 2,
                FROTHY_FFI_VALUE_INT,
                "Return a pseudo-random integer between lo and hi inclusive.",
                board_random_range_cb, "( lo hi -- n )"),
    BOARD_ENTRY("tm1629.raw.init", board_tm1629_init_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_init_params), 3,
                FROTHY_FFI_VALUE_NIL,
                "Configure TM1629 pins and prepare the driver.",
                board_tm1629_raw_init_cb, "( stb clk dio -- )"),
    BOARD_ENTRY("tm1629.raw.brightness!", board_tm1629_level_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_level_params), 1,
                FROTHY_FFI_VALUE_NIL, "Clamp and store brightness (0..7).",
                board_tm1629_raw_brightness_cb, "( level -- )"),
    BOARD_ENTRY("tm1629.raw.show", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Flush the current framebuffer to the TM1629.",
                board_tm1629_raw_show_cb, "( -- )"),
    BOARD_ENTRY("tm1629.raw.clear", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Clear the current framebuffer.", board_tm1629_raw_clear_cb,
                "( -- )"),
    BOARD_ENTRY("tm1629.raw.fill", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Fill the current framebuffer.", board_tm1629_raw_fill_cb,
                "( -- )"),
    BOARD_ENTRY("tm1629.raw.row@", board_tm1629_row_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_row_params), 1,
                FROTHY_FFI_VALUE_INT, "Fetch one current row mask.",
                board_tm1629_raw_row_get_cb, "( y -- mask )"),
    BOARD_ENTRY("tm1629.raw.row!", board_tm1629_mask_row_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_mask_row_params), 2,
                FROTHY_FFI_VALUE_NIL, "Store one current row mask.",
                board_tm1629_raw_row_set_cb, "( mask y -- )"),
    BOARD_ENTRY("tm1629.raw.next@", board_tm1629_row_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_row_params), 1,
                FROTHY_FFI_VALUE_INT, "Fetch one next-buffer row mask.",
                board_tm1629_raw_next_get_cb, "( y -- mask )"),
    BOARD_ENTRY("tm1629.raw.next!", board_tm1629_mask_row_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_mask_row_params), 2,
                FROTHY_FFI_VALUE_NIL, "Store one next-buffer row mask.",
                board_tm1629_raw_next_set_cb, "( mask y -- )"),
    BOARD_ENTRY("tm1629.raw.nextClear", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Clear the next framebuffer.", board_tm1629_raw_next_clear_cb,
                "( -- )"),
    BOARD_ENTRY("tm1629.raw.commitNext", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Copy the next framebuffer into the current framebuffer.",
                board_tm1629_raw_commit_next_cb, "( -- )"),
    BOARD_ENTRY("tm1629.raw.pixel@", board_tm1629_xy_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_xy_params), 2,
                FROTHY_FFI_VALUE_BOOL,
                "Read one pixel. Out-of-bounds reads return false.",
                board_tm1629_raw_pixel_get_cb, "( x y -- flag )"),
    BOARD_ENTRY("tm1629.raw.pixel!", board_tm1629_xy_flag_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_xy_flag_params), 3,
                FROTHY_FFI_VALUE_NIL,
                "Set or clear one pixel. Out-of-bounds writes are ignored.",
                board_tm1629_raw_pixel_set_cb, "( x y flag -- )"),
    BOARD_ENTRY("tm1629.raw.nextPixel!", board_tm1629_xy_flag_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_xy_flag_params), 3,
                FROTHY_FFI_VALUE_NIL,
                "Set or clear one pixel in the next framebuffer.",
                board_tm1629_raw_next_pixel_set_cb, "( x y flag -- )"),
    BOARD_ENTRY("tm1629.raw.invert", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Invert the current framebuffer.", board_tm1629_raw_invert_cb,
                "( -- )"),
    BOARD_ENTRY("tm1629.raw.shiftLeft", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Shift the framebuffer one column left.",
                board_tm1629_raw_shift_left_cb, "( -- )"),
    BOARD_ENTRY("tm1629.raw.shiftRight", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Shift the framebuffer one column right.",
                board_tm1629_raw_shift_right_cb, "( -- )"),
    BOARD_ENTRY("tm1629.raw.shiftUp", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Shift the framebuffer one row up.",
                board_tm1629_raw_shift_up_cb, "( -- )"),
    BOARD_ENTRY("tm1629.raw.shiftDown", NULL, 0, 0, FROTHY_FFI_VALUE_NIL,
                "Shift the framebuffer one row down.",
                board_tm1629_raw_shift_down_cb, "( -- )"),
    BOARD_ENTRY("tm1629.raw.line", board_tm1629_line_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_line_params), 5,
                FROTHY_FFI_VALUE_NIL, "Draw a clipped line.",
                board_tm1629_raw_line_cb, "( x0 y0 x1 y1 flag -- )"),
    BOARD_ENTRY("tm1629.raw.rect", board_tm1629_rect_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_rect_params), 5,
                FROTHY_FFI_VALUE_NIL, "Draw a clipped rectangle outline.",
                board_tm1629_raw_rect_cb, "( x y width height flag -- )"),
    BOARD_ENTRY("tm1629.raw.fillRect", board_tm1629_rect_params,
                FROTHY_FFI_PARAM_COUNT(board_tm1629_rect_params), 5,
                FROTHY_FFI_VALUE_NIL, "Draw a clipped filled rectangle.",
                board_tm1629_raw_fill_rect_cb, "( x y width height flag -- )"),
    {0},
};
