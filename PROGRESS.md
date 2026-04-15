# Frothy Progress

*Last updated: 2026-04-14*

This file is the thin operational note for Frothy.
The current-state block in `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the authoritative live control surface.

If this file disagrees with the accepted Frothy spec, ADRs, or roadmap, this
file is wrong.

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
  `docs/audit/Frothy_Repo_Audit_2026-04.md`; it now serves as the reference
  record for the landed publishability reset rather than as a future cleanup
  ledger.
- The Frothy-versus-`froth` naming boundary is now explicit and frozen in the
  maintained surface: repo, product, docs, and editor are Frothy, while the
  installed CLI path remains intentionally transitional `froth`.
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
- The first detailed Friday workshop run spec is now checked in at
  `docs/roadmap/Frothy_Workshop_Run_Spec_2026-04-17.md`; it freezes the lesson
  arc, `Get Home` inspection puzzle, `Get Home+` starter game, required helper
  surface, persistence teaching points, and rehearsal checklist.
- The first explicit evaluator-frame-stack tranche is now landed on `main`:
  `CALL`, `IF`, `WHILE`, `SEQ`, and required compound expression paths run
  through a bounded explicit frame stack instead of recursive evaluator entry,
  prompt-facing `record ...` forms now match the landed
  parser/evaluator/snapshot record surface, and the focused host proof slice
  now includes both the eval stack-budget tripwire and shell record coverage.
- The evaluator stack-overflow regression found by a simple ESP32 `boot` loop
  is therefore no longer defended only by compile-time frame-size hygiene; the
  remaining runtime item is closeout around the current bounded frame-arena
  ownership shape plus refreshed device proof, not reintroducing recursive IR
  execution.
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
- The workshop-operational slice is now concrete in-repo without widening the
  product surface: `README.md` points at one minimal front door,
  `docs/guide/Frothy_Workshop_Quick_Reference.md` keeps the in-room prompt and
  recovery path short, `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`
  freezes the promised clean-machine checklist, `boards/esp32-devkit-v1/WORKSHOP.md`
  holds the room-side kit and reflash card, and
  `docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md` carries the
  checked-in rehearsal status note plus the required real-device proof
  command.
- The remaining manual workshop gates stay explicit: separate clean-machine
  passes on the promised platforms, physical room pack-out, and the final
  measured real-device rehearsal closeout are still exit steps, not work that
  prose can claim complete.
- The full publishability reset is now landed on `main`: stale
  proof artifacts are archived under `docs/archive/`, Frothy-facing
  naming/packaging are normalized, release packaging no longer needs Python,
  release CI uses the maintained VS Code host-smoke lane, firmware manifest
  parsing/ordering/validation and artifact path checks are centralized under
  the maintained Go surface, the default proof surface is back to `C` + `Go`
  + `Shell` with explicit `Node` and hardware-only `Python` exceptions, and
  the retained Froth substrate boundary is explicit in code and docs.
  References:
  `docs/audit/Frothy_Repo_Audit_2026-04.md` and
  `docs/reference/Frothy_Retained_Substrate_Manifest.md`.

## Remaining Gates

- Runtime closeout is still open: the explicit evaluator-frame-stack tranche is
  landed, but the bounded frame-arena ownership shape still needs final
  maintainability judgment plus refreshed focused proof on the maintained
  host/device slice.
- The remaining pre-workshop risk is operational rather than structural:
  clean-machine validation, room-side hardware/recovery prep, and one recorded
  measured real-device rehearsal still need to be executed.
- Workspace/image flow remains intentionally deferred and single-sourced in
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- See `TIMELINE.md` for the live movable queue and
  `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md` for the
  rationale behind that order.
