# ADR-030: Platform interrupt polling and safe boot window

**Date**: 2026-03-13
**Status**: Accepted
**Spec sections**: Froth_Interactive_Development_v0_5.md, Interrupt semantics, Boot sequence

## Context

The Froth VM needs two interrupt-related mechanisms:

1. **Runtime interrupt**: stop runaway code (infinite loops, long computations) from the serial console. On POSIX, `SIGINT` sets the flag asynchronously. On bare-metal (ESP32), there is no signal mechanism, so the VM must poll the UART.

2. **Safe boot escape**: skip `restore` and `autorun` when a saved definition is broken (infinite loop, crash). Without this, a bad `autorun` bricks the device until reflash or `wipe` from a host tool.

The spec originally specified CAN (0x18) as the interrupt byte. The implementation uses ETX (0x03, Ctrl-C). This ADR resolves that mismatch and documents both mechanisms.

## Options Considered

### Option A: CAN (0x18, Ctrl-X)

Avoids collision with Link Mode's STX/ETX framing. Requires teaching users a non-standard key.

### Option B: ETX (0x03, Ctrl-C)

Universally understood. Works with `idf.py monitor` (passes through to device). Collides with Link Mode ETX framing, but Link Mode is not yet implemented and can use escape sequences or a different framing scheme when the time comes.

### Option C: UART BREAK condition

Hardware-level, no byte collision. Not portable across all serial adapters and terminal emulators. Hard to send from some tools.

## Decision

**Option B: ETX (0x03, Ctrl-C)** for both runtime interrupt and safe boot escape.

Deciding factors:
- Ctrl-C is the universally expected "stop" key. Zero explanation needed in a workshop.
- `idf.py monitor` passes 0x03 through to the device (confirmed on hardware).
- The Link Mode framing collision is a future problem with a future solution.

The spec (Froth_Interactive_Development_v0_5.md) is updated to say ETX where it previously said CAN.

### Runtime interrupt: `platform_check_interrupt`

Signature: `void platform_check_interrupt(struct froth_vm_t *vm)`

Called at executor safe points (once per cell in `froth_execute_quote`). If an interrupt byte is detected, sets `vm->interrupted = 1`. The executor checks the flag after each call and throws `ERR.INTERRUPT`.

Platform implementations:
- **POSIX**: no-op. `SIGINT` handler (`sigaction`, no `SA_RESTART`) sets `vm->interrupted` asynchronously. `fgetc` returns on signal, so the REPL escapes blocking reads.
- **ESP-IDF**: polls `platform_key_ready()`, reads byte via `fgetc`. If 0x03, sets flag. Otherwise, pushes byte back with `ungetc` so it's not lost.

### Safe boot window

Lives in `froth_boot.c`, after `platform_init` and stdlib load, before restore/autorun.

Sequence:
1. Print `boot: CTRL-C for safe boot`.
2. Poll for 750ms (75 iterations x 10ms `platform_delay_ms`). Discard all bytes.
3. If 0x03 seen (via `platform_key` byte check or `vm->interrupted` flag), set `safe_boot = true`.
4. If safe boot: skip restore and autorun, print confirmation, enter REPL.

The dual check (byte value + interrupt flag) handles both platforms: ESP32's `platform_key` intercepts 0x03 and sets `vm->interrupted` instead of writing the byte, while POSIX's `SIGINT` handler sets the flag asynchronously.

### `platform_delay_ms`

Added to the platform API: `void platform_delay_ms(froth_cell_u_t ms)`.

- **POSIX**: `usleep(ms * 1000)`
- **ESP-IDF**: `vTaskDelay(pdMS_TO_TICKS(ms))`

Used by the safe boot poll loop. Also available for future kernel timing needs.

### ESP32 UART hardening (related)

`platform_init` on ESP-IDF now includes:
- `uart_flush(UART_NUM_0)` after driver install to drain stale RX bytes.
- 50ms `vTaskDelay` settle window, then a second `uart_flush`.
- `platform_fatal` halts (`while(1){}`) instead of calling `esp_restart()`, preventing boot-failure reboot loops.

## Consequences

- Runtime interrupt works on both platforms with the same user gesture (Ctrl-C).
- Safe boot provides a recovery path from broken autorun definitions without reflash.
- `platform_delay_ms` is a new platform obligation. Trivial to implement on any target.
- The ETX choice will need revisiting if/when Link Mode is implemented (ETX is the frame delimiter). Likely resolution: escape sequences within Link Mode frames, or a different framing scheme.
- The safe boot window adds 750ms to every boot. Acceptable for a development/workshop device. Could be made opt-in via a CMake define for production builds.

## References

- Froth_Interactive_Development_v0_5.md, "Interrupt / Ctrl-C semantics" and "Boot sequence"
- ADR-020: interrupt flag via signal handler (original POSIX-only design)
- ESP32 DevKit V1 death spiral diagnosis (Mar 13 session): DTR-triggered resets + UART RX contamination
