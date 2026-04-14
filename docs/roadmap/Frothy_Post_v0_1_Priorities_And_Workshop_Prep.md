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
It needs a truthful workshop-first queue and small next cuts.

What is already settled:

- the `v0.1` core is functionally closed through M10
- spoken-ledger syntax tranche 1 is the frozen baseline for next-stage
  language work
- the first workspace/image-flow tranche is closed as a slot-bundle-first
  docs-only boundary
- the direct-control transport, helper/editor path, and measured runtime
  hardening slices are landed

What still needs explicit surfacing is the order of the next follow-on work.
The workshop path now leads the queue; slot-bundle-first follow-on work does
not.

## Priority Stack

### 1. Workshop install, editor, and recovery surface

Goal:

- make `brew install frothy`, VSCode setup, device discovery, connect,
  interrupt, reconnect, and reset-safe send reliable enough that another
  person can reach a clean maintained Frothy session quickly

Held boundary:

- keep the accepted single-owner direct-control path
- keep the first cut shipping- and recovery-focused rather than widening the
  editor surface opportunistically

References:

- `README.md`
- `tools/package-release.sh`
- `.github/workflows/release.yml`
- `docs/adr/110-single-owner-control-session-transport.md`
- `docs/adr/111-vscode-extension-owned-control-session.md`
- `docs/adr/113-manifest-owned-project-target-selection.md`

### 2. Inspection-first teaching surface

Goal:

- make `words`, `see`, `core`, `slotInfo`, and `@` clean enough to support
  slot introspection, the workshop exploration puzzle, and a truthful teaching
  story

Held boundary:

- improve inspection quality and metadata exposure before introducing heavier
  runtime features
- keep this tranche focused on discoverability rather than generic UI growth

References:

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/adr/107-interactive-profile-boot-and-interrupt.md`
- `docs/spec/Frothy_Language_Spec_vNext.md`
- `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`

### 3. Workshop base-image library and board surface

Goal:

- define the preflashed workshop base library and board-facing surface for
  blink, animation, `millis`, ADC, GPIO, and related utilities while keeping
  that surface small, explicit, and easy to teach

Held boundary:

- no broad native ABI redesign first
- no inherited Froth library carry-over without a Frothy-native surface review

References:

- `docs/adr/108-frothy-ffi-boundary.md`
- `docs/roadmap/Frothy_M9_Board_FFI_Closeout.md`
- `docs/spec/Frothy_Language_Spec_v0_1.md`, Appendix C

### 4. Readability language tranche: `in prefix`, `cond`, `case`, and ordinary-code `@`

Goal:

- land the narrow language additions that most improve workshop code and small
  library readability without reopening the slot/image model

Held boundary:

- `in prefix` remains source-time grouping over prefixed stable slots only
- `cond` / `case` remain narrow control additions
- ordinary-code `@` remains a stable top-level slot designator, not a new
  persisted value class

References:

- `docs/adr/112-next-stage-language-growth-and-recovery-boundary.md`
- `docs/spec/Frothy_Language_Spec_vNext.md`
- `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`
- `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`

### 5. Records for workshop/game objects

Goal:

- add fixed-layout records for workshop and game-object use without widening
  Frothy into dynamic object bags

Held boundary:

- keep record layout fixed and explicit
- do not reopen the one-namespace stable-slot model

References:

- `docs/adr/112-next-stage-language-growth-and-recovery-boundary.md`
- `docs/spec/Frothy_Language_Spec_vNext.md`
- `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`

### 6. Performance and persistence closeout on workshop programs

Goal:

- prove the real workshop loops, input paths, and `save` / `restore` / `wipe`
  story under measured before/after checks rather than guessed optimization

Held boundary:

- no speculative rewrite
- tie the proof surface to actual workshop programs and board behavior

References:

- `docs/roadmap/F1_Runtime_Hardening_Benchmark_Notes.md`
- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`

### 7. Workshop content and rehearsal

Goal:

- freeze the teaching arc, puzzle path, proofs, and handoff so the maintained
  Frothy path works without extra spoken repair

Held boundary:

- only add features or examples that materially improve the actual workshop
  lesson path
- keep the demo path on the maintained Frothy surface

References:

- `README.md`
- `PROGRESS.md`
- `TIMELINE.md`
- `tools/frothy/`

### 8. Later workspace/image-flow work

Goal:

- defer host-only slot-bundle implementation and any later apply/load growth
  until the workshop path is solid

Held boundary:

- do not skip straight to loader, registry, or on-device artifact work
- do not let workspace/image flow displace the workshop-critical tranches above

References:

- `docs/adr/115-first-workspace-image-flow-tranche.md`
- `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`

## Workshop Gate For 2026-04-16

Before the 2026-04-16 workshop:

- keep the install, editor, and recovery story strong enough that workshop
  participants can reach a clean session quickly
- keep the introspection, puzzle, blink, animation, sensor, and persistence
  path on the maintained Frothy surface
- keep accepted `v0.1` semantics authoritative until a deliberate follow-on
  tranche changes them
- keep workspace/image flow deferred behind the workshop-critical tranches
- keep the later queue visible enough that pausing discussion does not erase it

## Reordering Rule

Items 1 through 8 may move as priorities change.
When they move, preserve each item's goal, held boundary, and references.
