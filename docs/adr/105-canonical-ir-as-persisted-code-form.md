# Frothy ADR-105: Canonical IR As Persisted Code Form

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 5.4, 6.1, 8.4, Appendix A.4, Appendix A.5
**Roadmap milestone(s)**: M2, M4, M7, M8
**Inherited Froth references**: none

## Context

Frothy wants persistent live images without tying the language contract to exact
source preservation or to an early bytecode decision. Inspection and restore
need one normalized form that is semantic, stable, and small enough to reason
about.

## Options Considered

### Option A: Canonical tree-shaped IR is the persisted code truth

Parse Frothy surface syntax into a semantic IR and persist that IR directly.

Trade-offs:

- Pro: stable semantic core for `see`, `core`, and persistence.
- Pro: no need to preserve exact source text.
- Pro: keeps bytecode optional instead of required.

### Option B: Persist exact source and reparse on restore

Store surface text and reconstruct semantics from text later.

Trade-offs:

- Pro: simpler to explain initially.
- Con: couples persistence to exact parser behavior and source retention.
- Con: weak basis for normalized inspection.

### Option C: Require bytecode as the first canonical form

Compile immediately to a lower-level execution form and persist that.

Trade-offs:

- Pro: may help performance later.
- Con: premature complexity for `v0.1`.
- Con: makes the semantic source of truth less transparent.

## Decision

**Option A.**

Canonical tree-shaped IR is the persisted source of truth for Frothy code.

Minimum node set:

- `LIT`
- `READ_LOCAL`
- `WRITE_LOCAL`
- `READ_SLOT`
- `WRITE_SLOT`
- `READ_INDEX`
- `WRITE_INDEX`
- `FN`
- `CALL`
- `IF`
- `WHILE`
- `SEQ`

Rules:

- evaluation order is left to right
- persisted `Code` stores arity, local count, constant table, and canonical IR
- exact source metadata is optional and auxiliary only
- no bytecode layer is required in `v0.1`
- inspection should derive from canonical IR, not from preserved raw source

## Consequences

- Parser and evaluator work must converge on one semantic representation.
- `see` and `core` can stay consistent across formatting differences.
- Later bytecode or optimization work can be additive caches, not the normative
  persisted contract.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
