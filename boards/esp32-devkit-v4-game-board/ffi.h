#pragma once

#include "frothy_ffi.h"

#define FROTH_BOARD_BOOT_BUTTON_PIN 0
#define FROTH_BOARD_CONSOLE_DEFAULT_PORT 0
#define FROTH_BOARD_CONSOLE_DEFAULT_TX_PIN 1
#define FROTH_BOARD_CONSOLE_DEFAULT_RX_PIN 3
#define FROTH_BOARD_CONSOLE_DEFAULT_BAUD 115200

void froth_board_reset_runtime_state(void);

FROTHY_FFI_DECLARE(frothy_board_bindings);
