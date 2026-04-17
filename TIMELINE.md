# Frothy Timeline

*Last updated: 2026-04-15*

This file is the movable milestone and queue ledger for Frothy.
The roadmap current-state block in
`docs/roadmap/Frothy_Development_Roadmap_v0_1.md` remains the live control
surface.

If this file and the roadmap disagree, the roadmap wins.

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
  Reference: roadmap section 7 and
  `docs/archive/proofs/m3a_esp32_prompt_transcript.txt`.
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
  Deliverable: save, restore, and `dangerous.wipe` on the overlay image.
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
  `docs/archive/proofs/m10_esp32_proof_transcript.txt`.

## Movable Post-v0.1 Queue

The queue below is intentionally movable. Reorder it as priorities change, but
keep each item's description and references so deferral does not erase context.

- [x] Prompt-facing record surface matches the landed implementation
  Deliverable: the prompt-facing shell accepts the maintained `record ...`
  forms and keeps record definition, construction, field access, inspection,
  and save/restore behavior aligned with the landed parser, evaluator, and
  snapshot record surface.
  References: Frothy ADR-112, `src/frothy_shell.c`,
  `tests/frothy_shell_test.c`, `tests/frothy_parser_test.c`,
  `tests/frothy_snapshot_test.c`, and
  `docs/spec/Frothy_Language_Spec_vNext.md`.
- [x] Control-surface repair and workshop-prep note
  Deliverable: thin `PROGRESS.md`, movable `TIMELINE.md`, targeted
  `AGENTS.md`, and one forward-priority note.
  References: Frothy ADR-116 and
  `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`.
- [x] Workshop install, editor, and recovery surface
  Deliverable: make install, device discovery, connect, interrupt, reconnect,
  and reset-safe send dependable on the maintained Frothy path.
  References: Frothy ADR-110, Frothy ADR-111, Frothy ADR-113,
  `tools/package-release.sh`, and `.github/workflows/release.yml`.
- [x] Inspection-first teaching surface
  Deliverable: make `words`, `see`, `core`, `slotInfo`, and `@` clean enough
  for the workshop exploration and puzzle path.
  References: `docs/spec/Frothy_Language_Spec_v0_1.md`,
  Frothy ADR-107, `docs/spec/Frothy_Language_Spec_vNext.md`, and
  Frothy ADR-114.
- [x] Workshop base-image library and board surface
  Deliverable: define the preflashed workshop base library and the board
  surface for blink, animation, `millis`, ADC, GPIO, and related helpers.
  References: Frothy ADR-108,
  `docs/roadmap/Frothy_M9_Board_FFI_Closeout.md`, and
  `docs/spec/Frothy_Language_Spec_v0_1.md`, Appendix C.
- [x] Embedded tool surface tranche 1
  Deliverable: stop treating `v0.1` as the whole user-facing ceiling, and ship
  a first serious embedded-helper cut through the maintained base image:
  `map`, `clamp`, `mod`, `wrap`, and integer `random.*` helpers plus short
  aliases.
  References: Frothy ADR-123,
  `docs/roadmap/Frothy_Embedded_Tool_Surface_Tranche_1.md`,
  `boards/posix/lib/base.frothy`,
  `boards/esp32-devkit-v1/lib/base.frothy`,
  `boards/esp32-devkit-v4-game-board/lib/base.frothy`, and
  `tests/frothy_ffi_test.c`.
- [x] Readability language tranche: `in prefix`, `cond`, `case`, and ordinary-code `@`
  Deliverable: land the narrow language additions that most improve workshop
  code and small-library readability without reopening the slot/image model.
  References: Frothy ADR-112, `docs/spec/Frothy_Language_Spec_vNext.md`,
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, and Frothy ADR-114.
- [x] Records for workshop/game objects
  Deliverable: add fixed-layout records for workshop and game-object use
  without widening Frothy into dynamic object bags.
  References: Frothy ADR-112, `docs/spec/Frothy_Language_Spec_vNext.md`, and
  Frothy ADR-114.
