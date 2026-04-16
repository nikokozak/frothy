# Frothy Progress

*Last updated: 2026-04-15*

This file is the thin operational note for Frothy.
The current-state block in `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the authoritative live control surface.

If this file disagrees with the accepted Frothy spec, ADRs, or roadmap, this
file is wrong.

## Landed And Still Relevant

- Frothy `v0.1` is closed through M10; the dated ladder is done.
- The first embedded tool-surface tranche is now landed: Frothy no longer
  treats the accepted `v0.1` spec as the whole present-day user-facing
  ceiling, the maintained base image now ships `map`, `clamp`, `mod`, `wrap`,
  and integer `random.*` helpers plus short aliases across the maintained board
  paths, and `docs/adr/123-post-v0_1-embedded-tool-surface.md` plus
  `docs/roadmap/Frothy_Embedded_Tool_Surface_Tranche_1.md` now record that
  boundary explicitly, including the naming rule that canonical dotted
  families stay ordinary slots while bare aliases are reserved for common pure
  transforms.
- The first workshop-board DRAM downsize tranche is now landed: the v4 board
  now carries an `8192`-byte heap and a `64`-frame explicit evaluator stack,
  the snapshot codec no longer keeps duplicated encode/decode tables live in
  BSS at the same time, base-slot ownership no longer costs a full pointer
  array, and `docs/roadmap/Frothy_Workshop_DRAM_Tranche_2026-04-15.md` records
  the exact baseline, scenario high-waters, byte recovery, and remaining
  payload-arena constraints.
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
- The Frothy CLI/install rename tranche is now landed: repo, product, docs,
  release assets, installed CLI, and repo-local checkout build all use the
  Frothy-owned `frothy` / `frothy-cli` names, while VS Code keeps only narrow
  legacy `froth` discovery fallback during the transition.
- This control-surface repair tranche is landed: `PROGRESS.md` and
  `TIMELINE.md` are thin again, `AGENTS.md` supports targeted work, and the
  forward queue now lives in one short roadmap note plus Frothy ADR-116.
- The workshop-operational queue now leads: clean-machine validation,
  room-side hardware/recovery prep, and one recorded measured rehearsal pass;
  the evaluator frame-arena ownership revisit is deferred until Frothy
  intentionally grows multiple live runtime instances or another re-entrant
  evaluator owner.
- The first workshop base-image board/library cut is landed: `millis()` and
  `gpio.read()` are now native base slots, the preflashed workshop helper
  library is seeded as base image and survives `dangerous.wipe`, and the M10 proof
  ladder now covers `blink`, `animate`, GPIO helpers, and `adc.percent`.
  Reference: `docs/adr/121-workshop-base-image-board-library-surface.md`.
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
  arc, `Get Home` inspection puzzle, the shared `pong.frothy` game, required helper
  surface, persistence teaching points, and rehearsal checklist.
- The v4 workshop-helper tranche is now landed on the maintained proto-board
  path: `esp32-devkit-v4-game-board` base now carries the generic workshop
  helpers plus `grid.*`, `joy.*`, and `knob.*`; board-configured Frothy
  capacities are driven from `board.json` on both host and ESP-IDF builds; the
  Friday workshop docs now describe the real v4 matrix/knob/joystick surface;
  and real-device proof on the mounted board froze the semantic joystick map to
  `left=JOY_1`, `click=JOY_2`, `down=JOY_3`, `up=JOY_4`, `right=JOY_6`, with
  `dangerous.wipe` restoring those base-owned pin slots on hardware.
- The first explicit evaluator-frame-stack tranche is now landed on `main`:
  `CALL`, `IF`, `WHILE`, `SEQ`, and required compound expression paths run
  through a bounded explicit frame stack instead of recursive evaluator entry,
  prompt-facing `record ...` forms now match the landed
  parser/evaluator/snapshot record surface, and the focused host proof slice
  now includes both the eval stack-budget tripwire and shell record coverage.
- The evaluator stack-overflow regression found by a simple ESP32 `boot` loop
  is therefore no longer defended only by compile-time frame-size hygiene; the
  explicit evaluator-frame-stack tranche is the maintained path, and the
  remaining bounded frame-arena ownership revisit is now deferred until
  multi-instance runtime work makes shared ownership matter.
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
  `esp32-devkit-v4-game-board` hardware.
- The workshop product shape is now simpler and single-sourced in-repo: the
  v4 board base image is the canonical demo-board source, `workshop/pong.frothy`
  is exported from that base image, `frothy doctor` no longer treats source-build
  tools as attendee blockers, and the manual release workflow no longer
  promises an attendee firmware artifact that this tranche does not publish.
- Board-selected build input is now truthful again: host and ESP-IDF builds
  resolve board-owned extra C sources from the selected board declaration
  instead of carrying hardcoded TM1629 linkage in the global build lists, so
  the default v1 board path stays a plain base Frothy image unless a board
  explicitly declares more.
- The repo-checkout CLI selection path is now explicit again: `--target`
  means platform, `--board` means board, manifest projects ignore those flags
  with an explicit note instead of a hard error, legacy repo build/flash
  force-clean sticky target/board caches when selection flags are passed, and
  real-device proof now goes back through `frothy flash` rather than raw
  `idf.py` for the maintained repo-side flash path.
- The attendee-facing naming and recovery story is now explicit on the
  maintained Frothy path: Frothy owns the product/docs/editor/install identity,
  the default CLI home is `~/.frothy` with `FROTHY_HOME` override, Frothy now
  creates that home on demand instead of consulting legacy `~/.froth`,
  whole-file editor send attempts control `reset` before replay and marks the
  session degraded when the user explicitly chooses `Send Anyway` after reset is
  unavailable, and the control proof ladder now re-checks recovery on the real
  ESP32 path.
- `esp32-devkit-v1` and `esp32-devkit-v4-game-board` remain accepted board
  models in the repo; the workshop promise is simply narrower and currently
  centered on the mounted preflashed v4 board.
- The workshop-operational slice is now concrete in-repo without widening the
  product surface: `README.md` points at one minimal front door,
  `docs/guide/Frothy_Workshop_Quick_Reference.md` keeps the in-room prompt and
  recovery path short, `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`
  freezes the promised clean-machine checklist,
  `boards/esp32-devkit-v4-game-board/WORKSHOP.md` holds the room-side kit and
  reflash card, and
  `docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md` carries the
  checked-in rehearsal status note plus the focused
  `sh tools/frothy/proof.sh workshop-v4 <PORT>` real-device proof command.
- The remaining manual workshop gates stay explicit: separate clean-machine
  passes on the promised platforms and physical room pack-out are still exit
  steps, not work that prose can claim complete; the focused v4 mounted-board
  smoke is now recorded separately from any broader room rehearsal.
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

- Workshop-operational closeout is now the active gate:
  clean-machine validation and room-side hardware/recovery prep still need to
  be executed, and one complete recorded rehearsal pass still needs to be
  captured; the focused v4 workshop-board hardware smoke is now recorded.
- The evaluator frame-arena ownership revisit is deferred: the explicit
  evaluator-frame-stack tranche is landed, and the remaining shared-ownership
  question does not block the maintained single-runtime path until Frothy
  intentionally grows multiple live runtime instances or another re-entrant
  evaluator owner.
- Workspace/image flow remains intentionally deferred and single-sourced in
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`.
- See `TIMELINE.md` for the live movable queue and
  `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md` for the
  rationale behind that order.
