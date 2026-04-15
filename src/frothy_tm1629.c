#include "frothy_tm1629.h"

#include <string.h>

#define FROTHY_TM1629_CMD_DATA_AUTO UINT8_C(0x40)
#define FROTHY_TM1629_CMD_ADDR_BASE UINT8_C(0xC0)
#define FROTHY_TM1629_CMD_DISPLAY_ON_BASE UINT8_C(0x88)

static bool frothy_tm1629_row_valid(int32_t row) {
  return row >= 0 && row < FROTHY_TM1629_HEIGHT;
}

static bool frothy_tm1629_point_valid(int32_t x, int32_t y) {
  return x >= 0 && x < FROTHY_TM1629_WIDTH && y >= 0 && y < FROTHY_TM1629_HEIGHT;
}

static uint16_t frothy_tm1629_mask_row(int32_t mask) {
  return (uint16_t)mask & FROTHY_TM1629_ROW_MASK;
}

static uint8_t frothy_tm1629_clamp_brightness(int32_t level) {
  if (level < 0) {
    return 0;
  }
  if (level > 7) {
    return 7;
  }
  return (uint8_t)level;
}

static void frothy_tm1629_delay_tick(const frothy_tm1629_t *display) {
  if (display == NULL || display->hal.delay_us == NULL) {
    return;
  }

  display->hal.delay_us(display->hal.context, 1);
}

static void frothy_tm1629_pin_mode(const frothy_tm1629_t *display, int32_t pin,
                                   bool output) {
  if (display == NULL || display->hal.pin_mode == NULL) {
    return;
  }

  display->hal.pin_mode(display->hal.context, pin, output);
}

static void frothy_tm1629_pin_write(const frothy_tm1629_t *display, int32_t pin,
                                    bool high) {
  if (display == NULL || display->hal.pin_write == NULL) {
    return;
  }

  display->hal.pin_write(display->hal.context, pin, high);
}

static void frothy_tm1629_write_bit(const frothy_tm1629_t *display, bool bit) {
  frothy_tm1629_pin_write(display, display->clk_pin, false);
  frothy_tm1629_pin_write(display, display->dio_pin, bit);
  frothy_tm1629_delay_tick(display);
  frothy_tm1629_pin_write(display, display->clk_pin, true);
  frothy_tm1629_delay_tick(display);
}

static void frothy_tm1629_write_byte(const frothy_tm1629_t *display,
                                     uint8_t byte) {
  int bit_index;

  for (bit_index = 0; bit_index < 8; bit_index++) {
    frothy_tm1629_write_bit(display, (byte & 0x01u) != 0);
    byte >>= 1;
  }
}

static void frothy_tm1629_write_command(const frothy_tm1629_t *display,
                                        uint8_t command) {
  if (display == NULL || !display->configured) {
    return;
  }

  frothy_tm1629_pin_write(display, display->stb_pin, false);
  frothy_tm1629_write_byte(display, command);
  frothy_tm1629_pin_write(display, display->stb_pin, true);
  frothy_tm1629_delay_tick(display);
}

static void frothy_tm1629_apply_brightness(const frothy_tm1629_t *display) {
  if (display == NULL) {
    return;
  }

  frothy_tm1629_write_command(
      display, (uint8_t)(FROTHY_TM1629_CMD_DISPLAY_ON_BASE |
                         (display->brightness & 0x07u)));
}

static void frothy_tm1629_write_point(uint16_t *rows, int32_t x, int32_t y,
                                      bool on) {
  uint16_t bit;

  if (rows == NULL || !frothy_tm1629_point_valid(x, y)) {
    return;
  }

  bit = (uint16_t)(UINT16_C(1) << x);
  if (on) {
    rows[y] |= bit;
  } else {
    rows[y] &= (uint16_t)~bit;
  }
}

static bool frothy_tm1629_read_point(const uint16_t *rows, int32_t x,
                                     int32_t y) {
  if (rows == NULL || !frothy_tm1629_point_valid(x, y)) {
    return false;
  }

  return (rows[y] & (uint16_t)(UINT16_C(1) << x)) != 0;
}

