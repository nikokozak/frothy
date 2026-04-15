#pragma once

#include "froth_ffi.h"

#define FROTHY_BOARD_FFI_USE_LEGACY_EXPORT

void froth_board_reset_runtime_state(void);

FROTH_BOARD_DECLARE(froth_board_bindings);
