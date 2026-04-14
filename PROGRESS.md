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
- Next artifact: first workshop install/editor/recovery hardening cut across CLI release, VSCode distribution, and reset-safe send
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
- The direct-control surface, helper/editor path, runtime hardening, and CLI
  naming-alignment tranches are already landed and are no longer the unclear
  part of the repo story.
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

## Near-Term Priority Stack

- 1. Workshop install, editor, and recovery surface: make install, connect,
  interrupt, reconnect, and reset-safe send dependable.
- 2. Inspection-first teaching surface: make `words`, `see`, `core`,
  `slotInfo`, and `@` clean enough for the workshop exploration path.
- 3. Workshop base-image library and board surface: cover blink, animation,
  `millis`, ADC, GPIO, and other workshop-critical helpers.
- 4. Readability language tranche: `in prefix`, `cond`, `case`, and
  ordinary-code `@`.
- 5. Records for workshop/game objects.
- 6. Performance and persistence closeout on actual workshop programs.
- 7. Workshop content and rehearsal.
- Reference: `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`

## Workshop Gate

- Before 2026-04-16, the control docs, queue order, and deferrals must be
  explicit enough that paused discussions do not lose context.
