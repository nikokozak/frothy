# ADR-020: Interrupt Flag via Signal Handler

**Date**: 2026-03-06
**Status**: Accepted
**Spec sections**: FROTH-Interactive v0.5, Section 3 (Interrupt / "Ctrl-C" semantics)

## Context

`while` loops and long-running quotation execution cannot currently be interrupted. The only escape is to kill the process. The spec requires that CAN (0x18) sets an interrupt flag, checked at safe points, which triggers `throw ERR.INTERRUPT`. This is a Demo Definition of Done item ("interrupt stops runaway loop").

The design must work on POSIX now and map cleanly to embedded targets (ESP32 UART interrupt, RP2040 GPIO/UART) later.

## Options Considered

### Option A: Non-blocking stdin peek at safe points

At each safe point, call `platform_key_ready` (which uses `poll`) to check if a byte is waiting, then read it and check for CAN.

**Pros**: No signal handler, stays within existing platform API.

**Cons**:
- `platform_key_ready` doesn't reveal *which* byte is waiting. Reading it to check for CAN consumes non-CAN bytes, losing user input.
- Would require a new `platform_peek` API to inspect without consuming.
- `poll` is a syscall — calling it between every token execution is expensive on POSIX.
- On embedded, checking UART is a register read (cheap), but the byte-consumption problem remains.

### Option B: POSIX signal handler sets flag on VM struct

Install a `SIGINT` handler that sets a `volatile sig_atomic_t` flag on the VM. Safe points read the flag — a plain memory read, no syscall. Platform-specific setup lives behind a `platform_init` function.

**Pros**:
- No stdin involvement during execution; no byte consumption problem.
- Safe-point check is a single memory read (zero syscall overhead).
- Maps to embedded: replace signal handler with UART RX interrupt or ISR that sets the same flag.
- `volatile sig_atomic_t` is the C-standard-blessed type for signal handler communication.

**Cons**:
- Signal handlers have constraints (async-signal-safe functions only). Setting a flag is safe; anything more complex is not.
- POSIX-specific setup code needed (but isolated in `platform_posix.c`).

### Option C: Flag on REPL struct instead of VM

Same as Option B but the flag lives outside the VM, in REPL-level state.

**Pros**: Keeps VM struct "pure" (no platform concerns).

**Cons**: The executor needs to check the flag but doesn't have access to the REPL. Adds a dependency or requires passing extra context through the call chain. The VM already bundles all mutable execution state; the flag belongs with it.

## Decision

**Option B**: POSIX `SIGINT` signal handler setting `volatile sig_atomic_t interrupted` on the VM struct.

Deciding factors:
- Zero-cost safe-point checks (memory read, not syscall).
- No risk of consuming user input bytes.
- Clean embedded mapping (UART ISR sets the same field).
- Flag on VM struct keeps all execution state in one place; executor already has VM access.

### Implementation details

- **Flag**: `volatile sig_atomic_t interrupted` field on `froth_vm`.
- **Signal setup**: `platform_init()` in `platform_posix.c` installs `SIGINT` handler. Other platforms provide their own `platform_init()`.
- **Handler**: Sets `froth_vm.interrupted = 1`. Nothing else.
- **Safe points**: Check `vm->interrupted` in:
  - `froth_execute_quote` (between each token dispatch)
  - `prim_while` (each iteration, before calling the body)
- **On interrupt**: Clear the flag, return `FROTH_ERROR_INTERRUPT`.
- **Error code**: `FROTH_ERROR_INTERRUPT` added to the error enum with a stable value (next available in the runtime range).
- **REPL**: No changes needed — existing `catch`-based error recovery handles it like any other thrown error.

## Consequences

- Infinite `while` loops become safe to experiment with in the REPL.
- The platform layer gains `platform_init()`, which each platform must implement (can be a no-op on targets where interrupt detection is wired differently).
- The `volatile sig_atomic_t` type is portable C, but the signal handler registration is POSIX-specific — this is already isolated by the platform abstraction.
- Future: ESP32 implementation will set the same flag from a UART RX ISR when CAN is received, requiring no changes to the executor or REPL.

## References

- FROTH-Interactive v0.5, Section 3: CAN interrupt flag, safe points
- C11 7.14.1.1: `signal`, `sig_atomic_t`, async-signal-safe constraints
- ADR-015: catch/throw error propagation (reused for interrupt recovery)
- ADR-016: stable error codes (interrupt code must have a stable value)
