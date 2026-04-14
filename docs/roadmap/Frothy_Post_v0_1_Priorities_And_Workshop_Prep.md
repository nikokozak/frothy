# Frothy Post-v0.1 Priorities And Workshop Prep

Status: active follow-on priority note
Date: 2026-04-13
Authority: `docs/spec/Frothy_Language_Spec_v0_1.md`, `docs/spec/Frothy_Language_Spec_vNext.md`, `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, `docs/adr/108-frothy-ffi-boundary.md`, `docs/adr/112-next-stage-language-growth-and-recovery-boundary.md`, `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`, `docs/adr/115-first-workspace-image-flow-tranche.md`, `docs/adr/116-targeted-session-start-and-thin-follow-on-control-docs.md`, `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`

## Purpose

This note makes the post-`v0.1` queue legible without widening Frothy
semantics through status prose.

The accepted `v0.1` spec remains authoritative for current behavior.
The `vNext` docs remain the draft direction for later language work.

## Current Read

Frothy does not currently need another broad rewrite.
It needs a truthful queue and small next cuts.

What is already settled:

- the `v0.1` core is functionally closed through M10
- spoken-ledger syntax tranche 1 is the frozen baseline for next-stage
  language work
- the first workspace/image-flow tranche is closed as a slot-bundle-first
  docs-only boundary
- the direct-control transport, helper/editor path, and measured runtime
  hardening slices are landed

What still needs explicit surfacing is the order of the next follow-on work.

## Priority Stack

### 1. Workshop readiness for 2026-04-16

Goal:

- make the repo surface readable enough that another person can understand the
  current Frothy story without a spoken replay

Held boundary:

- do not reopen language or runtime semantics to chase workshop polish

References:

- `README.md`
- `PROGRESS.md`
- `TIMELINE.md`
- `AGENTS.md`

### 2. Host-only slot-bundle inspection/generation

Goal:

- land the first workspace/image-flow implementation cut in the CLI project
  layer only

Held boundary:

- no apply/load growth yet
- no helper/editor/manifest/kernel-loader growth yet
- no registry, package, daemon, or PTY detour

References:

- `docs/adr/115-first-workspace-image-flow-tranche.md`
- `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`

### 3. FFI boundary quality and porting discipline

Goal:

- make the shipped Frothy shim and its porting rules explicit, narrow, and
  test-backed before any deeper native cleanup is attempted

Held boundary:

- no full value-oriented native ABI redesign first

References:

- `docs/adr/108-frothy-ffi-boundary.md`
- `docs/roadmap/Frothy_M9_Board_FFI_Closeout.md`

### 4. Small useful core library growth

Goal:

- grow a small library that makes Frothy more useful on the accepted `v0.1`
  substrate

Held boundary:

- do not smuggle records, modules, or wider runtime semantics into library work

References:

- `docs/spec/Frothy_Language_Spec_v0_1.md`, Appendix C
- `docs/adr/112-next-stage-language-growth-and-recovery-boundary.md`
- `docs/spec/Frothy_Language_Spec_vNext.md`

### 5. Robust string support

Goal:

- define the next truthful string-support cut around current byte-string
  semantics, missing utilities, and boundary behavior at FFI and persistence

Held boundary:

- `Text` remains immutable byte strings unless a later accepted ADR says
  otherwise

References:

- `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 3.6 and 9
- `docs/spec/Frothy_Language_Spec_v0_1.md`, Appendix C

### 6. Measured performance tightening

Goal:

- keep speed work benchmark-backed and tied to the existing runtime rather than
  a speculative rewrite

Held boundary:

- no guessed optimization passes without measured before/after proof

References:

- `docs/roadmap/F1_Runtime_Hardening_Benchmark_Notes.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`, runtime-hardening follow-on section

### 7. Direct-control tooling improvements

Goal:

- improve CLI/helper/editor ergonomics on the accepted single-owner control
  path

Held boundary:

- do not bring back daemon ownership, PTY passthrough, or shared-client
  transport design

References:

- `docs/adr/110-single-owner-control-session-transport.md`
- `docs/adr/111-vscode-extension-owned-control-session.md`
- `docs/adr/113-manifest-owned-project-target-selection.md`

### 8. Later workspace/image-flow apply/load growth

Goal:

- consider additive apply/load growth only after the host-only inspection and
  generation cut proves that the slot-bundle artifact is actually useful

Held boundary:

- do not skip straight to loader, registry, or on-device artifact work

References:

- `docs/adr/115-first-workspace-image-flow-tranche.md`
- `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`

## Workshop Gate For 2026-04-16

Before the 2026-04-16 workshop:

- keep the control docs aligned and short enough to hand to another person
  without extra narration
- keep the demo and proof story on the maintained Frothy path:
  `build/Frothy`, `make test`, the transitional `froth`/repo-local
  `froth-cli` split, direct control, and the checked-in M10 proof bundle
- keep the accepted `v0.1` semantics frozen
- keep `vNext` runtime work draft-only
- keep FFI work quality-focused rather than ABI-redesign-first
- keep workspace/image flow host-only and inspection/generation-only
- keep library, string, performance, and tooling items visible in the queue
  even if discussion pauses

## Reordering Rule

Items 2 through 8 may move as priorities change.
When they move, preserve each item's goal, held boundary, and references.