static void frothy_tm1629_draw_line(uint16_t *rows, int32_t x0, int32_t y0,
                                    int32_t x1, int32_t y1, bool on) {
  int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int32_t sx = x0 < x1 ? 1 : -1;
  int32_t dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1);
  int32_t sy = y0 < y1 ? 1 : -1;
  int32_t err = dx + dy;

  while (1) {
    frothy_tm1629_write_point(rows, x0, y0, on);
    if (x0 == x1 && y0 == y1) {
      break;
    }

    if ((err << 1) >= dy) {
      err += dy;
      x0 += sx;
    }
    if ((err << 1) <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void frothy_tm1629_draw_rect(uint16_t *rows, int32_t x, int32_t y,
                                    int32_t width, int32_t height, bool on) {
  if (rows == NULL || width <= 0 || height <= 0) {
    return;
  }

  if (width == 1) {
    frothy_tm1629_draw_line(rows, x, y, x, y + height - 1, on);
    return;
  }
  if (height == 1) {
    frothy_tm1629_draw_line(rows, x, y, x + width - 1, y, on);
    return;
  }

  frothy_tm1629_draw_line(rows, x, y, x + width - 1, y, on);
  frothy_tm1629_draw_line(rows, x, y + height - 1, x + width - 1,
                          y + height - 1, on);
  frothy_tm1629_draw_line(rows, x, y, x, y + height - 1, on);
  frothy_tm1629_draw_line(rows, x + width - 1, y, x + width - 1,
                          y + height - 1, on);
}

static void frothy_tm1629_draw_fill_rect(uint16_t *rows, int32_t x, int32_t y,
                                         int32_t width, int32_t height,
                                         bool on) {
  int32_t yy;
  int32_t xx;

  if (rows == NULL || width <= 0 || height <= 0) {
    return;
  }

  for (yy = y; yy < y + height; yy++) {
    for (xx = x; xx < x + width; xx++) {
      frothy_tm1629_write_point(rows, xx, yy, on);
    }
  }
}

void frothy_tm1629_init(frothy_tm1629_t *display,
                        const frothy_tm1629_hal_t *hal) {
  if (display == NULL) {
    return;
  }

  memset(display, 0, sizeof(*display));
  display->brightness = FROTHY_TM1629_BRIGHTNESS_DEFAULT;
  if (hal != NULL) {
    display->hal = *hal;
  }
}

void frothy_tm1629_reset(frothy_tm1629_t *display) {
  frothy_tm1629_hal_t hal;

  if (display == NULL) {
    return;
  }

  hal = display->hal;
  memset(display, 0, sizeof(*display));
  display->hal = hal;
  display->brightness = FROTHY_TM1629_BRIGHTNESS_DEFAULT;
}

void frothy_tm1629_factory_reset(frothy_tm1629_t *display) {
  if (display == NULL) {
    return;
  }

  if (display->configured) {
    frothy_tm1629_clear(display);
    frothy_tm1629_next_clear(display);
    display->brightness = FROTHY_TM1629_BRIGHTNESS_DEFAULT;
    frothy_tm1629_show(display);
  }
  frothy_tm1629_reset(display);
}

void frothy_tm1629_configure(frothy_tm1629_t *display, int32_t stb_pin,
                             int32_t clk_pin, int32_t dio_pin) {
  if (display == NULL) {
    return;
  }

  display->stb_pin = stb_pin;
  display->clk_pin = clk_pin;
  display->dio_pin = dio_pin;
  display->configured = true;

  frothy_tm1629_pin_mode(display, stb_pin, true);
  frothy_tm1629_pin_mode(display, clk_pin, true);
  frothy_tm1629_pin_mode(display, dio_pin, true);

  frothy_tm1629_pin_write(display, stb_pin, true);
  frothy_tm1629_pin_write(display, clk_pin, true);
  frothy_tm1629_pin_write(display, dio_pin, false);
  frothy_tm1629_apply_brightness(display);
}

void frothy_tm1629_set_brightness(frothy_tm1629_t *display, int32_t level) {
  if (display == NULL) {
    return;
  }

  display->brightness = frothy_tm1629_clamp_brightness(level);
  frothy_tm1629_apply_brightness(display);
}

void frothy_tm1629_show(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL || !display->configured) {
    return;
  }

  frothy_tm1629_write_command(display, FROTHY_TM1629_CMD_DATA_AUTO);
  frothy_tm1629_pin_write(display, display->stb_pin, false);
  frothy_tm1629_write_byte(display, FROTHY_TM1629_CMD_ADDR_BASE);

  for (row = 0; row < FROTHY_TM1629_HEIGHT; row++) {
    uint16_t mask = display->rows[row] & FROTHY_TM1629_ROW_MASK;

    frothy_tm1629_write_byte(display, (uint8_t)(mask & 0x00FFu));
    frothy_tm1629_write_byte(display, (uint8_t)((mask >> 8) & 0x000Fu));
  }

  frothy_tm1629_pin_write(display, display->stb_pin, true);
  frothy_tm1629_delay_tick(display);
  frothy_tm1629_apply_brightness(display);
}

void frothy_tm1629_clear(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL) {
    return;
  }

  for (row = 0; row < FROTHY_TM1629_HEIGHT; row++) {
    display->rows[row] = 0;
  }
}

