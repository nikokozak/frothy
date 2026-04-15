#pragma once

#include "froth_ffi.h"
#include "frothy_ffi.h"

/*
 * Temporary retained-substrate compatibility surface.
 *
 * New Frothy-owned board and project FFI code should export
 * `frothy_ffi_entry_t` tables and use the maintained declarations in
 * `frothy_ffi.h`. This header exists only for retained substrate tests and
 * compatibility shims that still exercise the legacy `froth_ffi_entry_t`
 * registration path.
 */
froth_error_t frothy_ffi_install_binding_table(const froth_ffi_entry_t *table);
