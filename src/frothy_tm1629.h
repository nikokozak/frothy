#pragma once

#include "froth_types.h"

#include <stdbool.h>
#include <stdint.h>

#define FROTHY_TM1629_WIDTH 12
#define FROTHY_TM1629_HEIGHT 8
#define FROTHY_TM1629_ROW_MASK UINT16_C(0x0FFF)
#define FROTHY_TM1629_BRIGHTNESS_DEFAULT 1

typedef struct {
  void *context;
  bool (*pin_mode)(void *context, int32_t pin, bool output);
  void (*pin_write)(void *context, int32_t pin, bool high);
  void (*delay_us)(void *context, uint32_t usec);
} frothy_tm1629_hal_t;

typedef struct {
  frothy_tm1629_hal_t hal;
  int32_t stb_pin;
  int32_t clk_pin;
  int32_t dio_pin;
  uint16_t rows[FROTHY_TM1629_HEIGHT];
  uint16_t next_rows[FROTHY_TM1629_HEIGHT];
  uint8_t brightness;
  bool configured;
} frothy_tm1629_t;

void frothy_tm1629_init(frothy_tm1629_t *display,
                        const frothy_tm1629_hal_t *hal);
void frothy_tm1629_reset(frothy_tm1629_t *display);
void frothy_tm1629_factory_reset(frothy_tm1629_t *display);

bool frothy_tm1629_configure(frothy_tm1629_t *display, int32_t stb_pin,
                             int32_t clk_pin, int32_t dio_pin);
void frothy_tm1629_set_brightness(frothy_tm1629_t *display, int32_t level);
void frothy_tm1629_show(frothy_tm1629_t *display);

void frothy_tm1629_clear(frothy_tm1629_t *display);
void frothy_tm1629_fill(frothy_tm1629_t *display);
uint16_t frothy_tm1629_row_get(const frothy_tm1629_t *display, int32_t row);
void frothy_tm1629_row_set(frothy_tm1629_t *display, int32_t row, int32_t mask);
uint16_t frothy_tm1629_next_get(const frothy_tm1629_t *display, int32_t row);
void frothy_tm1629_next_set(frothy_tm1629_t *display, int32_t row, int32_t mask);
void frothy_tm1629_next_clear(frothy_tm1629_t *display);
void frothy_tm1629_commit_next(frothy_tm1629_t *display);

bool frothy_tm1629_pixel_get(const frothy_tm1629_t *display, int32_t x,
                             int32_t y);
void frothy_tm1629_pixel_set(frothy_tm1629_t *display, int32_t x, int32_t y,
                             bool on);
void frothy_tm1629_next_pixel_set(frothy_tm1629_t *display, int32_t x,
                                  int32_t y, bool on);

void frothy_tm1629_invert(frothy_tm1629_t *display);
void frothy_tm1629_shift_left(frothy_tm1629_t *display);
void frothy_tm1629_shift_right(frothy_tm1629_t *display);
void frothy_tm1629_shift_up(frothy_tm1629_t *display);
void frothy_tm1629_shift_down(frothy_tm1629_t *display);
void frothy_tm1629_line(frothy_tm1629_t *display, int32_t x0, int32_t y0,
                        int32_t x1, int32_t y1, bool on);
void frothy_tm1629_rect(frothy_tm1629_t *display, int32_t x, int32_t y,
                        int32_t width, int32_t height, bool on);
void frothy_tm1629_fill_rect(frothy_tm1629_t *display, int32_t x, int32_t y,
                             int32_t width, int32_t height, bool on);