void frothy_tm1629_fill(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL) {
    return;
  }

  for (row = 0; row < FROTHY_TM1629_HEIGHT; row++) {
    display->rows[row] = FROTHY_TM1629_ROW_MASK;
  }
}

uint16_t frothy_tm1629_row_get(const frothy_tm1629_t *display, int32_t row) {
  if (display == NULL || !frothy_tm1629_row_valid(row)) {
    return 0;
  }

  return display->rows[row] & FROTHY_TM1629_ROW_MASK;
}

void frothy_tm1629_row_set(frothy_tm1629_t *display, int32_t row, int32_t mask) {
  if (display == NULL || !frothy_tm1629_row_valid(row)) {
    return;
  }

  display->rows[row] = frothy_tm1629_mask_row(mask);
}

uint16_t frothy_tm1629_next_get(const frothy_tm1629_t *display, int32_t row) {
  if (display == NULL || !frothy_tm1629_row_valid(row)) {
    return 0;
  }

  return display->next_rows[row] & FROTHY_TM1629_ROW_MASK;
}

void frothy_tm1629_next_set(frothy_tm1629_t *display, int32_t row, int32_t mask) {
  if (display == NULL || !frothy_tm1629_row_valid(row)) {
    return;
  }

  display->next_rows[row] = frothy_tm1629_mask_row(mask);
}

void frothy_tm1629_next_clear(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL) {
    return;
  }

  for (row = 0; row < FROTHY_TM1629_HEIGHT; row++) {
    display->next_rows[row] = 0;
  }
}

void frothy_tm1629_commit_next(frothy_tm1629_t *display) {
  if (display == NULL) {
    return;
  }

  memcpy(display->rows, display->next_rows, sizeof(display->rows));
}

bool frothy_tm1629_pixel_get(const frothy_tm1629_t *display, int32_t x,
                             int32_t y) {
  if (display == NULL) {
    return false;
  }

  return frothy_tm1629_read_point(display->rows, x, y);
}

void frothy_tm1629_pixel_set(frothy_tm1629_t *display, int32_t x, int32_t y,
                             bool on) {
  if (display == NULL) {
    return;
  }

  frothy_tm1629_write_point(display->rows, x, y, on);
}

void frothy_tm1629_next_pixel_set(frothy_tm1629_t *display, int32_t x,
                                  int32_t y, bool on) {
  if (display == NULL) {
    return;
  }

  frothy_tm1629_write_point(display->next_rows, x, y, on);
}

void frothy_tm1629_invert(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL) {
    return;
  }

  for (row = 0; row < FROTHY_TM1629_HEIGHT; row++) {
    display->rows[row] ^= FROTHY_TM1629_ROW_MASK;
  }
}

void frothy_tm1629_shift_left(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL) {
    return;
  }

  for (row = 0; row < FROTHY_TM1629_HEIGHT; row++) {
    display->rows[row] =
        frothy_tm1629_mask_row((int32_t)(display->rows[row] << 1));
  }
}

void frothy_tm1629_shift_right(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL) {
    return;
  }

  for (row = 0; row < FROTHY_TM1629_HEIGHT; row++) {
    display->rows[row] >>= 1;
  }
}

void frothy_tm1629_shift_up(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL) {
    return;
  }

  for (row = 0; row < FROTHY_TM1629_HEIGHT - 1; row++) {
    display->rows[row] = display->rows[row + 1];
  }
  display->rows[FROTHY_TM1629_HEIGHT - 1] = 0;
}

void frothy_tm1629_shift_down(frothy_tm1629_t *display) {
  int32_t row;

  if (display == NULL) {
    return;
  }

  for (row = FROTHY_TM1629_HEIGHT - 1; row > 0; row--) {
    display->rows[row] = display->rows[row - 1];
  }
  display->rows[0] = 0;
}

void frothy_tm1629_line(frothy_tm1629_t *display, int32_t x0, int32_t y0,
                        int32_t x1, int32_t y1, bool on) {
  if (display == NULL) {
    return;
  }

  frothy_tm1629_draw_line(display->rows, x0, y0, x1, y1, on);
}

void frothy_tm1629_rect(frothy_tm1629_t *display, int32_t x, int32_t y,
                        int32_t width, int32_t height, bool on) {
  if (display == NULL) {
    return;
  }

  frothy_tm1629_draw_rect(display->rows, x, y, width, height, on);
}

void frothy_tm1629_fill_rect(frothy_tm1629_t *display, int32_t x, int32_t y,
                             int32_t width, int32_t height, bool on) {
  if (display == NULL) {
    return;
  }

  frothy_tm1629_draw_fill_rect(display->rows, x, y, width, height, on);
}
