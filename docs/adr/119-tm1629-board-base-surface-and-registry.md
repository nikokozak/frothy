# Frothy ADR-119: TM1629 Board Base Surface And Base Registry

**Date**: 2026-04-14
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 4, 8, 10 and Appendix C
**Roadmap queue item**: `Workshop starter project and frozen board/game surface`
**Related ADRs**: `docs/adr/108-frothy-ffi-boundary.md`, `docs/adr/121-workshop-base-image-board-library-surface.md`

## Context

The workshop display path needs the original TM1629 matrix library on the
maintained Frothy board surface, not as a project-local Froth-era shim.

Three problems blocked that move:

- the old TM1629 path lived in one-off workshop runtime code instead of a
  maintained Frothy-owned runtime/library split
- board base-image seeding still depended on hard-coded slot-name lists and a
  `workshop.frothy` special case
- the frozen Frothy lexer could not express conventional board/library names
  such as `brightness!`, `row@`, or `ready?`

The workshop board also needs a real baked-in display surface that survives
`dangerous.wipe`, while keeping timing-critical TM1629 work in C and higher
level affordances in Frothy.

## Decision

Use a maintained Frothy-native TM1629 board stack.

### Board base installation

- board base libraries are loaded from `boards/<board>/lib/base.frothy`
- board pins, board FFI slots, and board-library definitions become base-owned
  by being installed during base-image seeding, not by matching hard-coded
  allowlists
- `frothy_base_image_install()` captures one base-slot registry from the
  actual installed base surface and `frothy_base_image_reset()` / `wipe()`
  restore from that registry

### TM1629 runtime split

- the timing-critical display path lives in shared maintained C runtime code:
  `src/frothy_tm1629.c`
- the board `ffi.c` is a thin maintained `frothy_ffi_entry_t` shim over that
  runtime
- the new maintained workshop board target is
  `esp32-devkit-v4-game-board`
- the baked-in TM1629 wiring is:
  `TM1629_STB=18`, `TM1629_CLK=19`, `TM1629_DIO=23`
- `froth_board_reset_runtime_state()` resets the TM1629 runtime so
  base-image reset and `dangerous.wipe` leave the display substrate in a known
  default state

### Public surface

Ship three layers:

- `tm1629.raw.*`: maintained C-backed primitives for framebuffer mutation and
  flush
- `tm1629.*`: canonical advanced Frothy display API
- `matrix.*`: small board-default teaching surface using the baked-in pins

### Name surface

Allow non-leading `!`, `@`, and `?` in Frothy names so maintained board/library
surfaces can expose conventional words such as:

- `tm1629.brightness!`
- `tm1629.row@`
- `tm1629.pixel!`
- `tm1629.plot@`
- `tm1629._rowBit?`

Leading `@name` keeps its existing slot-designator meaning.

### Board configuration

Board `board.json` metadata remains the maintained source of board build
defaults. The generic board-config path now seeds both the inherited Froth
substrate sizes and selected Frothy runtime capacities when a board needs
them, including the TM1629 board's larger Frothy payload arena.

Board-owned extra C runtime sources are also declared from `board.json`
`sources`, so the build and flash path pulls them from the selected board
declaration instead of product-wide hardcoded source lists.

## Held Boundary

This tranche does not port the rest of the old workshop runtime.

Specifically out of scope:

- `game.*`
- `button.*`
- `pot.*`
- `joy.*`
- workshop RNG/frame-pacing helpers beyond the maintained generic board words

## Consequences

- `dangerous.wipe` now restores the real board base surface instead of a
  manually curated name list.
- The `workshop.frothy` special case is gone from the maintained path.
- The TM1629 stack is now Frothy-owned and board-shaped rather than
  project-local legacy substrate.
- Host proof now includes both direct TM1629 runtime coverage and a POSIX
  board sub-build that exercises `tm1629.raw.*`, `tm1629.*`, `matrix.*`,
  wipe/reset behavior, and slot ownership.
