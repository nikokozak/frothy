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

What still needs explicit surfacing is the order of the next follow-on work.
The workshop implementation tranche is now landed on `main`, but the immediate
queue head changed after an ESP32 `boot` loop exposed that ordinary embedded
looping still depended on hidden C stack depth through the recursive
evaluator. The live control surface now puts evaluator execution-stack
hardening first. The workshop-operational list below is the resumed order after
that runtime tranche lands.

## Priority Stack

### 1. Support matrix and release/install artifacts

Goal:

- freeze the promised attendee platform matrix and ship a truthful CLI plus
  VSIX install path

Held boundary:

- promise only the release targets that actually exist
- keep the install story concrete and release-shaped rather than aspirational

References:

- `README.md`
- `tools/package-release.sh`
- `.github/workflows/release.yml`
- `tools/vscode/README.md`

### 2. Attendee-facing naming alignment

Goal:

- converge the workshop-facing product, CLI, extension, and docs story so
  attendees do not bounce between Frothy, `froth`, and `froth-cli`

Held boundary:

- prefer one truthful transitional story over ambiguous mixed naming
- do not break release/install compatibility late; if a full rename is risky,
  use compatibility shims and explicit wording instead of half-renaming

References:

- `README.md`
- `tools/vscode/README.md`
- `tools/package-release.sh`
- `tools/cli/Makefile`

### 3. Attendee install email and quickstart

Goal:

- send one clear install note that tells attendees what to install, why the
  CLI and extension are both needed, what platforms are supported, and what to
  expect before they arrive

Held boundary:

- keep this note short, direct, and operational
- explain the Frothy product name versus the transitional `froth` CLI name
  truthfully instead of papering over it

References:

- `README.md`
- `tools/vscode/README.md`
- `docs/guide/Frothy_From_The_Ground_Up.md`

### 4. Workshop preflight and serial recovery path

Goal:

- verify CLI presence, extension compatibility, serial visibility, board
  handshake, and fallback recovery on the maintained Frothy path

Held boundary:

- do not require firmware build tooling just to confirm a preflashed attendee
  board is workshop-ready
- make the recovery story explicit before the workshop rather than discovering
  it live at the tables

References:

- `tools/cli/cmd/doctor.go`
- `tools/cli/internal/serial/discover.go`
- `tools/vscode/src/control-session-client.ts`
- `docs/adr/110-single-owner-control-session-transport.md`
- `docs/adr/111-vscode-extension-owned-control-session.md`

### 5. Workshop starter project and frozen board/game surface

Goal:

- give attendees one sanctioned folder shape and one sanctioned board/display
  API for the puzzle, blink, animation, sensor, and small-game path

Held boundary:

- keep the starter narrow and teachable
- use the maintained Frothy workshop surface rather than inventing a second
  one-off demo stack

References:

- `tools/cli/cmd/new.go`
- `tools/frothy/`
- `boards/esp32-devkit-v1/`
- `docs/adr/117-workshop-base-image-board-library-surface.md`

### 6. Minimal docs front door and quick reference

Goal:

- publish a minimal documentation front door for install, first connect,
  inspection, board API, persistence, and troubleshooting

Held boundary:

- do not try to mirror a whole site before the workshop
- point everything at the maintained Frothy path and the sanctioned starter

References:

- `README.md`
- `tools/vscode/README.md`
- `docs/guide/Frothy_From_The_Ground_Up.md`

### 7. Clean-machine validation on promised platforms

Goal:

- prove the attendee path on the platforms we actually promise before people
  walk in with their laptops

Held boundary:

- promise only what has been exercised on clean machines
- treat install failure as a top-level workshop blocker rather than a late bug

References:

- `.github/workflows/release.yml`
- `README.md`
- `tools/vscode/README.md`

### 8. Classroom hardware and recovery kit

Goal:

- make the room-side hardware path resilient: preflashed boards, known-good
  cables, spare parts, labels, and a written recovery procedure

Held boundary:

- do not let the software path depend on unplanned reflashing or ad hoc cable
  triage
- keep the fallback CLI path ready if the extension misbehaves on a given
  machine

References:

- `tools/frothy/proof_m10_smoke.sh`
- `tools/frothy/m10_esp32_proof_transcript.txt`
- `boards/`

### 9. Workshop rehearsal plus measured performance/persistence closeout

Goal:

- run the actual lesson path end to end, verify loop cadence, sensor flow, and
  `save` / `restore` / `dangerous.wipe`, and freeze the take-home path

Held boundary:

- no speculative rewrite
- tie the proof surface to the actual workshop puzzle, blink, animation,
  sensor, and small-game flow

References:

- `docs/roadmap/F1_Runtime_Hardening_Benchmark_Notes.md`
- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `tools/frothy/`

### 10. Later workspace/image-flow work

Goal:

- defer host-only slot-bundle implementation and any later apply/load growth
  until the workshop path is solid

Held boundary:

- do not skip straight to loader, registry, or on-device artifact work
- do not let workspace/image flow displace the workshop-critical tranches above

References:

- `docs/adr/115-first-workspace-image-flow-tranche.md`
- `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`

## After-Workshop Publishability Reset

Once the 2026-04-16 workshop gate is stable, the next disciplined cleanup is
the publishability-reset stack frozen in
`docs/audit/Frothy_Repo_Audit_2026-04.md`.

Execute it in this order:

1. immediate cuts for daemon-era editor residue, tracked repo pollution, and
   proof artifacts that belong in docs/archive space rather than active tooling
2. naming and packaging normalization, including the Go module/import path and
   one explicit Frothy-versus-`froth` identity matrix
3. proof and dependency collapse, removing Python from release glue first and
   keeping Node extension-only/release-only
4. runtime boundary tightening, making retained Froth substrate and temporary
   compatibility shims explicit
5. docs front door and archive pass

Worktree guidance:

- do not default to a worktree for the pre-workshop queue above unless parallel
  owners force it
- do use a dedicated worktree for the multi-day post-workshop cleanup tranches
  that touch many files or subsystems
- the best worktree candidates are naming/module-path normalization,
  proof/dependency collapse, runtime-boundary tightening, and the docs/archive
  pass
- avoid a worktree for doc-only control-surface edits or one-file workshop
  fixes that should land directly and quickly

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

Items 1 through 8 may move as priorities change.
When they move, preserve each item's goal, held boundary, and references.
