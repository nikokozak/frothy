#include "frothy_tm1629.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PIN_STB 18
#define TEST_PIN_CLK 19
#define TEST_PIN_DIO 23
#define TEST_PIN_MAX 64

typedef struct {
  int32_t stb_pin;
  int32_t clk_pin;
  int32_t dio_pin;
  bool levels[TEST_PIN_MAX];
  uint8_t modes[TEST_PIN_MAX];
  uint8_t bytes[64];
  size_t byte_count;
  uint8_t current_byte;
  uint8_t bit_count;
} fake_tm1629_hal_t;

static void fake_hal_reset_capture(fake_tm1629_hal_t *hal) {
  hal->byte_count = 0;
  hal->current_byte = 0;
  hal->bit_count = 0;
}

static void fake_hal_pin_mode(void *context, int32_t pin, bool output) {
  fake_tm1629_hal_t *hal = (fake_tm1629_hal_t *)context;

  if (hal == NULL || pin < 0 || pin >= TEST_PIN_MAX) {
    return;
  }

  hal->modes[pin] = output ? 1u : 2u;
}

static void fake_hal_pin_write(void *context, int32_t pin, bool high) {
  fake_tm1629_hal_t *hal = (fake_tm1629_hal_t *)context;
  bool previous = false;

  if (hal == NULL || pin < 0 || pin >= TEST_PIN_MAX) {
    return;
  }

  previous = hal->levels[pin];
  hal->levels[pin] = high;
  if (pin != hal->clk_pin || previous || !high || hal->levels[hal->stb_pin]) {
    return;
  }

  if (hal->levels[hal->dio_pin]) {
    hal->current_byte |= (uint8_t)(1u << hal->bit_count);
  }
  hal->bit_count++;
  if (hal->bit_count == 8) {
    if (hal->byte_count < sizeof(hal->bytes)) {
      hal->bytes[hal->byte_count++] = hal->current_byte;
    }
    hal->current_byte = 0;
    hal->bit_count = 0;
  }
}

static void fake_hal_delay_us(void *context, uint32_t usec) {
  (void)context;
  (void)usec;
}

static int expect_true(bool value, const char *label) {
  if (!value) {
    fprintf(stderr, "%s expected true\n", label);
    return 0;
  }
  return 1;
}

static int expect_false(bool value, const char *label) {
  if (value) {
    fprintf(stderr, "%s expected false\n", label);
    return 0;
  }
  return 1;
}

static int expect_u8(uint8_t actual, uint8_t expected, const char *label) {
  if (actual != expected) {
    fprintf(stderr, "%s expected %u, got %u\n", label, (unsigned)expected,
            (unsigned)actual);
    return 0;
  }
  return 1;
}

static int expect_u16(uint16_t actual, uint16_t expected, const char *label) {
  if (actual != expected) {
    fprintf(stderr, "%s expected %u, got %u\n", label, (unsigned)expected,
            (unsigned)actual);
    return 0;
  }
  return 1;
}

static int expect_bytes(const fake_tm1629_hal_t *hal, const uint8_t *expected,
                        size_t expected_count, const char *label) {
  size_t i;

  if (hal->byte_count != expected_count) {
    fprintf(stderr, "%s expected %zu bytes, got %zu\n", label, expected_count,
            hal->byte_count);
    return 0;
  }

  for (i = 0; i < expected_count; i++) {
    if (hal->bytes[i] != expected[i]) {
      fprintf(stderr, "%s byte %zu expected 0x%02x, got 0x%02x\n", label, i,
              (unsigned)expected[i], (unsigned)hal->bytes[i]);
      return 0;
    }
  }

  return 1;
}

static void make_display(frothy_tm1629_t *display, fake_tm1629_hal_t *hal) {
  frothy_tm1629_hal_t hooks = {
      .context = hal,
      .pin_mode = fake_hal_pin_mode,
      .pin_write = fake_hal_pin_write,
      .delay_us = fake_hal_delay_us,
  };

  memset(hal, 0, sizeof(*hal));
  hal->stb_pin = TEST_PIN_STB;
  hal->clk_pin = TEST_PIN_CLK;
  hal->dio_pin = TEST_PIN_DIO;
  frothy_tm1629_init(display, &hooks);
}

