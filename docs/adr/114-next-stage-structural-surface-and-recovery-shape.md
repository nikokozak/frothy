# Frothy ADR-114: Next-Stage Structural Surface And Recovery Shape

**Date**: 2026-04-13
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_vNext.md`, sections 3, 4, 6, 7, 8; `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, sections 2, 3, 4, 5
**Roadmap milestone(s)**: post-M10 follow-on language definition
**Inherited Froth references**: none

## Context

Frothy ADR-112 defined the boundary for the next language tranche:

- keep spoken-ledger syntax tranche 1 as the current baseline
- keep the remaining work small enough to review before implementation widens
- add records, modules, stronger multi-way control, and Frothy-native recovery
  without reopening the accepted `v0.1` slot/image core

The remaining draft still had several unresolved collisions:

- whether `in prefix [ ... ]` was already baseline syntax or still draft
- whether counted iteration and `when` / `unless` were still open design topics
- whether ordinary-code `@name` was deferred or already part of the next draft
- whether records would use `.` or another field-access spelling
- whether modules implied a loader or only prefixed stable slots
- whether in-language recovery would use `raise`, `fail`, or a looser surface

That ambiguity was large enough to make later implementation drift likely.

## Options Considered

### Option A: Keep the remaining draft loose

Leave the vNext docs broad and postpone the exact structural and recovery
choices until implementation starts.

Trade-offs:

- Pro: fewer immediate writing decisions.
- Con: invites runtime widening from discussion alone.
- Con: leaves contradictory draft statements in place.
- Con: weakens the roadmap milestone closeout.

### Option B: Freeze spoken-ledger tranche 1 and narrow the remaining draft now

Accept the already-landed spoken-ledger tranche 1 as the frozen baseline, then
choose one explicit draft shape for records, modules, `cond` / `case`,
`try/catch`, and binding/place designators.

Trade-offs:

- Pro: preserves the accepted `v0.1` core and ADR-112 boundary.
- Pro: gives later implementation one decision-complete draft target.
- Pro: keeps the remaining semantic widening small and reviewable.
- Con: still defers actual runtime work.

### Option C: Reopen the runtime model together with the remaining draft

Use the next-stage docs to also introduce new runtime object classes, new
persistence rules, or a generalized module/recovery system.

Trade-offs:

- Pro: more ambitious feature jump.
- Con: directly violates the current branch constraint.
- Con: collides with the accepted `v0.1` authority and roadmap boundary.

## Decision

**Option B.**

Spoken-ledger syntax tranche 1 is the frozen baseline for the next-stage draft.
The remaining draft is narrowed as follows:

- counted iteration, `when`, `unless`, `and`, and `or` are baseline, not
  open design questions
- records use `record Name [ field, ... ]`, constructor slot `Name: ...`,
  field read `value->field`, and field update `set value->field to expr`
- `->` is the chosen record-field spelling because `.` stays with ordinary slot
  names and dotted prefixes
- `in prefix [ ... ]` is source-time grouping over prefixed stable top-level
  slots only
- nested `in` forms concatenate prefixes
- this module draft introduces no module object, second namespace, loader,
  registry, or package surface
- `cond` is ordered boolean clause selection with optional `else`
- `case expr [ ... ]` evaluates its scrutinee once and dispatches on scalar
  literals only: `Int`, `Bool`, `Nil`, and `Text`
- `case` has no fallthrough
- Frothy-native recovery uses `try [ ... ] catch err [ ... ]` and explicit
  `fail: err`
- first-step `try/catch` handles runtime evaluation failures only and is
  explicitly non-transactional
- parse, restore, startup, interrupt, and reset remain boundary-control
  failures
- binding/place designators are restricted to stable top-level slots:
  `@name` and `@module.name`
- those designators are not valid for locals, parameters, computed names,
  cells elements, record fields, or a new persisted runtime value class

## Consequences

- The next-stage language-definition milestone can close as doc-only work
  without pretending runtime widening already happened.
- Later implementation work inherits one explicit draft target for structural
  surface and recovery shape.
- The accepted `v0.1` authority remains intact, and ADR-112 continues to serve
  as the umbrella boundary for why this tranche exists at all.
- Any future change to record layout rules, module loading, catchable-failure
  scope, or binding/place breadth now requires a deliberate new ADR update
  instead of prose drift.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/spec/Frothy_Language_Spec_vNext.md`
- `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
- `docs/adr/112-next-stage-language-growth-and-recovery-boundary.md`
