# ADR-053: First Board-Level I2C/UART Integration Surface

**Date**: 2026-04-02
**Status**: Accepted
**Related ADRs**: ADR-028 (board/platform architecture), ADR-044 (project system), ADR-048 (exclusive live transport)

## Context

The ESP32 board already exposes raw I2C bindings, and the workshop queue now
calls for "finish I2C/UART FFI/library integration" before the deferred
hardware-validation pass.

Two practical gaps remained:

- UART had no board-level Froth surface at all.
- I2C existed only as raw handle-oriented words, with no settled board-lib
  convenience layer or host-validation story.

Because the default development path is still the POSIX board, the integration
tranche also needs a host-visible validation surface. Otherwise the only proof
would be deferred hardware smoke tests.

## Decision

### 1. Standardize the first raw UART board words

This tranche introduces three board-level UART words:

- `uart.init ( tx rx baud -- uart )`
- `uart.write ( byte uart -- )`
- `uart.read ( uart -- byte )`

`uart.init` returns a small integer handle. The handle is then passed to
`uart.write` and `uart.read`.

This is intentionally minimal. There is no `uart.key?`, `uart.deinit`, or
string-oriented primitive in this tranche.

### 2. Keep ESP32 user UART off the console transport

On ESP32 DevKit V1, the console/live session remains on UART0. The user-facing
UART surface uses only secondary UARTs (UART1/UART2) so it does not compete
with the REPL/transport path defined by ADR-048.

### 3. Add deterministic POSIX stubs for validation

The POSIX board gains stub I2C/UART bindings with the same word names as the
ESP32 board surface.

These stubs exist for host-side testing and demo authoring:

- I2C reads return deterministic values derived from the configured device
  address and register.
- UART writes emit bytes to the host console.
- UART reads return deterministic bytes from a small fixed sequence.

This keeps the default host build useful for regression coverage without
pretending that POSIX is real hardware.

### 4. Settle the first board-lib convenience surface

Board libs provide a thin convenience layer on top of the raw words:

- `i2c.setup ( -- bus )`
- `i2c.setup-fast ( -- bus )`
- `i2c.device ( bus addr -- device )`
- `i2c.device-fast ( bus addr -- device )`
- `uart.setup ( baud -- uart )`
- `uart.type ( s uart -- )`

The point is not to hide the raw handle API completely. The point is to give
projects a small default path that works out of the box with board pin
constants and ordinary string output.

### 5. Update board metadata to match the surface

`board.json` for the POSIX and ESP32 boards now advertises `i2c` and `uart`
peripherals and exposes default pin constants used by the board libs:

- `SDA`
- `SCL`
- `UART_TX`
- `UART_RX`

## Consequences

- The host board can now validate the same high-level I2C/UART vocabulary that
  the ESP32 board exposes.
- Project code can use board-lib defaults instead of reconstructing bus/pin
  setup by hand.
- The project-facing board metadata is less misleading: it now reflects the
  actual available peripheral surface.

## Non-goals

- No generic platform-module refactor for I2C/UART in this tranche
- No runtime effect-checking or contracts for dynamic peripheral handles
- No `uart.key?` or buffered UART driver layer
- No hardware-validation claims beyond host regression coverage in this ADR
