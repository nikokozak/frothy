# ADR-002: Host-native Development First, with Platform Abstraction Boundary

**Date**: 2026-02-26
**Status**: Accepted
**Spec sections**: General (no specific section — this is a development strategy decision)

## Context

We're building a VM that targets ESP32 and RP2040, but we need to iterate fast. Cross-compilation adds friction: longer build times, flash-and-test cycles, serial console debugging. We need to decide whether we develop against the target hardware from day one, or build and test on the host machine first.

Separately, the VM needs platform services (character I/O now, timing and persistent storage later). These work completely differently on a POSIX host vs. an ESP32. We need a strategy for keeping the VM portable without over-engineering it.

## Options Considered

### Option A: Target-first (ESP-IDF from day one)

Set up the ESP-IDF toolchain, compile and flash for every test cycle.

Trade-offs: we'd be testing on real hardware immediately, which catches platform-specific issues early. But the edit-compile-flash-test loop is slow (10-30 seconds per cycle), debugging is limited to serial printf, and we'd be blocked if the hardware isn't available.

### Option B: Host-native first, port later with no abstraction

Build with the system compiler (clang/gcc), test on macOS. When it's time to port, go through the code and swap out platform-specific calls.

Trade-offs: fast iteration, but the port becomes a surgery. Every `printf` and `getchar` in the VM has to be found and replaced. Easy to miss one.

### Option C: Host-native first, with a thin platform abstraction boundary

Same as B, but platform-dependent operations go behind a small interface (`platform.h`) from the start. The VM never calls `getchar()` directly — it calls `platform_getchar()`. We provide a POSIX implementation now; we'll add an ESP32 implementation when we port.

Trade-offs: tiny upfront cost (one header, one `.c` file with ~5 wrapper functions to start). The port becomes "write a new `platform_esp32.c`" instead of "find and replace every I/O call." The risk is scope creep — putting too much behind the boundary, or accidentally hiding spec-mandated behavior in it.

## Decision

**Option C.** We develop and test on macOS with the system compiler. Platform-dependent operations (I/O, timing, eventually flash storage) go behind a `platform.h` interface. The VM itself stays platform-agnostic.

The boundary stays thin and dumb. It only abstracts *how* the platform does something, never *what* the VM is supposed to do. If we find ourselves putting Froth semantics (error codes, stack behavior, execution rules) behind the boundary, that's a bug in our architecture.

Starting surface:
- `platform_init()` — one-time setup
- `platform_getchar()` — blocking character read
- `platform_putchar(int c)` — write one character
- `platform_puts(const char *s)` — write a string (convenience, avoids per-char call overhead)

We'll add timing and storage functions later, as ADRs, when we actually need them.

## Consequences

- The VM core has no `#include <stdio.h>` — it includes `platform.h` instead.
- The CMake build compiles `platform_posix.c` by default. A future ESP-IDF build will swap in `platform_esp32.c`.
- We can run the full test suite on the host machine in milliseconds, which matters a lot for the pace we need to keep.
- The abstraction boundary is a judgment call. We need to stay disciplined about what goes behind it. Rule of thumb: if the spec defines the behavior, it belongs in the VM. If the behavior depends on the OS or hardware, it belongs behind the boundary.
- We lose early detection of target-specific issues (memory limits, alignment quirks, flash timing). Acceptable risk — we'll catch those during the port.

## References

- Timeline: host-native development supports the daily proof-of-concept cadence
- Spec: Froth is designed to be portable across AVR/RP2040/ESP32 — a platform boundary aligns with the spec's intent
