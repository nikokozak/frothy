# Frothy ADR-102: Frothy/32 Value Representation

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, Appendix A
**Roadmap milestone(s)**: M2, M3, M5, M6, M7
**Inherited Froth references**: `docs/adr/001-cell-width.md`, `docs/adr/004-value-tagging.md`

## Context

Frothy `v0.1` needs a concrete implementation profile for host and ESP32-class
targets. The language surface is small, but persistence, inspection, and FFI
all depend on a clear runtime value model.

The runtime profile must:

- fit a 32-bit target floor
- support fast small-value handling
- keep snapshots pointer-free
- distinguish persistable and non-persistable runtime kinds
- stay small enough to explain and debug

## Options Considered

### Option A: 32-bit tagged value word with slot and object indirection

Use a 32-bit runtime value word with immediate encodings and indirect handles
for slot and object references.

Trade-offs:

- Pro: fits the intended host and ESP32-class profile.
- Pro: keeps snapshots pointer-free.
- Pro: keeps runtime representation explicit and inspectable.

### Option B: Native pointer-rich host representation first

Use a looser host-only representation and tighten it later.

Trade-offs:

- Pro: faster prototype on host.
- Con: delays the real constraints that matter for persistence and targets.
- Con: invites semantic drift before hardware reality is confronted.

### Option C: More aggressive compression or NaN-boxing

Pursue a denser or more clever representation from the start.

Trade-offs:

- Pro: may save space later.
- Con: adds complexity before the semantic core is proven.
- Con: makes explanation and debugging worse.

## Decision

**Option A.**

Frothy `v0.1` targets a 32-bit value-word profile:

- runtime value word: 32 bits
- low tag bits:
  - `00`: signed 30-bit integer
  - `01`: special immediate (`nil`, `false`, `true`, reserved)
  - `10`: slot ID
  - `11`: object ID

The minimum object-kind set is:

- `TEXT`
- `CELLS_DESC`
- `CODE`
- `NATIVE_ADDR`
- `FOREIGN_HANDLE`

Persistable language-visible values are `Int`, `Bool`, `Nil`, `Text`, `Cells`,
and `Code`. Native addresses and foreign handles remain runtime-only and must
be rejected by persistence.

## Consequences

- The implementation must preserve a pointer-free persisted image.
- Small scalar values stay cheap and direct.
- Object identity remains explicit instead of hidden in host pointers.
- Future optimization work may add caches or bytecode, but not replace the
  canonical persisted representation promised by this profile.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
