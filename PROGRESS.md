# Frothy Progress

*Last updated: 2026-04-14*

This file is the thin operational note for Frothy.
The current-state block in `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the authoritative live control surface.

If this file disagrees with the accepted Frothy spec, ADRs, or roadmap, this
file is wrong.

## Current Control Snapshot

- Active milestone: `queued follow-on only`
- Blocked by: none
- Next artifact: attendee install, naming-alignment, and preflight hardening cut across CLI/VSCode release artifacts, serial readiness, and workshop starter materials
- Next proof command: `sh tools/frothy/proof_control_surface_docs.sh`

## Landed And Still Relevant

- Frothy `v0.1` is closed through M10; the dated ladder is done.
- Spoken-ledger syntax tranche 1 is the frozen baseline for future language
  work. See `docs/spec/Frothy_Language_Spec_vNext.md`,
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, and
  `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`.
- The first workspace/image-flow tranche is closed as a doc-only,
  slot-bundle-first boundary. See
  `docs/adr/115-first-workspace-image-flow-tranche.md` and
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- The direct-control surface, helper/editor path, and runtime hardening
  tranches are already landed and are no longer the unclear part of the repo
  story.
- Attendee-facing naming alignment is still active: the repo, product, and
  editor are Frothy, while the installed CLI path is still transitional
  `froth`, and the workshop queue now treats that mismatch as explicit work.
- This control-surface repair tranche is landed: `PROGRESS.md` and
  `TIMELINE.md` are thin again, `AGENTS.md` supports targeted work, and the
  forward queue now lives in one short roadmap note plus Frothy ADR-116.
- The approved follow-on order is now workshop-first: delivery/editor/recovery,
  inspection, board library and surface, readability language work, records,
  then performance/persistence closeout.
- The first workshop base-image board/library cut is landed: `millis()` and
  `gpio.read()` are now native base slots, the preflashed workshop helper
  library is seeded as base image and survives `wipe()`, and the M10 proof
  ladder now covers `blink`, `animate`, GPIO helpers, and `adc.percent`.
  Reference: `docs/adr/117-workshop-base-image-board-library-surface.md`.
- The workshop implementation tranche is now closed on `main`: the delivery,
  inspection, workshop base-image, readability-language, and records cuts have
  all survived the local proof ladder plus repeated review cycles.

## Near-Term Priority Stack

- 1. Support matrix and release/install artifacts: freeze the promised
  platforms and ship the CLI plus VSIX install path truthfully.
- 2. Attendee-facing naming alignment: converge the workshop-facing product,
  CLI, extension, and docs story so people do not bounce between Frothy,
  `froth`, and `froth-cli`.
- 3. Attendee install email and quickstart: tell people exactly what to
  install, why the CLI and extension are both needed, and what to expect.
- 4. Workshop preflight and serial recovery path: verify CLI presence,
  extension compatibility, serial visibility, board handshake, and fallback
  recovery without requiring firmware build tooling.
- 5. Workshop starter project and frozen board/game surface: give attendees
  one sanctioned project and one sanctioned display/board API.
- 6. Minimal docs front door and quick reference: install, first connect,
  inspection, board API, persistence, and troubleshooting.
- 7. Clean-machine validation on promised platforms.
- 8. Classroom hardware and recovery kit: preflashed boards, known-good data
  cables, reflash path, spare hardware, and CLI fallback.
- 9. Workshop rehearsal plus measured performance/persistence closeout on the
  actual lesson path.
- 10. Host-only slot-bundle inspection/generation after the workshop path is
  solid again.
- Reference: `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`

## Workshop Gate

- Before 2026-04-16, the control docs, queue order, and deferrals must be
  explicit enough that paused discussions do not lose context.
