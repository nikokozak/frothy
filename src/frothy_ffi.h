#pragma once

#include "froth_ffi.h"
#include "froth_types.h"

#include <stdbool.h>

typedef struct {
  const char *name;
  froth_cell_t value;
} frothy_board_pin_t;

froth_error_t frothy_ffi_install_binding_table(const froth_ffi_entry_t *table);
froth_error_t frothy_ffi_install_pin_table(const frothy_board_pin_t *pins);
froth_error_t frothy_ffi_install_board_base_slots(void);
bool frothy_ffi_is_base_slot_name(const char *name);
froth_cell_t frothy_ffi_wrap_uptime_ms(uint32_t uptime_ms);
