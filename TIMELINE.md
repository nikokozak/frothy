# Frothy Timeline

*Last updated: 2026-04-13*

This file is the movable milestone and queue ledger for Frothy.
The roadmap current-state block in
`docs/roadmap/Frothy_Development_Roadmap_v0_1.md` remains the live control
surface.

If this file and the roadmap disagree, the roadmap wins.

## Current Control Snapshot

- Active milestone: `queued follow-on only`
- Blocked by: none
- Next artifact: first host-only slot-bundle inspection/generation artifact in the CLI project layer
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
- [ ] Host-only slot-bundle inspection/generation in the CLI project layer
  Deliverable: the first workspace/image-flow implementation cut only.
  References: Frothy ADR-115 and
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- [ ] FFI boundary quality and porting discipline
  Deliverable: make the shipped shim and porting rules explicit, narrow, and
  test-backed before any ABI cleanup.
  References: Frothy ADR-108 and
  `docs/roadmap/Frothy_M9_Board_FFI_Closeout.md`.
- [ ] Small useful core library growth
  Deliverable: choose and land a minimal library tranche that makes Frothy
  more useful without sneaking in new runtime semantics.
  References: `docs/spec/Frothy_Language_Spec_v0_1.md`, Appendix C, and
  Frothy ADR-112.
- [ ] Robust string support
  Deliverable: define the next truthful string-support cut around current
  byte-string semantics, utilities, and FFI/persistence boundaries.
  References: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 3.6 and 9,
  and `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`.
- [ ] Measured performance tightening
  Deliverable: benchmark-backed speed work on the current runtime rather than
  speculative rewrites.
  References: `docs/roadmap/F1_Runtime_Hardening_Benchmark_Notes.md` and the
  roadmap follow-on queue.
- [ ] Direct-control tooling improvements
  Deliverable: keep CLI/helper/editor improvements on the single-owner control
  path and out of daemon or PTY drift.
  References: Frothy ADR-110, Frothy ADR-111, and Frothy ADR-113.
- [ ] Workshop readiness for 2026-04-16
  Deliverable: make the demo path, kept scope, and deferred queue explicit
  before the workshop.
  References: `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`
  and `README.md`.
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
