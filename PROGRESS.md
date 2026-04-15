# Frothy Progress

*Last updated: 2026-04-14*

This file is the thin operational note for Frothy.
The current-state block in `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the authoritative live control surface.

If this file disagrees with the accepted Frothy spec, ADRs, or roadmap, this
file is wrong.

## Current Control Snapshot

- Active milestone: `evaluator execution-stack hardening`
- Blocked by: none
- Next artifact: Frothy ADR-118 plus the first evaluator-trampoline tranche for `CALL`, `IF`, `WHILE`, and `SEQ` over an explicit frame stack
- Next proof command: `cmake -S . -B build && cmake --build build && ./build/frothy_eval_tests && sh tools/frothy/proof_eval_stack_budget.sh`

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
- The publishability audit is now landed at
  `docs/audit/Frothy_Repo_Audit_2026-04.md`; it freezes the aggressive cleanup
  plan, but the queue still keeps that work behind the 2026-04-16
  workshop-critical path.
- Attendee-facing naming alignment is still active: the repo, product, and
  editor are Frothy, while the installed CLI path is still transitional
  `froth`, and the workshop queue now treats that mismatch as explicit work.
- This control-surface repair tranche is landed: `PROGRESS.md` and
  `TIMELINE.md` are thin again, `AGENTS.md` supports targeted work, and the
  forward queue now lives in one short roadmap note plus Frothy ADR-116.
- The workshop-first follow-on order is now the resumed queue after evaluator
  execution-stack hardening: delivery/editor/recovery, inspection, board
  library and surface, readability language work, records, then
  performance/persistence closeout.
- The first workshop base-image board/library cut is landed: `millis()` and
  `gpio.read()` are now native base slots, the preflashed workshop helper
  library is seeded as base image and survives `dangerous.wipe`, and the M10 proof
  ladder now covers `blink`, `animate`, GPIO helpers, and `adc.percent`.
  Reference: `docs/adr/117-workshop-base-image-board-library-surface.md`.
- The Frothy-native TM1629 workshop board cut is now landed:
  `esp32-devkit-v4-game-board` ships a maintained TM1629 C runtime plus
  baked-in `tm1629.raw.*`, `tm1629.*`, and `matrix.*` base-image surfaces;
  board base ownership now comes from a captured install-time registry rather
  than hard-coded slot-name allowlists; and the host proof ladder now includes
  direct TM1629 runtime tests plus a POSIX sub-build smoke for the new board.
  Reference: `docs/adr/119-tm1629-board-base-surface-and-registry.md`.
- The post-review TM1629 cleanup tranche is now landed: parser, shell, and
  snapshot name validation share one Frothy grammar for `!`, `@`, and `?`;
  `tm1629.raw.init` now fails on invalid pins or failed pin-mode setup instead
  of silently succeeding; the payload-fragmentation proof scales with
  board-configured arena size; the ESP-IDF v4 board target now carries its
  required console defaults, links the TM1629 runtime, honors board.json
  runtime capacities, builds from the current repo, flashes on
  `/dev/cu.usbserial-0001`, and answers direct `matrix.*` / `tm1629.raw.*`
  control smoke on hardware.
- The workshop implementation tranche is now closed on `main`: the delivery,
  inspection, workshop base-image, readability-language, and records cuts have
  all survived the local proof ladder plus repeated review cycles.
- The evaluator stack-overflow regression found by a simple ESP32 `boot` loop
  is fixed, and the Frothy proof ladder now includes a host compile
  stack-budget check for recursive evaluator paths plus record-definition IR
  allocation so large local arrays fail before they reach hardware.
- That stack-budget proof is now explicitly treated as an interim tripwire, not
  the permanent architecture answer; Frothy ADR-118 makes the explicit
  evaluator-frame stack the immediate next runtime cut.
- The ESP32 shell-path overflow on multiline `in` / `cond` / `case` definitions
  is fixed in the current tree by removing a 1KB rewrite buffer from
  `frothy_shell_run()`'s task stack, widening the stack-budget proof to cover
  parser/shell entry paths, and restoring the maintained ESP-IDF main-task
  stack setting to 8192 bytes in `targets/esp-idf/sdkconfig`.
- The host serial control path is now pruned of the legacy raw-`HELLO`
  discovery probe, the maintained macOS CLI transport uses the direct raw
  termios path that actually survives ESP32 prompt/control handoff, and the
  Frothy local connect build cache no longer collides with inherited Froth's
  stale `local-build` directory.
- The workshop release/install surface is now truthful in-repo: `README.md`,
  `docs/guide/Frothy_Workshop_Install_Quickstart.md`,
  `tools/package-release.sh`, and the VS Code docs all agree on the promised
  attendee path of released CLI assets, matching VSIX, and preflashed
  `esp32-devkit-v1` hardware.
- The attendee-facing naming and recovery story is now explicit on the
  maintained Frothy path: Frothy owns the product/docs/editor identity, the
  installed release command remains transitional `froth`, whole-file editor
  send blocks unsafe replay when control `reset` is unavailable, and the
  control proof ladder now re-checks recovery on the real ESP32 path.
- The workshop starter scaffold and its proof path are now landed: `froth new
  --target esp32-devkit-v1` emits the sanctioned lesson/game starter, resolve
  is warning-free, and the maintained M10 board proof now scaffolds, resolves,
  runs, and checks that starter on the attached ESP32 path.

## Near-Term Priority Stack

- 1. Evaluator execution-stack hardening: replace recursive IR evaluation with
  an explicit frame stack/trampoline so ordinary embedded loops and nested game
  code are bounded by Frothy-managed depth rather than hidden C stack.
- 2. Priority repair: live-shell records must match the landed record surface
  so prompt behavior agrees with the landed parser/evaluator/snapshot record
  path before more workshop-facing polish stacks on top.
- 3. Minimal docs front door and quick reference: install, first connect,
  inspection, board API, persistence, and troubleshooting.
- 4. Clean-machine validation on promised platforms.
- 5. Classroom hardware and recovery kit: preflashed boards, known-good data
  cables, reflash path, spare hardware, and CLI fallback.
- 6. Workshop rehearsal plus measured performance/persistence closeout on the
  actual lesson path.
- 7. Post-workshop publishability reset tranche 1: immediate cuts for
  daemon-era editor residue, tracked repo pollution, and archived proof
  artifacts.
- 8. Post-workshop publishability reset tranche 2: naming and packaging
  normalization.
- 9. Post-workshop publishability reset tranche 3: proof and dependency
  collapse.
- 10. Post-workshop publishability reset tranche 4: runtime boundary
  tightening.
- 11. Post-workshop publishability reset tranche 5: docs front door and
  archive pass.
- 12. Deferred workspace/image-flow queue after the workshop path and
  publishability reset are solid again; keep it single-sourced in
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- Reference: `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`
  and `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`

## Workshop Gate

- Before 2026-04-16, the control docs, queue order, and deferrals must be
  explicit enough that paused discussions do not lose context.
- Worktree guidance is now explicit in `TIMELINE.md`: avoid worktrees for
  small pre-workshop fixes, and reserve them for the larger multi-day
  post-workshop cleanup tranches.
