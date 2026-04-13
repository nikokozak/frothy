# Frothy ADR-108: Frothy FFI Boundary

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 3.2, 8.1, 9, 10
**Roadmap milestone(s)**: M2, M9, M10
**Inherited Froth references**: `src/froth_ffi.h`, `src/froth_ffi.c`

## Context

Frothy must expose hardware and platform services naturally, but a full native
ABI redesign is not on the critical path for `v0.1`. The first implementation
should reuse working Froth FFI substrate where practical while presenting a
Frothy value-oriented call surface to the user.

## Options Considered

### Option A: Frothy call surface over reused Froth FFI substrate

Expose foreign bindings as top-level base-image `Code` values and keep internal
registration and native entrypoints close to the inherited substrate for the
first milestone.

Trade-offs:

- Pro: fastest path to hardware proof.
- Pro: keeps runtime scope small during bootstrap.
- Con: temporary mismatch between user model and internal calling machinery.

### Option B: Full value-oriented native ABI redesign first

Redesign the native boundary before using existing substrate.

Trade-offs:

- Pro: cleaner long-term story immediately.
- Con: higher schedule risk with little user-visible value in `v0.1`.

### Option C: Expose raw native handles and pointers directly

Let low-level native details leak through the language surface.

Trade-offs:

- Pro: expedient for early experiments.
- Con: violates the Frothy value model and persistence boundary.

## Decision

**Option A.**

Frothy foreign bindings are base-image top-level `Code` values.

User-facing rules:

- foreign calls use ordinary Frothy syntax such as `gpio.write(pin, 1)`
- native runtime state does not persist
- raw pointers, general foreign handles, and implementation-private control
  objects are not language-visible values

Initial implementation rules:

- internal registration may reuse the inherited Froth stack-oriented FFI
  substrate
- the `v0.1` call surface must support `Int`, `Bool`, `Nil`, and `Text`
- cells handles may be supported where the initial shim can do so clearly
- foreign handles remain non-persistable

Minimum board-facing base-image bindings stay intentionally small:

- `gpio.mode`
- `gpio.write`
- `ms`
- one input primitive such as `adc.read`
- one serial or bus surface only if it stays within the timebox

## Consequences

- Frothy gets a natural user-facing call model without forcing an ABI rewrite
  during bootstrap.
- The first FFI shim must be explicit about type checks and non-persistable
  state.
- A cleaner native ABI can be a later additive cleanup once the language core
  is proven.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
