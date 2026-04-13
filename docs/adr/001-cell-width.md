# ADR-001: Cell Width — 32-bit Default, Compile-time Configurable

**Date**: 2026-02-26
**Status**: Accepted
**Spec sections**: 3.1 (Values), 3.2 (Stacks)

## Context

The spec says cell width is "implementation-defined, typically 16/32/64." We need to pick a default and decide how rigid that choice is.

Our primary targets are ESP32 (32-bit Xtensa/RISC-V) and RP2040 (32-bit ARM Cortex-M0+). We'd also like to leave the door open for smaller micros (AVR/ATtiny, 8-bit) and potentially 64-bit hosts for development.

## Options Considered

### Option A: Hardcode 32-bit

Use `int32_t` / `uint32_t` everywhere. Simple, no abstraction cost.

Trade-offs: easiest to write, but locks us in. If we ever want to run on an 8-bit AVR with 16-bit cells, or use 64-bit cells on a desktop for testing with larger values, we'd have to go back and retrofit a typedef through the entire codebase.

### Option B: Compile-time typedef with a CMake option

Define a `froth_cell_t` (and unsigned `froth_ucell_t`) whose underlying type is selected by a build flag — e.g., `-DFROTH_CELL_BITS=32`. Default to 32. Provide a header that maps this to the right `stdint.h` type and defines related constants (min, max, format specifiers).

Trade-offs: small upfront cost (one header, a few `#define`s), but every piece of code that touches cells goes through the typedef from day one. Changing width later is a recompile, not a rewrite.

### Option C: Runtime-configurable width

Store cell width as a VM parameter; indirect through it on every memory access.

Trade-offs: huge complexity cost, performance hit on every operation, and the serialization format would need to be width-aware. Way overkill for our use case.

## Decision

**Option B.** Default to 32-bit cells. Width is selected at compile time via `FROTH_CELL_BITS`, which maps to the appropriate `int32_t` / `int16_t` / `int64_t` under the hood.

The deciding factor: the cost is near-zero (one header with a few typedefs and a switch on the define), and it avoids a painful retrofit later. The spec explicitly anticipates multiple widths, so we should too.

## Consequences

- All code uses `froth_cell_t` and `froth_ucell_t` — never raw `int` or `int32_t` for stack values.
- We need a small header (`froth_types.h` or similar) that defines these types plus constants like `FROTH_CELL_MAX`, `FROTH_CELL_MIN`, and a `PRId` format macro for printing.
- When we get to value tagging (cells vs object references), the tag scheme will need to fit within whatever width is selected. That's a future ADR, but we should keep it in mind.
- Changing width is a recompile and a re-test, not a code change. That's the right trade-off.

## References

- Spec Section 3.1 (Values): "a machine word-sized signed integer (implementation-defined width, typically 16/32/64)"
- Spec Section 3.2 (Stacks): DS holds Values, which include Cells
