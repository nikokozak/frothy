# Frothy Post-v0.1 Priorities And Workshop Prep

Status: active follow-on priority note
Date: 2026-04-14
Authority: `docs/spec/Frothy_Language_Spec_v0_1.md`, `docs/spec/Frothy_Language_Spec_vNext.md`, `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, `docs/adr/108-frothy-ffi-boundary.md`, `docs/adr/112-next-stage-language-growth-and-recovery-boundary.md`, `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`, `docs/adr/115-first-workspace-image-flow-tranche.md`, `docs/adr/116-targeted-session-start-and-thin-follow-on-control-docs.md`, `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`

## Purpose

This note makes the post-`v0.1` queue legible without widening Frothy
semantics through status prose.

The accepted `v0.1` spec remains authoritative for current behavior.
The `vNext` docs remain the draft direction for later language work.

## Current Read

Frothy does not currently need another broad rewrite.
It needs a truthful immediate queue and small next cuts.

What is already settled:

- the `v0.1` core is functionally closed through M10
- spoken-ledger syntax tranche 1 is the frozen baseline for next-stage
  language work
- the first workspace/image-flow tranche is closed as a slot-bundle-first
  docs-only boundary
- the direct-control transport, helper/editor path, and measured runtime
  hardening slices are landed
- the workshop release/install matrix, transitional Frothy-versus-`froth`
  naming story, attendee quickstart, serial recovery path, and sanctioned
  starter scaffold are now landed on `main`

What still needs explicit surfacing is the order of the next follow-on work.
The workshop implementation tranche is now landed on `main`, but the immediate
queue head changed after an ESP32 `boot` loop exposed that ordinary embedded
looping still depended on hidden C stack depth through the recursive
evaluator. The first explicit evaluator-frame-stack tranche and the
prompt-facing record repair are now landed on `main`; the remaining runtime
item is closeout around the bounded frame-arena ownership shape plus refreshed
proof on the maintained device path.

The workshop-operational artifacts are now concrete in-repo:

- `docs/guide/Frothy_Workshop_Quick_Reference.md`
- `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`
- `boards/esp32-devkit-v1/WORKSHOP.md`
- `docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md`

Those files freeze the front door, the clean-machine checklist, the room-side
recovery card, and the rehearsal status note plus proof command.
They do not claim that every manual gate has already been executed; clean
machines, physical room pack-out, and the final measured real-device closeout
still have to be run and recorded.

## Why This Order

- Runtime closeout still leads because the first explicit
  evaluator-frame-stack tranche is landed, but the bounded frame-arena
  ownership shape still needs final maintainability judgment and refreshed
  proof on the maintained device path. Keep that work narrow; do not reopen
  recursive evaluator execution by another path. References:
  `src/frothy_eval.c`, `tests/frothy_eval_test.c`,
  `tools/frothy/proof_eval_stack_budget.sh`, and
  `docs/adr/118-explicit-evaluator-frame-stack-for-canonical-ir-execution.md`.
- Clean-machine validation, classroom hardware prep, and measured rehearsal are
  next because the remaining workshop risk is operational. The install/docs
  surface and the room-side recovery artifacts are already checked in; what is
  left is to execute and record the real passes. References:
  `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`,
  `boards/esp32-devkit-v1/WORKSHOP.md`, and
  `docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md`.
- Workspace/image flow stays deferred because the staged queue is already
  single-sourced in
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`, and this note
  should not become a second implementation ledger for that future work.

## Publishability Reset

The publishability-reset stack frozen in
`docs/audit/Frothy_Repo_Audit_2026-04.md` is now landed on `main`.

That means the repo now has:

1. archived historical proof artifacts in `docs/archive/`
2. one explicit Frothy-versus-`froth` publishability matrix
3. a maintained core proof/dependency surface centered on `C`, `Go`, and
   `Shell`, with `Node` and hardware-only `Python` kept explicit
4. an explicit retained-substrate/runtime-boundary record in code and docs
5. one maintained docs/archive front door instead of competing cleanup notes

## Workshop Gate For 2026-04-16

Before the 2026-04-16 workshop:

- publish a truthful install path with a frozen support matrix
- publish one clear naming story for Frothy versus `froth`
- give attendees a sendable install note and a preflight path before they
  arrive
- keep the introspection, puzzle, blink, animation, sensor, and persistence
  path on one sanctioned starter and one maintained board surface
- prove the path on clean machines and carry a recovery kit into the room
- keep workspace/image flow deferred behind the workshop-critical tranches

## Reordering Rule

The remaining runtime closeout and workshop-operational items may move as
priorities change.
When they move, preserve the reasoning above instead of cloning another queue.
