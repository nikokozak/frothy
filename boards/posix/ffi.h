#pragma once

#include "froth_ffi.h"

/* Retained legacy export until this board surface is ported to frothy_ffi. */
#define FROTHY_BOARD_FFI_USE_LEGACY_EXPORT

void froth_board_reset_runtime_state(void);

FROTH_BOARD_DECLARE(froth_board_bindings);
