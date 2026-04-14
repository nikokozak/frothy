# Frothy ADR-117: Record Value Representation And Persistence

**Date**: 2026-04-14
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_vNext.md`, record sections
**Roadmap milestone(s)**: post-v0.1 workshop/game-object records tranche
**Inherited Froth references**: none

## Context

The workshop/game-object tranche needs a small record surface that keeps the
existing Frothy substrate promises intact:

- stable top-level slot identity
- truthful inspection
- overlay-only save / restore / wipe
- additive runtime growth beside the inherited substrate

The accepted forward-direction docs already keep records separate from the
readability branch. This tranche therefore needs a records-only runtime and
persistence cut instead of a mixed control-surface branch.

The key design pressure is representation:

- record declarations must create a stable, persistable top-level definition
- record instances must keep a fixed field layout even after the definition
  slot is rebound later
- the snapshot walk must stay small and reject shapes that need a general graph
  loader or open object model

## Decision

Frothy records land as a fixed-layout, closed-surface value family with two
runtime object kinds:

- `record-def`
- `record`

Accepted surface in this tranche:

- `record Name [ field, ... ]`
- `Name: ...`
- `value->field`
- `set value->field to expr`

Boundary rules:

- record declarations are top-level only
- record fields are fixed and ordered by the declaration
- field names are simple identifiers only; dotted field names are rejected
- zero-field records are rejected
- record values retain their own `record-def`, so rebinding the top-level
  definition slot does not retroactively change older instances
- record field values may be `Int`, `Bool`, `Nil`, `Text`, and `Record`
- `Cells` are widened only enough to store `Record` values in addition to the
  previously persistable scalar/text set
- `RecordDef`, `Cells`, `Code`, and `Native` values are not allowed in record
  fields
- record equality remains the existing object-identity equality
- there are no dynamic maps, open bags, computed field names, or reflection
  APIs in this tranche

Inspection rules:

- a bound record definition renders as `record Name [ field, ... ]`
- a record instance renders as `Name: ...` in definition order
- inspection class names gain `record-def` and `record`

Persistence rules:

- snapshots persist both `record-def` and `record` objects
- persistence remains overlay-only and symbol-remapped
- cyclic record graphs are rejected during `save()` as
  `FROTH_ERROR_NOT_PERSISTABLE`
- the Frothy snapshot payload version advances to `2`

## Consequences

- workshop/game-object overlays can use fixed-layout records without reopening
  the language around dynamic object bags
- rebinding a record definition slot preserves old instance layout, which keeps
  stable slot identity separate from instance representation
- truthful inspection now covers both the declaration object and the instance
  value shape
- snapshot compatibility intentionally breaks across the version bump; older
  payloads fail as incompatible instead of restoring under the wrong object
  rules
- any future move to open records, dynamic lookup, structural equality, or
  cyclic object persistence requires a later ADR

## References

- `docs/adr/101-stable-top-level-slot-model.md`
- `docs/adr/105-canonical-ir-as-persisted-code-form.md`
- `docs/adr/106-snapshot-format-and-overlay-walk-rules.md`
- `docs/adr/112-next-stage-language-growth-and-recovery-boundary.md`
- `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`
- `docs/spec/Frothy_Language_Spec_vNext.md`
