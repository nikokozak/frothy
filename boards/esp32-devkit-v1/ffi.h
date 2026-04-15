#pragma once

#include "froth_ffi.h"

/* Retained legacy export until this board surface is ported to frothy_ffi. */
#define FROTHY_BOARD_FFI_USE_LEGACY_EXPORT

#define FROTH_BOARD_BOOT_BUTTON_PIN 0
#define FROTH_BOARD_CONSOLE_DEFAULT_PORT 0
#define FROTH_BOARD_CONSOLE_DEFAULT_TX_PIN 1
#define FROTH_BOARD_CONSOLE_DEFAULT_RX_PIN 3
#define FROTH_BOARD_CONSOLE_DEFAULT_BAUD 115200

void froth_board_reset_runtime_state(void);

FROTH_BOARD_DECLARE(froth_board_bindings);
