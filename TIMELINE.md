# Frothy Timeline

*Last updated: 2026-04-13*

This file is the thin milestone ledger for Frothy.
The roadmap current-state block in
`docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the live control surface.

If this file and the roadmap disagree, the roadmap wins.

## Current Control Snapshot

- Active milestone: `none`
- Blocked by: none
- Next artifact: first workspace/image-loading design artifact for named slot
  bundles / IR capsules
- Next proof command: `make test-all && rg -n 'Next artifact: first workspace/image-loading design artifact for named slot' docs/roadmap/Frothy_Development_Roadmap_v0_1.md PROGRESS.md TIMELINE.md && rg -n 'bundles / IR capsules' docs/roadmap/Frothy_Development_Roadmap_v0_1.md PROGRESS.md TIMELINE.md && rg -n 'slot-bundle / IR-capsule loading' docs/roadmap/Frothy_Development_Roadmap_v0_1.md && rg -n 'Workspace/image flow' TIMELINE.md`

## Milestone Ledger

| ID | Status | Window | Primary Deliverable | Proof | Repo State |
|---|---|---|---|---|---|
| M0 | `[x]` | 2026-04-09 to 2026-04-09 | accepted spec + handoff + roadmap | doc review complete | Landed. Permanent Frothy control docs are in place. |
| M1 | `[x]` | 2026-04-09 to 2026-04-10 | safe fork identity | release pipeline disabled | Landed. Frothy release identity is separated from inherited Froth. |
| M2 | `[x]` | 2026-04-09 to 2026-04-11 | core ADR stack | ADR files landed | Landed. Frothy ADR-100 through ADR-108 define the accepted core. |
| M3 | `[x]` | 2026-04-10 to 2026-04-11 | Frothy shell builds beside Froth | host build succeeds | Landed. `Frothy` builds beside inherited `Froth`. |
| M3a | `[x]` | 2026-04-11 to 2026-04-11 | ESP32 Frothy shell stub boots | target reaches prompt | Landed. Checked-in prompt transcripts cover normal and safe boot. |
| M4 | `[x]` | 2026-04-11 to 2026-04-12 | parser and canonical IR | parser tests pass | Landed. Parser, fixtures, and canonical IR are in place. |
| M5 | `[x]` | 2026-04-12 to 2026-04-14 | host evaluator with rebinding | eval tests pass | Landed. Stable values are VM-owned and reclaimable. |
| M6 | `[x]` | 2026-04-14 to 2026-04-15 | top-level `cells(n)` storage | cells tests pass | Landed. `Cells` are live, narrow, and covered. |
| M7 | `[x]` | 2026-04-15 to 2026-04-16 | save/restore/wipe | snapshot tests pass | Landed. Overlay-only persistence is closed. |
| M8 | `[x]` | 2026-04-16 to 2026-04-17 | multiline REPL + interrupt + inspection | REPL smoke passes | Landed. Interactive profile is closed on host. |
| M9 | `[x]` | 2026-04-17 to 2026-04-18 | base-image bindings for hardware | `ctest -R frothy_ffi` | Closed with the explicit board-FFI closeout note. |
| M10 | `[x]` | 2026-04-18 to 2026-04-19 | blink + boot + cells sketch | `./tools/frothy/proof_m10_smoke.sh <PORT>` passes | Closed with the proof bundle and checked-in ESP32 transcript. |

## Current Follow-On Queue

- Operational label: `queued follow-on only`
- `Immediate control hardening + RESET`: landed on 2026-04-12. Builtin
  metadata now has one owner, `WORDS` / `SEE` stream across multiple value
  events, malformed bound-session requests fail explicitly, current firmware
  resets cleanly through the direct control path, and stale firmware still
  reports `reset_unavailable`.
- `VS Code plugin merge`: landed on 2026-04-12. The checked-in editor path now
  runs on the maintained helper-owned control session rather than a daemon
  split.
- `Local helper`: landed in tree on 2026-04-12. Shared CLI/session reuse plus
  flashing, new-project creation, and file send/apply now sit on the direct
  single-owner control path.
- `Helper proof coverage`: confirmed on target hardware on 2026-04-12. The
  checked-in `proof_f1_control_smoke.sh <PORT>` path now closes the public-CLI
  flash/apply/reconnect proof on device.
- `Spoken-ledger syntax tranche 1`: landed on 2026-04-13. `name is expr`,
  `here name is expr`, `set place to expr`, `to` / `fn with`, bracket
  blocks, `:` calls plus `call expr with ...`, `repeat`, `when`, `unless`,
  `and`, `or`, prompt verbs, and prompt-only simple-call sugar are now
  merged on top of canonical IR lowering, with refreshed parser/eval
  coverage plus M8 REPL and inspect smokes.
- `Spoken-ledger syntax tranche 1 baseline`: frozen on 2026-04-13. The next
  language-definition pass should treat that proved slice as fixed input and
  move the remaining design work in a separate process.
- `Next-stage language definition`: landed on 2026-04-13 as a doc-only
  closeout. The vNext spec, surface proposal, and Frothy ADR-114 now freeze
  spoken-ledger syntax tranche 1 as the baseline while keeping records,
  modules, `cond`/`case`, Frothy-native `try/catch`, and restricted
  binding/place designators as explicit draft-only work before runtime
  semantics widen again.
- `CLI naming alignment`: landed on 2026-04-13 as the first truthful
  docs/tooling-note artifact. `README.md`, repo-local build labels, release
  notes, and VS Code CLI discovery/config wording now explain the repo-local
  `froth-cli`, release-time `froth`, and intended global `frothy` split
  without renaming binaries, tarball contents, Homebrew install targets, or
  editor command ids.
- `Urgent transport slice 1`: landed on 2026-04-12. Raw prompt `.control`
  now enters Frothy-owned structured mode with `HELLO`, `EVAL`, `WORDS`,
  `SEE`, `DETACH`, structured `OUTPUT` / `VALUE` / `ERROR` /
  `INTERRUPTED` / `IDLE`, direct-tool proof, and prompt recovery without any
  daemon.
- `Runtime hardening`: landed on 2026-04-12. `frothy_runtime_bench` and
  `make bench-frothy` are checked in, evaluator scratch and object metadata
  now use fixed capacities, and the before/after host numbers live in
  `docs/roadmap/F1_Runtime_Hardening_Benchmark_Notes.md`.
- `Syntax tranche 1`: landed on 2026-04-12. `here`, top-level
  `name(args) = expr`, `name(args) { block }`, `boot { block }`, and the
  first accepted bare REPL command sugar are now merged on top of the runtime
  baseline that the spoken-ledger follow-on extends.
- `Transport simplification`: replace the inherited daemon and mixed-stream
  direction with the ADR-110 single-owner control session. Raw REPL stays raw;
  structured control gets explicit exclusive framing and event replies. After
  the urgent first slice, this means rounding out the remaining commands rather
  than reopening the transport shape.
- `Workspace/image flow`: now the next queued design follow-on after the
  local-helper broadening and CLI naming-alignment closeout. Add named slot
  bundles or IR capsules only once those surfaces are stable. Do not build a
  registry, PTY layer, or background service first.

## Slip Notes

- M9 remained active until its closeout note landed on 2026-04-12 even though
  most of the technical work was already present.
- The first M10 board run on 2026-04-12 exposed Frothy shell stack-use and
  device-runner assumptions before the same-day hardware transcript closed the
  milestone.
- The 2026-04-12 phase 2 sanity pass removed the tracked CLI SDK mirror,
  collapsed Frothy proof behind `tools/frothy/proof.sh`, and made Frothy
  base-image assembly explicit without changing semantics, snapshots, or the
  public FFI surface.
- The 2026-04-12 control-slice landing replaced the inherited daemon-first
  path for new Frothy tooling with a checked-in direct control session and a
  shared host plus device smoke driver.
- The 2026-04-12 runtime hardening slice split cleanly from syntax tranche 1:
  runtime budgeting and benchmarking landed in this worktree, while syntax
  rollout continues in parallel so parser and shell work does not obscure the
  evaluator and runtime proof path.
- The 2026-04-12 shell-tranche merge closed that split: syntax tranche 1 is
  now landed on top of the runtime-hardening baseline, so the follow-on queue
  can move to workspace / image-loading design instead of parallel syntax
  rollout.
- The next timeline adjustment moved workspace / image-loading back behind a
  narrower tightening pass plus real host tooling.
- The merged VS Code helper/editor path and the immediate control hardening
  plus reset slice are now landed on the same baseline, so the next timeline
  step is helper broadening and shared CLI/session reuse rather than editor
  merge or reset design work.
- The 2026-04-12 repo-prune tranche removed the legacy `Froth` host/runtime
  surface, daemon-first CLI path, and active-tree historical Froth doc sprawl,
  leaving a Frothy-only maintained build/test path plus explicit archives.
- The 2026-04-12 lean proof-coverage slice kept that surface small: one new
  CLI integration test for the real helper entrypoint and one extension of the
  maintained F1 device smoke for public-CLI flash/apply/reconnect proof.

## Current Rules

- Only one milestone may be `[~]` at a time.
- Every active milestone must name one next artifact and one proof command.
- The dated milestone ladder stops at M10; use the follow-on queue for current
  priority, not a fake dated milestone.
- Do not widen the board surface or add package machinery before syntax
  tranche 1, the immediate control/tooling slice, and the first
  workspace/image flow are defined against the landed runtime baseline.
