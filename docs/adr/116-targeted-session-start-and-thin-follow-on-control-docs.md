# Frothy ADR-116: Targeted Session Start And Thin Follow-On Control Docs

**Date**: 2026-04-13
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 1, 8, Appendix C; `docs/spec/Frothy_Language_Spec_vNext.md`
**Roadmap milestone(s)**: post-M10 docs/control-surface repair
**Inherited Froth references**: none

## Context

Frothy already has accepted language, runtime, and follow-on direction docs:

- the accepted `v0.1` spec
- the Frothy `100`-series ADR stack
- the roadmap current-state block
- the `vNext` language and surface docs
- the accepted workspace/image-flow tranche note

The repo surface still drifted anyway:

- `PROGRESS.md` became a long narrative status log instead of a short
  operational note
- `TIMELINE.md` became part ledger, part slip diary, and part follow-on
  narrative
- `AGENTS.md` still implied a heavyweight full-session cold-start ritual even
  for targeted work
- the post-`v0.1` queue existed in pieces, but not as one legible
  workshop-facing stack

That drift made it too easy to lose context between conversations and too hard
to defer work without re-explaining it.

## Options Considered

### Option A: Keep the current control-doc sprawl

Leave the roadmap, progress file, timeline, and agent guidance as-is.

Trade-offs:

- Pro: no immediate doc churn.
- Con: the repo keeps multiple narrative status streams.
- Con: targeted work keeps paying a stale startup tax.
- Con: the forward queue stays hard to reorder without losing information.

### Option B: Restore thin control docs and task-scoped startup guidance

Keep the roadmap current-state block authoritative, make `PROGRESS.md` short,
make `TIMELINE.md` a movable checkbox ledger, and let `AGENTS.md` require only
task-relevant reads unless the task is broad or stale.

Trade-offs:

- Pro: the live repo surface becomes legible again.
- Pro: deferred discussions retain context through short descriptions and
  explicit references.
- Pro: targeted work becomes faster without weakening authority discipline.
- Con: requires touching several repo-control docs together.

### Option C: Collapse everything back into the roadmap only

Remove most of the information from `PROGRESS.md`, `TIMELINE.md`, and
`AGENTS.md`, and keep the roadmap as the only maintained control document.

Trade-offs:

- Pro: fewer files to update.
- Con: the roadmap becomes the only place to carry both status and queue
  descriptions.
- Con: agents and users lose the thin operational mirrors that help quick
  orientation.

## Decision

**Option B.**

Frothy adopts the following repo-control rules for post-`v0.1` work:

- the roadmap current-state block remains the authoritative live control
  surface
- `PROGRESS.md` is a short operational note only
- `TIMELINE.md` is a movable checkbox ledger for closed milestones and the
  reorderable post-`v0.1` queue
- if queue context would be lost, that context belongs in a referenced roadmap
  or ADR note, not in `TIMELINE.md` prose
- `AGENTS.md` supports targeted work by default and requires the broader read
  pass only when the task is broad, stale, or policy/semantics-touching
- the post-`v0.1` priority stack and workshop gate live in one short roadmap
  note rather than being scattered across status logs

This ADR changes workflow and control-surface policy only.
It does not widen Frothy language or runtime semantics.

## Consequences

- Frothy regains a short, reviewable live control surface.
- Deferred discussions keep their context through queue descriptions and
  explicit references.
- Agents can work faster on targeted tasks without guessing at semantics or
  skipping authority checks.
- Any later attempt to turn `PROGRESS.md` or `TIMELINE.md` back into narrative
  sprawl is now a policy regression, not just a style preference.

## References

- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
- `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`
- `PROGRESS.md`
- `TIMELINE.md`
- `AGENTS.md`
- `docs/adr/109-repo-control-surface-and-proof-path.md`
