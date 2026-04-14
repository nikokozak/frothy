# Frothy Timeline

*Last updated: 2026-04-14*

This file is the movable milestone and queue ledger for Frothy.
The roadmap current-state block in
`docs/roadmap/Frothy_Development_Roadmap_v0_1.md` remains the live control
surface.

If this file and the roadmap disagree, the roadmap wins.

## Current Control Snapshot

- Active milestone: `queued follow-on only`
- Blocked by: none
- Next artifact: first workshop install/editor/recovery hardening cut across CLI release, VSCode distribution, and reset-safe send
- Next proof command: `sh tools/frothy/proof_control_surface_docs.sh`

## Closed v0.1 Ladder

- [x] M0 Freeze direction
  Deliverable: accepted spec, roadmap, and control-doc baseline.
  Reference: `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`, section 7.
- [x] M1 Fork hygiene
  Deliverable: Frothy release identity separated from inherited Froth.
  Reference: Frothy ADR-100 and roadmap section 7.
- [x] M2 ADR foundation
  Deliverable: Frothy ADR-100 through ADR-108.
  Reference: `docs/adr/README.md`.
- [x] M3 Parallel host scaffolding
  Deliverable: `build/Frothy` exists beside inherited substrate.
  Reference: roadmap section 7.
- [x] M3a Device smoke
  Deliverable: ESP32 shell stub reaches prompt and safe boot.
  Reference: roadmap section 7 and checked-in prompt transcripts.
- [x] M4 Parser + canonical IR
  Deliverable: parser and canonical IR under focused test coverage.
  Reference: roadmap section 7.
- [x] M5 Evaluator + stable rebinding
  Deliverable: host evaluator with stable-slot semantics.
  Reference: roadmap section 7.
- [x] M6 Cells stores
  Deliverable: narrow top-level `cells(n)` storage with tests.
  Reference: roadmap section 7 and Frothy ADR-104.
- [x] M7 Snapshot format
  Deliverable: save, restore, and wipe on the overlay image.
  Reference: roadmap section 7 and Frothy ADR-106.
- [x] M8 Interactive profile
  Deliverable: multiline REPL, interrupt, inspection, and `boot`.
  Reference: roadmap section 7 and Frothy ADR-107.
- [x] M9 Board FFI surface
  Deliverable: narrow board-facing base image plus proof coverage.
  Reference: `docs/roadmap/Frothy_M9_Board_FFI_Closeout.md`.
- [x] M10 Hardware proof
  Deliverable: blink, boot persistence, and cells proof on ESP32.
  Reference: `./tools/frothy/proof_m10_smoke.sh <PORT>` and
  `tools/frothy/m10_esp32_proof_transcript.txt`.

## Movable Post-v0.1 Queue

The queue below is intentionally movable. Reorder it as priorities change, but
keep each item's description and references so deferral does not erase context.

- [x] Control-surface repair and workshop-prep note
  Deliverable: thin `PROGRESS.md`, movable `TIMELINE.md`, targeted
  `AGENTS.md`, and one forward-priority note.
  References: Frothy ADR-116 and
  `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`.
- [ ] Workshop install, editor, and recovery surface
  Deliverable: make install, device discovery, connect, interrupt, reconnect,
  and reset-safe send dependable on the maintained Frothy path.
  References: Frothy ADR-110, Frothy ADR-111, Frothy ADR-113,
  `tools/package-release.sh`, and `.github/workflows/release.yml`.
- [ ] Inspection-first teaching surface
  Deliverable: make `words`, `see`, `core`, `slotInfo`, and `@` clean enough
  for the workshop exploration and puzzle path.
  References: `docs/spec/Frothy_Language_Spec_v0_1.md`,
  Frothy ADR-107, `docs/spec/Frothy_Language_Spec_vNext.md`, and
  Frothy ADR-114.
- [ ] Workshop base-image library and board surface
  Deliverable: define the preflashed workshop base library and the board
  surface for blink, animation, `millis`, ADC, GPIO, and related helpers.
  References: Frothy ADR-108,
  `docs/roadmap/Frothy_M9_Board_FFI_Closeout.md`, and
  `docs/spec/Frothy_Language_Spec_v0_1.md`, Appendix C.
- [ ] Readability language tranche: `in prefix`, `cond`, `case`, and ordinary-code `@`
  Deliverable: land the narrow language additions that most improve workshop
  code and small-library readability without reopening the slot/image model.
  References: Frothy ADR-112, `docs/spec/Frothy_Language_Spec_vNext.md`,
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, and Frothy ADR-114.
- [ ] Records for workshop/game objects
  Deliverable: add fixed-layout records for workshop and game-object use
  without widening Frothy into dynamic object bags.
  References: Frothy ADR-112, `docs/spec/Frothy_Language_Spec_vNext.md`, and
  Frothy ADR-114.
- [ ] Performance and persistence closeout on workshop programs
  Deliverable: prove the real workshop loops, input paths, and
  `save` / `restore` / `wipe` story with measured before/after checks.
  References: `docs/roadmap/F1_Runtime_Hardening_Benchmark_Notes.md`,
  `docs/spec/Frothy_Language_Spec_v0_1.md`, and the roadmap follow-on queue.
- [ ] Workshop content and rehearsal
  Deliverable: freeze the lesson arc, puzzle path, proofs, and handoff so the
  maintained Frothy path works without extra spoken repair.
  References: `README.md`, `PROGRESS.md`, `TIMELINE.md`, and `tools/frothy/`.
- [ ] Host-only slot-bundle inspection/generation in the CLI project layer
  Deliverable: the first workspace/image-flow implementation cut only, after
  the workshop-critical tranches above are stable.
  References: Frothy ADR-115 and
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- [ ] Later workspace/image-flow apply/load growth
  Deliverable: only after host-only inspection/generation proves useful; still
  no registry, daemon, or loader-first detour.
  References: Frothy ADR-115 and
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.

## Queue Rules

- The checkboxes above are the live movable queue.
- Reordering the queue is expected when priorities change.
- If an item needs more than a short description here, add or update its
  reference doc instead of expanding this file into narrative history.
- The dated v0.1 ladder ends at M10. Do not invent fake dated milestones for
  post-`v0.1` work.
