# Frothy Retained Substrate Manifest

Status: active reference
Date: 2026-04-14
Authority: `docs/reference/Froth_Substrate_References.md`, `CMakeLists.txt`, `targets/esp-idf/main/CMakeLists.txt`

This manifest names the retained Froth substrate and the temporary
compatibility seams that still ship in the maintained Frothy tree.

It exists to keep the boundary explicit:

- retained `froth_*` units are still live substrate, not accidental leftovers
- Frothy-owned runtime code remains the maintained product surface
- temporary compatibility shims stay visible and bounded until they can be
  deleted cleanly

## Retained Froth Substrate

These retained source files still build on maintained Frothy paths because the
current runtime still reuses them directly:

- `src/froth_console.c`
- `src/froth_stack.c`
- `src/froth_fmt.c`
- `src/froth_ffi.c`
- `src/froth_tbuf.c`
- `src/froth_vm.c`
- `src/froth_heap.c`
- `src/froth_cellspace.c`
- `src/froth_slot_table.c`
- `src/froth_snapshot.c`
- `src/froth_crc32.c`
- `src/froth_transport.c`

The corresponding retained public headers are:

- `src/froth_cellspace.h`
- `src/froth_console.h`
- `src/froth_crc32.h`
- `src/froth_ffi.h`
- `src/froth_fmt.h`
- `src/froth_heap.h`
- `src/froth_link.h`
- `src/froth_slot_table.h`
- `src/froth_snapshot.h`
- `src/froth_stack.h`
- `src/froth_tbuf.h`
- `src/froth_transport.h`
- `src/froth_types.h`
- `src/froth_vm.h`

## Frothy-Owned Runtime Surface

These are Frothy-owned runtime or board-facing sources on the maintained path:

- `src/frothy_ffi.c`
- `src/frothy_tm1629.c`
- `boards/<board>/ffi.c`
- the `src/frothy_*.c` language/runtime units listed in the host and ESP-IDF
  build targets

New board and project FFI code should prefer the maintained
`frothy_ffi_entry_t` / `frothy_project_bindings` path declared in
`src/frothy_ffi.h`.

## Temporary Compatibility Layer

These files are deliberate temporary compatibility seams, not product-center
runtime modules:

- `src/compat/frothy_console_compat.c`
- `src/compat/frothy_link_stub.c`
- `src/frothy_ffi_legacy.h`

These compatibility exports remain accepted only while retained substrate and
legacy board/project FFI paths still need them:

- `froth_ffi_entry_t` tables via `froth_board_bindings`
- `froth_ffi_entry_t` tables via `froth_project_bindings`
- `frothy_ffi_install_binding_table(...)`

New code should not add to that legacy surface.

## Current Legacy Holdouts

The maintained repo still carries these explicit legacy FFI exports:

- `boards/posix/ffi.h`
- `boards/esp32-devkit-v1/ffi.h`
- `tests/project_ffi/legacy_bindings.c`

They are retained only because the board/project migration is not complete
yet. They should shrink over time; they should not spread.