static int test_configure_and_show_sequence(void) {
  frothy_tm1629_t display;
  fake_tm1629_hal_t hal;
  uint8_t expected[] = {
      0x40, 0xC0, 0x03, 0x00, 0x55, 0x0A, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8B,
  };
  int ok = 1;

  make_display(&display, &hal);
  ok &= expect_false(display.configured, "display starts unconfigured");
  ok &= expect_u8(display.brightness, FROTHY_TM1629_BRIGHTNESS_DEFAULT,
                  "default brightness");

  frothy_tm1629_configure(&display, TEST_PIN_STB, TEST_PIN_CLK, TEST_PIN_DIO);
  ok &= expect_true(display.configured, "display configured");
  ok &= expect_u8(hal.modes[TEST_PIN_STB], 1, "stb configured as output");
  ok &= expect_u8(hal.modes[TEST_PIN_CLK], 1, "clk configured as output");
  ok &= expect_u8(hal.modes[TEST_PIN_DIO], 1, "dio configured as output");

  frothy_tm1629_clear(&display);
  frothy_tm1629_row_set(&display, 0, 0x0003);
  frothy_tm1629_row_set(&display, 1, 0x0A55);
  frothy_tm1629_set_brightness(&display, 3);
  fake_hal_reset_capture(&hal);
  frothy_tm1629_show(&display);
  ok &= expect_bytes(&hal, expected, sizeof(expected), "show command stream");
  return ok;
}

static int test_raw_storage_and_bounds(void) {
  frothy_tm1629_t display;
  fake_tm1629_hal_t hal;
  int ok = 1;

  make_display(&display, &hal);
  frothy_tm1629_row_set(&display, 0, 0x1FFF);
  ok &= expect_u16(frothy_tm1629_row_get(&display, 0), 0x0FFF,
                   "row mask clamps to 12 bits");
  frothy_tm1629_row_set(&display, 99, 0x0007);
  ok &= expect_u16(frothy_tm1629_row_get(&display, 99), 0,
                   "oob row read returns zero");

  ok &= expect_false(frothy_tm1629_pixel_get(&display, -1, 0),
                     "oob pixel read returns false");
  ok &= expect_false(frothy_tm1629_pixel_get(&display, 12, 7),
                     "right-edge oob read returns false");
  frothy_tm1629_pixel_set(&display, 11, 7, true);
  ok &= expect_true(frothy_tm1629_pixel_get(&display, 11, 7),
                    "pixel set in bounds");
  frothy_tm1629_pixel_set(&display, 12, 7, true);
  ok &= expect_false(frothy_tm1629_pixel_get(&display, 12, 7),
                     "oob pixel write is ignored");

  frothy_tm1629_set_brightness(&display, -9);
  ok &= expect_u8(display.brightness, 0, "brightness clamps low");
  frothy_tm1629_set_brightness(&display, 99);
  ok &= expect_u8(display.brightness, 7, "brightness clamps high");
  return ok;
}

static int test_next_buffer_and_transforms(void) {
  frothy_tm1629_t display;
  fake_tm1629_hal_t hal;
  int ok = 1;

  make_display(&display, &hal);
  frothy_tm1629_next_set(&display, 2, 0x0011);
  frothy_tm1629_next_set(&display, 3, 0x0008);
  frothy_tm1629_commit_next(&display);
  ok &= expect_u16(frothy_tm1629_row_get(&display, 2), 0x0011,
                   "commit next copies row 2");
  ok &= expect_u16(frothy_tm1629_row_get(&display, 3), 0x0008,
                   "commit next copies row 3");

  frothy_tm1629_next_clear(&display);
  ok &= expect_u16(frothy_tm1629_next_get(&display, 2), 0,
                   "next clear resets next buffer");

  frothy_tm1629_shift_left(&display);
  ok &= expect_u16(frothy_tm1629_row_get(&display, 2), 0x0022,
                   "shift left updates row");
  frothy_tm1629_shift_right(&display);
  ok &= expect_u16(frothy_tm1629_row_get(&display, 2), 0x0011,
                   "shift right updates row");
  frothy_tm1629_shift_up(&display);
  ok &= expect_u16(frothy_tm1629_row_get(&display, 1), 0x0011,
                   "shift up moves rows");
  ok &= expect_u16(frothy_tm1629_row_get(&display, 7), 0,
                   "shift up clears last row");
  frothy_tm1629_shift_down(&display);
  ok &= expect_u16(frothy_tm1629_row_get(&display, 2), 0x0011,
                   "shift down restores row");
  ok &= expect_u16(frothy_tm1629_row_get(&display, 0), 0,
                   "shift down clears first row");

  frothy_tm1629_invert(&display);
  ok &= expect_u16(frothy_tm1629_row_get(&display, 2), 0x0FEE,
                   "invert flips active bits within row mask");
  return ok;
}

int main(void) {
  int ok = 1;

  ok &= test_configure_and_show_sequence();
  ok &= test_raw_storage_and_bounds();
  ok &= test_next_buffer_and_transforms();
  return ok ? 0 : 1;
}