- [x] Workshop-tranche safety closeout
  Deliverable: fix tranche-local regressions, rerun the proof ladder, and stop
  only after repeated review passes stop surfacing major findings.
  References: `make test`, `make test-cli-local`, `make test-integration`, and
  `make test-all`.
- [x] Support matrix and release/install artifacts
  Deliverable: freeze the promised attendee matrix in-repo and keep the CLI
  plus VSIX install path truthful.
  References: `.github/workflows/release.yml`, `tools/package-release.sh`,
  `tools/vscode/README.md`, `README.md`, and
  `docs/guide/Frothy_Workshop_Install_Quickstart.md`.
- [x] Attendee-facing naming alignment
  Deliverable: ship the Frothy-owned `frothy` installed command and
  `frothy-cli` repo-local build, while keeping legacy `froth` fallback only at
  explicit transition points so attendees do not bounce between equal names.
  References: `README.md`, `tools/vscode/README.md`,
  `tools/package-release.sh`, and `tools/cli/Makefile`.
- [x] Attendee install email and quickstart
  Deliverable: one sendable install note that explains the CLI/extension split,
  supported platforms, serial expectations, and the first-run path.
  References: `README.md`,
  `docs/guide/Frothy_Workshop_Install_Quickstart.md`,
  `tools/vscode/README.md`, and `docs/guide/Frothy_From_The_Ground_Up.md`.
- [x] Workshop preflight and serial recovery path
  Deliverable: a clean preflight path for CLI presence, extension
  compatibility, serial visibility, board handshake, and fallback recovery on
  the maintained Frothy path.
  References: `tools/cli/cmd/doctor.go`,
  `tools/cli/internal/serial/discover.go`,
  `tools/vscode/src/control-session-client.ts`,
  `tools/frothy/proof_f1_control_smoke.sh`, and
  `tools/frothy/proof_m10_smoke.sh`.
- [x] Workshop starter project and frozen board/game surface
  Deliverable: one sanctioned editable workshop starter surface, exported as
  the public workshop repo source and proved through the maintained host and
  board ladders.
  References: `tools/cli/cmd/new.go`,
  `tools/cli/internal/project/starter.go`,
  `tools/frothy/export_workshop_repo.sh`,
  `workshop/starter.frothy`,
  `docs/adr/121-workshop-base-image-board-library-surface.md`, and
  `docs/guide/Frothy_Workshop_Install_Quickstart.md`.
- [x] Minimal docs front door and quick reference
  Deliverable: install, first connect, inspection, board API, persistence, and
  troubleshooting docs that point at the maintained Frothy path.
  References: `README.md`, `docs/guide/Frothy_From_The_Ground_Up.md`,
  `docs/guide/Frothy_Workshop_Quick_Reference.md`, and
  `tools/vscode/README.md`.
- [ ] Clean-machine validation on promised platforms
  Deliverable: prove the attendee path on at least macOS Apple Silicon, macOS
  Intel, and Linux x86_64 before the workshop.
  Note: the exact checklist and recording sheet are now frozen in
  `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`; the actual
  machine-by-machine passes remain open.
  References: `.github/workflows/release.yml`, `README.md`,
  `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`, and the workshop
  install path above.
- [ ] Classroom hardware and recovery kit
  Deliverable: preflashed boards, known-good data cables, spare hardware,
  board labels, and a written reflash/fallback procedure.
  Note: the written room-side pack-out and recovery card now live in
  `boards/esp32-devkit-v4-game-board/WORKSHOP.md`; physical packing and labeling remain
  open.
  References: `tools/frothy/proof_m10_smoke.sh`,
  `docs/archive/proofs/m10_esp32_proof_transcript.txt`, `boards/`, and
  `boards/esp32-devkit-v4-game-board/WORKSHOP.md`.
- [~] Workshop operational closeout
  Deliverable: execute and record the remaining clean-machine validation,
  room-side hardware/recovery prep, and one complete real-device rehearsal on
  the maintained workshop path.
  Note: the checklist, room-side recovery card, and rehearsal status note are
  checked in, but the actual clean-machine passes, physical room pack-out, and
  one complete recorded rehearsal remain open.
  References: `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`,
  `boards/esp32-devkit-v4-game-board/WORKSHOP.md`,
  `docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md`,
  `docs/spec/Frothy_Language_Spec_v0_1.md`, and `tools/frothy/`.
