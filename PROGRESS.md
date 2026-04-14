# Frothy Progress

*Last updated: 2026-04-13*

This file is the thin operational note for Frothy.
The current-state block in `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the authoritative live control surface.

If this file disagrees with the accepted Frothy spec, ADRs, or roadmap, this
file is wrong.

## Current Control Snapshot

- Active milestone: `queued follow-on only`
- Blocked by: none
- Next artifact: first host-only slot-bundle inspection/generation artifact in the CLI project layer
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

## Near-Term Priority Stack

- 1. Workshop readiness for 2026-04-16: keep the repo surface truthful,
  readable, and easy to hand off.
- 2. Host-only slot-bundle inspection/generation in the CLI project layer:
  first implementation cut only; no apply/load/helper/editor growth.
- 3. FFI boundary quality and porting discipline: keep ADR-108 narrow while
  making current conversion and error rules explicit and provable.
- 4. Small useful core library growth: add capability without widening
  semantics by drift.
- 5. Robust string support: keep the byte-string contract explicit and stage
  missing utilities and boundaries deliberately.
- 6. Measured performance tightening: use benchmarks and proof paths, not
  guessed speed work.
- 7. Direct-control tooling improvements: keep the CLI/helper/editor path
  smaller than inherited Froth.
- Reference: `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`

## Workshop Gate

- Before 2026-04-16, the control docs, queue order, and deferrals must be
  explicit enough that paused discussions do not lose context.
