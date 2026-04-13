# Frothy ADR-103: Non-Capturing Code Value Model

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 3.7, 5.3, 6.3, 6.6, 11, Appendix A.5
**Roadmap milestone(s)**: M2, M4, M5
**Inherited Froth references**: none

## Context

Frothy replaces Froth's quotation-centric user model with a smaller lexical
model built around callable `Code` values. The first implementation needs a
function model that is easy to reason about, persist, inspect, and run on small
targets.

The main pressure point is closure capture. Capturing outer locals would expand
the runtime model immediately with environment objects and lifetime rules that
Frothy `v0.1` does not need.

## Options Considered

### Option A: Non-capturing `Code` in `v0.1`

Allow `Code` to use parameters, names bound inside its own body, and top-level
names only. Reject illegal outer-local capture before evaluation.

Trade-offs:

- Pro: keeps the runtime model small.
- Pro: fits canonical IR and persistence cleanly.
- Pro: preserves explicitness around state and rebinding.

### Option B: Full lexical closures from the start

Allow nested functions to capture outer locals normally.

Trade-offs:

- Pro: more expressive.
- Con: requires environment objects and capture semantics immediately.
- Con: complicates persistence, inspection, and implementation size.

### Option C: Dynamic-only name lookup

Avoid lexical validation and defer all lookup to runtime.

Trade-offs:

- Pro: simpler parser pipeline.
- Con: weaker errors and less predictable semantics.
- Con: fails the spec requirement to reject illegal capture before runtime.

## Decision

**Option A.**

In Frothy `v0.1`, `fn(...) { ... }` yields a non-capturing `Code` value.

- `Code` has fixed exact arity.
- A function body may use:
  - parameters
  - locals it binds inside its own body
  - top-level names
- A function body may not capture outer locals from surrounding lexical scopes.
- Implementations must perform lexical resolution and reject illegal outer-local
  capture before evaluation.
- Top-level names remain dynamically live through stable slot lookup, so old
  callers observe new top-level values after redefinition.

## Consequences

- Frothy gets lexical readability without closure machinery in `v0.1`.
- Stateful designs are pushed toward explicit top-level slots and cells stores.
- Later closure support, if it ever arrives, must be an additive design layer
  rather than something silently implied by the first runtime.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