- [x] Publishability reset tranche 1: immediate cuts
  Deliverable: after the 2026-04-16 workshop gate is stable, remove tracked
  repo pollution, archive historical proof artifacts out of active tooling,
  and delete daemon-era VS Code residue that is already outside the packaged
  extension path.
  References: `docs/audit/Frothy_Repo_Audit_2026-04.md`,
  `tools/vscode/test/package-smoke.js`, and `Makefile`.
- [x] Publishability reset tranche 2: naming and packaging normalization
  Deliverable: freeze one publishable identity matrix for Frothy, move the
  shipped CLI/install surface to `frothy` and the repo-local checkout build to
  `frothy-cli`, normalize the Go module/import path, and keep any legacy
  compatibility narrow and explicit.
  References: `docs/audit/Frothy_Repo_Audit_2026-04.md`, `README.md`,
  `tools/cli/go.mod`, and `tools/package-release.sh`.
- [x] Publishability reset tranche 3: proof and dependency collapse
  Deliverable: reduce proofs to core local, extended local, hardware-only, and
  release-only tiers; keep Node extension-only; remove Python from release glue
  first; and quarantine any remaining hardware-only Python.
  References: `docs/audit/Frothy_Repo_Audit_2026-04.md`, `Makefile`,
  `tools/frothy/proof.sh`, and `tools/package-firmware-release.sh`.
- [x] Publishability reset tranche 4: runtime boundary tightening
  Deliverable: make the retained Froth substrate set explicit, quarantine
  compatibility shims, and remove false placeholder labeling from kept runtime
  files without starting a speculative rewrite.
  References: `docs/audit/Frothy_Repo_Audit_2026-04.md`,
  `docs/reference/Froth_Substrate_References.md`, and `CMakeLists.txt`.
  Include: migrate the remaining board/project FFI seams off the legacy
  `froth_ffi_entry_t` / `froth_project_bindings` compatibility path, then
  delete the legacy dispatch/install surface once the maintained
  `frothy_ffi_entry_t` path is the only live ABI.
- [x] Publishability reset tranche 5: docs front door and archive pass
  Deliverable: keep one front door, shorten extension docs to extension-owned
  behavior, and archive historical proof evidence and duplicated release prose
  out of active tooling.
  References: `docs/audit/Frothy_Repo_Audit_2026-04.md`, `README.md`,
  `tools/vscode/README.md`, and `docs/archive/`.
- [ ] Deferred workspace/image-flow queue
  Deliverable: keep workspace/image flow deferred and single-sourced in
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md` until it is
  intentionally reprioritized.
  References: Frothy ADR-115 and
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- [ ] Deferred evaluator frame-arena ownership revisit
  Deliverable: revisit shared evaluator frame-arena ownership only when
  Frothy intentionally grows multiple live runtime instances or another
  re-entrant evaluator owner in one process.
  Note: the first explicit-stack tranche for `CALL`, `IF`, `WHILE`, `SEQ`, and
  required compound evaluation paths is landed on `main`; the maintained
  single-runtime path keeps that explicit stack authoritative, so the
  remaining ownership cleanup is deferred rather than a current workshop
  blocker.
  References: Frothy ADR-118, Frothy ADR-105,
  `docs/archive/adr/040-cs-trampoline-executor.md`, `src/frothy_eval.c`,
  `src/frothy_shell.c`, `tests/frothy_eval_test.c`,
  `tests/frothy_shell_test.c`, and `tools/frothy/proof_eval_stack_budget.sh`.

## Queue Rules

- The checkboxes above are the live movable queue.
- Reordering the queue is expected when priorities change.
- If an item needs more than a short description here, add or update its
  reference doc instead of expanding this file into narrative history.
- The dated v0.1 ladder ends at M10. Do not invent fake dated milestones for
  post-`v0.1` work.
