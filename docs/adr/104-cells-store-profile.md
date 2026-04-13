# Frothy ADR-104: Cells Store Profile

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 3.8, 4.4, 5.3, 7.4, 7.5, Appendix A
**Roadmap milestone(s)**: M2, M6, M7, M10
**Inherited Froth references**: `src/froth_cellspace.h`, `docs/adr/054-first-froth-cellspace-profile.md`

## Context

Frothy needs one mutable data structure in `v0.1` that is strong enough for
real hardware sketches without reopening the full collection-design problem.

The store must be:

- fixed size
- easy to inspect
- easy to persist
- cheap to implement on small targets
- explicit about ownership and mutation

## Options Considered

### Option A: One fixed-size `Cells` store in `v0.1`

Provide `cells(n)` as the only collection value. Keep it top-level owned, fixed
size, and bounds checked.

Trade-offs:

- Pro: enough for practical stateful sketches.
- Pro: small persistence and runtime surface.
- Pro: maps cleanly onto existing cellspace-like substrate.

### Option B: General mutable collections from the start

Introduce growable arrays, maps, or records immediately.

Trade-offs:

- Pro: richer user surface.
- Con: large design space with weak timebox discipline.
- Con: raises allocation and persistence complexity early.

### Option C: No mutable aggregates in `v0.1`

Keep only scalars and top-level names.

Trade-offs:

- Pro: smallest possible surface.
- Con: too weak for the intended hardware proof work.

## Decision

**Option A.**

Frothy `v0.1` includes one collection value: `Cells`.

- `cells(n)` creates a fixed-size mutable indexed store.
- `cells(n)` is only valid in a top-level rebinding form.
- The created store is owned by the top-level overlay slot that receives it.
- Elements start as `nil`.
- Allowed element kinds are `Int`, `Bool`, `Nil`, and `Text`.
- Reads and writes are bounds checked.
- `set place = expr` supports indexed cells mutation for existing stores.
- The first implementation should reuse inherited CellSpace-style substrate
  where that keeps the design smaller.

## Consequences

- Frothy gets explicit persistent mutable storage without broader collection
  machinery.
- Local allocation remains intentionally constrained.
- Future richer data structures must be additive and must not silently turn
  `Cells` into a growable general-purpose heap object.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
