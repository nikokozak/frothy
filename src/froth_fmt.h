#pragma once
#include "froth_types.h"
#include "platform.h"

/* Emit a null-terminated string through the platform layer. */
froth_error_t emit_string(const char* str);

/* Convert a cell-sized integer to a decimal string.
 * Returns a pointer to a static buffer (not reentrant). */
char* format_number(froth_cell_t number);
