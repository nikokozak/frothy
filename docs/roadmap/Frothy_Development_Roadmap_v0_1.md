# Frothy Development Roadmap

Status: Accepted working roadmap
Date: 2026-04-09
Depends on: `docs/spec/Frothy_Language_Spec_v0_1.md`
Supersedes: initial Frothy execution-plan draft

## 1. Purpose

This roadmap is the working plan for turning Froth into Frothy.

It is meant to be:

- executable,
- trackable,
- reviewable,
- and realistic for one developer plus an AI coding assistant.

This is not a vision memo.
It is a sequencing and control document.

## 2. Current State

This block is the live control surface for repo status.

Current milestone: `none`
Today's goal: keep the next-stage language-definition closeout and the landed control/runtime follow-ons truthful while leaving CLI naming alignment explicitly next
Next artifact: first CLI naming-alignment artifact across `README.md` and executable-adjacent tool surfaces
Blocked by: none
Next proof command: `make test-all && rg -n 'repo-local \`froth-cli\`' README.md && rg -n 'release-time \`froth\`' README.md && rg -n 'intended global \`frothy\`' README.md && rg -n 'CLI naming alignment' PROGRESS.md TIMELINE.md docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
Slip against plan: none; next-stage language-definition docs landed as a doc-only closeout, control-session slice 2 and bounded-memory hardening landed, and CLI naming alignment stays next
Cut candidate if slip persists: keep transitional `froth-cli` / release-time `froth` naming explicit in docs and tooling notes before touching binaries or release surfaces

## 3. Operating Rules

### 3.1 Design rules

Every milestone should be judged by four questions:

1. Does this keep the language core small?
2. Does this preserve the live image / persistence story?
3. Does this reuse working Froth substrate instead of rebuilding it?
4. Is this realistic inside the thesis timebox?

If the answer to any of these is no, the work should be cut, deferred, or
reframed.

### 3.2 Implementation rules

Bias toward:

- simplicity,
- efficiency,
- robustness,
- elegance,
- literal reuse of working code,
- and visible end-to-end progress.

Avoid:

- speculative abstractions,
- major frameworks,
- semantic drift before ADRs,
- and “we can optimize it later” designs that already violate the core model.

### 3.3 Tracking legend

Use these markers when updating the roadmap:

- `[ ]` not started
- `[~]` in progress
- `[x]` done
- `[!]` blocked or decision needed

### 3.4 Daily cadence

- update the current-state block once per work session
- only one milestone may be `[~]` at a time
- every `[~]` milestone must name one next artifact and one proof command
- every `[!]` milestone must name the blocking decision
- if a milestone slips beyond its max days, apply the cut ladder explicitly

## 4. End State For Frothy v0.1

Frothy v0.1 is considered real when all of these are true:

- [x] A host REPL can define `Code`, cells stores, and top-level values.
- [x] Redefining a top-level code slot changes behavior seen by old callers.
- [x] `save`, `restore`, and `wipe` work on the overlay image.
- [x] `words()` lists names, `see()` inspects overlay `Code`, and minimal
  `core()` output is available.
- [x] Ctrl-C cancels pending multiline input, interrupts running evaluation at
  safe points, and returns to a usable prompt.
- [x] A small ESP32 hardware sketch works end-to-end.
- [x] The public language model is no longer stack-centric.

## 5. Primary Workstreams

There are six workstreams.

### 5.1 Repo and release separation

Goal:

- prevent the Frothy fork from publishing as Froth by accident.

### 5.2 Language definition

Goal:

- lock the semantic core before implementation sprawls.

### 5.3 Runtime substrate adaptation

Goal:

- reuse slot table, heap, boot, snapshot, FFI, and transport infrastructure
  while replacing the language layer.

### 5.4 Language front end and evaluator

Goal:

- build parser, IR, tree-walk evaluator, and image semantics.

### 5.5 Persistence and inspection

Goal:

- preserve Froth’s strongest property: transparent persistent live images.

### 5.6 Hardware proof

Goal:

- prove the language on real device work, not just host demos.

## 6. Milestone Table

Assumption:
these target dates record the completed v0.1 path that began on 2026-04-09.
Post-M10 work is tracked below as a rolling follow-on queue rather than as new
date-bound milestones.

| ID | Status | Milestone | Target Start | Target End | Max Days | Primary Deliverable | Proof | Cut If Late |
|---|---|---|---|---|---:|---|---|---|
| M0 | `[x]` | Freeze direction | 2026-04-09 | 2026-04-09 | 0.5 | accepted spec + handoff + roadmap | doc review complete | cut nonessential prose |
| M1 | `[x]` | Fork hygiene | 2026-04-09 | 2026-04-10 | 1 | safe fork identity | release pipeline disabled | delay no code without this |
| M2 | `[x]` | ADR foundation | 2026-04-09 | 2026-04-11 | 1.5 | core ADR stack | ADR files landed | defer nonblocking ADRs |
| M3 | `[x]` | Parallel host scaffolding | 2026-04-10 | 2026-04-11 | 1 | Frothy shell builds beside Froth | host build succeeds | cut niceties, keep parallel shell |
| M3a | `[x]` | Device smoke | 2026-04-11 | 2026-04-11 | 0.5 | ESP32 Frothy shell stub boots | target reaches prompt | cut all nonessential device polish |
| M4 | `[x]` | Parser + IR | 2026-04-11 | 2026-04-12 | 1.5 | parser and canonical IR | parser tests pass | keep one-precedence operators only |
| M5 | `[x]` | Evaluator + stable rebinding | 2026-04-12 | 2026-04-14 | 2 | host evaluator with rebinding | eval tests pass | cut nonessential call flexibility |
| M6 | `[x]` | Cells stores | 2026-04-14 | 2026-04-15 | 1 | top-level `cells(n)` storage | cells tests pass | cut extra helpers, keep narrow storage |
| M7 | `[x]` | Snapshot format | 2026-04-15 | 2026-04-16 | 1 | save/restore/wipe | snapshot tests pass | cut extra inspection before persistence |
| M8 | `[x]` | Interactive profile | 2026-04-16 | 2026-04-17 | 1 | multiline REPL + interrupt + inspection | REPL smoke passes | make `core()` minimal |
| M9 | `[x]` | Board FFI surface | 2026-04-17 | 2026-04-18 | 1 | base-image bindings for hardware | `ctest -R frothy_ffi` | keep only minimal board surface |
| M10 | `[x]` | Hardware proof | 2026-04-18 | 2026-04-19 | 1 | blink + boot + cells sketch | `./tools/frothy/proof_m10_smoke.sh <PORT>` passes | cut breadth, keep three proof programs |

## 7. Detailed Milestones

### M0. Freeze Direction

Purpose:

- establish the current design as the working source of truth.

Checklist:

- [x] Finalize permanent Frothy spec
- [x] Absorb the Frothy handoff prompt into repo-control guidance
- [x] Finalize permanent Frothy roadmap
- [x] Confirm core design commitments:
  - one namespace of values
  - stable top-level slots
  - non-capturing `Code`
  - top-level-created cells stores
  - canonical IR persistence

Exit criteria:

- the spec, handoff prompt, and roadmap have each survived at least two review
  passes and together form the accepted temporary source of truth.

### M1. Fork Hygiene

Purpose:

- separate Frothy from Froth operationally before implementation deepens.

Checklist:

- [x] Create actual fork repo
- [x] Disable automatic release publishing in the fork
- [x] Rename release defaults in `tools/release-common.sh`
- [x] Update `tools/update-brew-formula.sh`
- [x] Update `tools/package-release.sh`
- [x] Update `.github/workflows/release.yml`
- [x] Rewrite install and release-identity notes while keeping local command names transitional

Exit criteria:

- There is no accidental path that publishes Frothy as Froth.

### M2. ADR Foundation

Purpose:

- turn the current design into explicit decisions before code drifts.

Required ADRs:

Must land before M3:

- [x] Frothy/32 runtime value representation
- [x] Stable top-level slot model
- [x] Non-capturing `Code` value model
- [x] Cells-store and persistent store profile
- [x] Canonical IR as persisted code form

Must land before M7:

- [x] Snapshot format and overlay walk rules

Must land before M8:

- [x] REPL / interrupt / boot behavior

Must land before M9:

- [x] Frothy FFI boundary

Must land during M1:

- [x] Release and packaging separation

Exit criteria:

- major runtime semantics are no longer implicit in temporary docs.

### M3. Parallel Host Scaffolding

Purpose:

- build Frothy beside Froth before removing anything.

Checklist:

- [x] Add Frothy source files beside existing Froth runtime
- [x] Keep existing host build green
- [x] Add build switches or targets for Frothy prototypes
- [x] Reuse `froth_slot_table`, `froth_heap`, boot and snapshot plumbing

Files landed:

- [x] `src/frothy_main.c`
- [x] `src/frothy_boot.h/.c`
- [x] `src/frothy_shell.h/.c`

Deferred follow-on files:

- `src/frothy_value.h/.c`
- `src/frothy_ir.h/.c`
- `src/frothy_parser.h/.c`
- `src/frothy_eval.h/.c`
- `src/frothy_repl.h/.c`
- `src/frothy_snapshot.h/.c`

Exit criteria:

- Host build contains a minimal parallel Frothy runtime shell.

### M3a. Device Smoke

Purpose:

- catch target-specific substrate reuse failures early.

Checklist:

- [x] ESP32 build includes Frothy shell stubs
- [x] boot path reaches prompt
- [x] transport still works
- [x] safe boot path still works

Deliverables:

- [x] target build logs
- [x] boot transcript
- [x] note of any target-size or boot-order issues

Verification:

- [x] `./tools/cli/froth-cli --target esp-idf build`
- [x] `make test` covers the host PTY safe-boot smoke
- [x] prompt transcript captured from target session

Exit criteria:

- target bring-up risk is no longer deferred to the end.

### M4. Parser + Canonical IR

Purpose:

- establish the actual language front end.

Checklist:

- [x] Parse top-level `name = expr`
- [x] Parse REPL expressions
- [x] Parse lexical blocks
- [x] Parse local `name = expr`
- [x] Parse `set place = expr`
- [x] Parse `fn(...) { ... }`
- [x] Parse `cells(n)` with positive integer literal enforcement
- [x] Parse indexing, named calls, `if`, `while`
- [x] Emit canonical IR directly
- [x] Do not add bytecode

Deliverables:

- [x] parser source files
- [x] IR source files
- [x] parser tests
- [x] IR golden fixtures

Verification:

- [x] golden parser tests for representative programs
- [x] pretty-printed IR stability tests
- [x] proof command: `ctest --test-dir build -R frothy_parser --no-tests=error`

Exit criteria:

- Small Frothy programs parse reproducibly to canonical IR.

### M5. Evaluator + Stable Rebinding

Purpose:

- execute the new language on host with stable-slot semantics baked in.

Checklist:

- [x] scalar value execution
- [x] lexical scopes
- [x] blocks yield values
- [x] application of `Code`
- [x] top-level rebinding
- [x] `set` on locals and top-level places
- [x] `if`
- [x] `while`
- [x] arithmetic and comparison
- [x] recoverable errors
- [x] minimal native `Code` application path works on host
- [x] callers observe new slot contents after redefinition

Deliverables:

- [x] evaluator source files
- [x] value and scope tests
- [x] rebinding tests
- [x] host eval smoke script

Verification:

- [x] expression evaluation tests
- [x] scope tests
- [x] non-capturing nested `fn` rejection tests
- [x] redefine `pulse`, old caller sees new behavior
- [x] proof command: `ctest -R frothy_eval`

Exit criteria:

- host evaluator or test harness can run small Frothy programs with stable top-level rebinding.

### M6. Cells Stores

Purpose:

- add the minimum live slot-owned mutable indexed storage that makes hardware sketches real.

Checklist:

- [x] implement `cells(n)` in top-level rebinding only
- [x] allocate backing storage via adapted CellSpace
- [x] `nil` initialization
- [x] scalar-only element enforcement
- [x] bounds-checked reads and writes
- [x] extend evaluator with indexed reads and writes
- [x] support `set cellsExpr[index] = expr`

Deliverables:

- [x] cells runtime code
- [x] cells tests
- [x] sample program using cells
- [x] host smoke script for cells sample

Verification:

- [x] cells creation tests
- [x] bounds tests
- [x] cells value-kind rejection tests
- [x] define / write / read smoke
- [x] proof command: `ctest --test-dir build -R frothy_eval --no-tests=error`

Exit criteria:

- `cells(4)` can be created, written, read, and type/bounds errors are tested.

### M7. Snapshot Format

Purpose:

- restore the persistence story.

Checklist:

- [x] define Frothy snapshot payload layout
- [x] keep A/B storage and CRC path
- [x] serialize overlay slot bindings
- [x] serialize code objects via canonical IR
- [x] serialize text objects
- [x] serialize cells descriptors and payload
- [x] remap top-level names by symbol on restore
- [x] reject non-persistable state explicitly

Deliverables:

- [x] serializer / loader code
- [x] corruption tests
- [x] restore smoke script
- [x] internal native-call entry points kept narrow to `save()`, `restore()`, and `wipe()`

Verification:

- [x] define / save / reboot / restore / inspect
- [x] wipe returns to base image
- [x] corrupt snapshot fails safely
- [x] proof command: `ctest --test-dir build -R frothy_snapshot --output-on-failure`
- [x] final gate: `make test`

Exit criteria:

- a saved overlay with code, text, and cells restores after reboot, `wipe()`
  returns the system to base-only state, and corrupt snapshots fail safe.

### M8. Interactive Profile

Purpose:

- make the language feel live, not batch-compiled.

Checklist:

- [x] multiline input
- [x] prompt-never-dies recovery
- [x] Ctrl-C interrupt checks at loop back-edges and ordinary IR dispatch safe
  points
- [x] `words`
- [x] `see`
- [x] `core`
- [x] `slotInfo`
- [x] `boot` hook

Deliverables:

- [x] REPL integration
- [x] interrupt tests
- [x] inspection output for reference programs

Verification:

- [x] cancel pending multiline input with Ctrl-C
- [x] interrupt a bad `while`
- [x] `see` matches canonical semantics for overlay `Code`
- [x] `core` dumps minimal normalized IR
- [x] next REPL expression succeeds after interrupt
- [x] proof command: `make test`

Exit criteria:

- Ctrl-C cancels pending multiline input and interrupts running evaluation at
  safe points, including loop back-edges and ordinary IR dispatch, the next
  REPL expression succeeds, `words()` works from the host REPL, `see()`
  matches canonical semantics for overlay `Code`, `core()` exposes minimal
  normalized IR, `slotInfo()` reports binding metadata, and `boot` runs before
  the prompt when bound to `Code`.

### M9. Board FFI Surface

Purpose:

- make the board-facing hardware surface natural in Frothy.

Checklist:

- [x] define the v0.1 Frothy FFI shim
- [x] keep Froth registration substrate and current native entrypoints internally
- [x] expose base-image bindings as `Code` values
- [x] support `Int`, `Bool`, `Nil`, and `Text`
- [x] reject `Cells` handles at the shim boundary with a type mismatch
- [x] keep foreign handles non-persistable

Minimal bindings:

- [x] `gpio.mode`
- [x] `gpio.write`
- [x] `ms`
- [x] `adc.read`
- [x] `uart.init`
- [x] `uart.write`
- [x] `uart.read`

Deliverables:

- [x] FFI shim code
- [x] host smoke scripts
- [x] board binding table updates
- [x] one device input example
- [x] M9 closeout note

Verification:

- [x] FFI smoke tests on host
- [x] value passing tests
- [x] `Cells` arguments are rejected in the initial shim
- [x] host and target smoke pass for `gpio.mode`, `gpio.write`, `ms`, `adc.read`, and `uart.*`
- [x] proof command: `ctest -R frothy_ffi`

Exit criteria:

- host and ESP32 smoke tests pass for `gpio.mode`, `gpio.write`, `ms`, and one input primitive.

### M10. Hardware Proof

Purpose:

- prove the language on actual device work.

Checklist:

- [x] blink sketch
- [x] persistent boot behavior
- [x] one stateful sketch using cells

Deliverables:

- [x] blink program
- [x] boot persistence program
- [x] cells-based stateful demo
- [x] `tools/frothy/proof_m10_smoke.sh`
- [x] `tools/frothy/proof_m10_esp32_smoke.py`
- [x] board instructions or proof transcript

Suggested proofs:

- [x] LED blink with configurable period
- [x] saved boot behavior on power cycle
- [x] four-sample ADC capture stored in cells

Verification:

- [x] board proof scripts or manual transcripts captured
- [x] proof command: `./tools/frothy/proof_m10_smoke.sh <PORT>`

Exit criteria:

- ESP32 runs blink, boot persistence, and one cells-based stateful sketch end-to-end.

Closeout:

- Closed on 2026-04-12 with a checked-in transcript at
  `tools/frothy/m10_esp32_proof_transcript.txt`.
- The first live-board failure on 2026-04-12 was not an ESP-IDF visibility
  problem after all; the real runtime-side issue was avoidable ESP32 main-task
  stack pressure from Frothy shell-local line buffers, compounded by a device
  proof runner that assumed POSIX-style GPIO trace output.
- The fix stayed within the accepted boundary: move the shell line buffers out
  of the task stack, harden the device runner's prompt handling, and validate
  the actual ESP32-observable behavior without widening the Frothy board
  surface.

### Follow-On Queue. Transport, Runtime, Tooling, Workspace

Purpose:

- move from a proven `v0.1` core to a daily-usable Frothy system without
  widening semantics carelessly or rebuilding the old Froth host stack.

Operational label:

- `queued follow-on only`

Pending priority order:

1. CLI naming alignment
2. workspace and image-loading primitives

Already landed in this follow-on tranche:

1. urgent transport slice 1
2. runtime hardening
3. syntax tranche 1
4. VS Code plugin merge and alignment
5. immediate control hardening + `RESET`

#### Next-stage language definition

Status:

- landed on 2026-04-13 as a doc-only closeout on top of the frozen
  spoken-ledger syntax tranche 1 baseline

Why now:

- syntax tranche 1 improved readability, but did not close the main remaining
  expressiveness gaps
- the next language work should be written down before runtime scope widens
- modules and records need to be placed explicitly in Frothy's slot-and-image
  model before they leak in piecemeal through tooling or library experiments
- the language also needs an explicit in-language recovery story for ordinary
  library code, not only shell-level recovery at the top-level boundary

Approach:

- keep the accepted `v0.1` spec authoritative for current behavior
- keep spoken-ledger syntax tranche 1 explicit in the live draft:
  `name is expr`, `here name is expr`, `set place to expr`, `to` / `fn with`,
  bracket blocks, `:` calls plus `call expr with ...`, `repeat`, `when`,
  `unless`, `and`, `or`, prompt verbs, and prompt-only bare simple-call sugar
- keep that first slice parser-lowered onto the existing canonical IR,
  evaluator, and snapshot machinery
- keep records, modules, `cond`/`case`, Frothy-native `try/catch`, and
  restricted top-level binding/place designators in the draft, but do not
  widen runtime semantics for them yet
- keep recovery terminology honest: top-level shell/control/boot recovery is
  current reality, and the next in-language `try/catch` surface must stay
  explicitly non-transactional and narrower than Froth's old global catch
- do not widen runtime semantics from discussion alone; land the draft spec and
  ADR before implementation work

Closeout:

- `docs/spec/Frothy_Language_Spec_vNext.md` now treats spoken-ledger syntax
  tranche 1 as the frozen baseline and keeps only records, modules,
  `cond`/`case`, Frothy-native `try/catch`, and binding/place designators in
  draft
- `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md` now keeps `in prefix`
  draft-only, narrows ordinary-code `@name`, and stops narrating already-landed
  parser and shell work as pending
- Frothy ADR-114 records the chosen remaining draft shape
- no parser, evaluator, snapshot, or control-session semantics widened in this
  closeout

Proof:

- `make test-all && sh tools/frothy/proof_next_stage_docs.sh`

#### CLI naming alignment

Status:

- queued after the 2026-04-13 next-stage language-definition closeout

Why now:

- the language-definition docs are now tight enough that naming cleanup can
  proceed against a stable control surface
- repo-local `froth-cli`, release-time `froth`, and intended global `frothy`
  still read as separate truths in executable-adjacent docs and tooling notes
- the repo needs one explicit Frothy-first naming note before broader release
  or discovery cleanup continues

Approach:

- keep runtime, transport, and language semantics unchanged
- align repo-control docs and executable-adjacent notes around one explicit
  Frothy-first CLI identity
- preserve current binaries and proof paths where names remain transitional
- avoid implicit renames without the matching control-doc update

Next artifact:

- repo-control docs and executable-adjacent notes that explain the current
  `froth-cli` / release-time `froth` / intended global `frothy` split in one
  place

Proof:

- `make test-all && rg -n 'repo-local \`froth-cli\`' README.md && rg -n 'release-time \`froth\`' README.md && rg -n 'intended global \`frothy\`' README.md && rg -n 'CLI naming alignment' PROGRESS.md TIMELINE.md docs/roadmap/Frothy_Development_Roadmap_v0_1.md`

#### Immediate control hardening + `RESET`

Status:

- landed on 2026-04-12 on top of the merged VS Code helper/control baseline

Approach:

- centralize builtin metadata so base install, inspect behavior, and shell
  output suppression stop carrying separate truths
- stream or chunk `WORDS` and `SEE` replies instead of forcing them into one
  `FROTH_LINK_MAX_PAYLOAD` value frame
- make malformed control-session requests fail explicitly without widening the
  transport shape
- land `RESET_REQ = 0x07` with reset-to-base-image semantics, structured reset
  payloads, and stale-firmware fallback through `reset_unavailable`
- add the missing startup regression for bad-arity `boot` so the accepted
  `boot()` story stays explicit and non-bricking

Proof:

- `make test-all && make bench-frothy && tools/frothy/proof_f1_control_smoke.sh --host-only`
- targeted regressions cover bad-arity `boot` startup and oversized inspect
  replies

Did not widen in this slice:

- no daemon
- no PTY layer
- no shared-owner session broker
- `save` / `restore` / `wipe` / `core` / `slotInfo` remain helper-side `EVAL`
  wrappers
- no workspace/image-loading protocol

#### Urgent transport slice 1

Why first:

- future editor and CLI work should not grow on the inherited fragile control
  path
- the highest-risk gotchas live at session entry, output delivery, interrupt,
  detach, and prompt recovery boundaries
- one broad end-to-end slice can prove the architecture without rebuilding the
  old daemon stack

Approach:

- enter structured mode from the raw prompt with `.control`
- keep `.control` as shell metacommand, not persisted language surface
- make the session exclusive and framed until `DETACH` or port close
- support `HELLO`, `EVAL`, `WORDS`, and `SEE` in the first slice
- emit structured `OUTPUT`, `VALUE`, `ERROR`, `INTERRUPTED`, and `IDLE` events
- preserve raw `Ctrl-C` as the emergency interrupt path in both raw and control
  modes
- return cleanly to the raw prompt after `DETACH`, interrupt, or disconnect
- ship one tiny direct host tool that opens the port itself and exercises the
  session without any daemon

Why this slice is intentionally broad:

- `HELLO` proves session entry and framing
- `EVAL` proves the hot path the editor cares about
- `WORDS` and `SEE` prove that inspection actions do not need REPL scraping
- structured events prove that output and errors do not need mixed raw traffic
- `DETACH` plus prompt recovery prove the mode switch is reversible
- the tiny host tool proves the protocol is usable without hidden infrastructure

Proof:

- host proof: direct tool enters `.control`, runs `HELLO`, `EVAL`, `WORDS`,
  `SEE`, receives structured events, detaches, and sees the raw prompt again
- device proof: the same flow works on ESP32 without daemon ownership
- `Ctrl-C` interrupts evaluation inside the session and still returns to a
  usable prompt
- raw REPL attach still behaves normally when no control session is active

Do not add in this slice:

- no daemon
- no PTY passthrough
- no shared-owner session broker
- no image-loading or workspace protocol
- no attempt to multiplex a human console and structured control at the same
  time

#### Syntax tranche 1

Status:

- landed on 2026-04-12 on top of the runtime-hardening baseline, closing the
  parallel parser + shell rollout under the accepted `v0.1` surface

Why second:

- it is mostly parser and shell work
- it makes Frothy read like its accepted lexical model sooner
- it does not require new IR or evaluator semantics
- it should land immediately after the urgent transport slice, not after larger
  workspace work

Approach:

- add `here`
- add top-level `name(args) = expr`
- add top-level `name(args) { block }`
- add `boot { block }`
- add bare REPL command sugar for `words`, `save`, `restore`, `wipe`,
  `see @name`, `core @name`, and `info @name`
- keep the accepted `v0.1` forms valid during rollout

Proof:

- parser coverage for the new top-level forms and `boot { ... }`
- shell continuation coverage after `=`, trailing operators, and pending
  top-level heads
- `make test` and `ctest --test-dir build -R 'frothy_parser|frothy_eval'`

#### Runtime hardening

Status:

- landed on 2026-04-12 in this worktree with fixed
  `FROTHY_EVAL_VALUE_CAPACITY=256`, fixed `FROTHY_OBJECT_CAPACITY=128`,
  `make bench-frothy`, and checked-in notes at
  `docs/roadmap/F1_Runtime_Hardening_Benchmark_Notes.md`
- this slice bounds evaluator scratch and object/free-span metadata only; text
  bytes and cloned IR program bodies remain separate bounded-memory follow-on
  work

Why third:

- current eval still pays obvious hot-path costs
- the next performance pass should be measured, not guessed

Approach:

- check in a narrow benchmark harness for arithmetic, call dispatch, slot
  access, snapshot save/restore, and narrow FFI loops
- remove per-call heap churn where possible by reusing call frames and argument
  buffers under explicit fixed capacities instead of elastic growth
- measure before and after every hot-path change
- keep the tree-walk evaluator and current value model while hardening

Proof:

- new benchmark target runs on host
- existing eval, snapshot, and FFI tests remain green
- no benchmark work widens the public semantic surface

#### Local helper baseline

Why next after the landed hardening slice:

- the merged VS Code plugin now proves the helper/control path, but the helper
  is still too editor-owned
- flashing, new-project creation, and file send/apply should sit on the landed
  direct-control path rather than revive old daemon assumptions
- CLI and editor flows should converge on one maintained manager/session stack

Approach:

- reuse the landed `frothycontrol` manager/session surface across helper and
  CLI workflows
- support flashing, new-project scaffolding, and file send/apply workflows
- keep it host-local and direct-session-first
- avoid hidden background services or ownership brokers

Proof:

- helper can flash a target, scaffold a minimal project, and send/apply a file
  through a checked-in smoke path
- helper works on host without requiring the old Python session layer
- one maintained smoke covers helper plus kernel control together

#### VS Code plugin merge and alignment

Status:

- landed on 2026-04-12 on the maintained helper-owned control-session path

Approach:

- merge the local VS Code plugin work onto the single-owner control path
- keep helper and plugin responsibilities explicit instead of reintroducing a
  daemon split
- use the plugin merge to validate the helper command surface and control-event
  shape

Proof:

- the merged plugin can drive the maintained helper/control surface on host
- no mixed raw/framed ownership or daemon broker is reintroduced

#### Workspace and image-loading primitives

Why after helper broadening:

- Frothy needs a small library story, but not before the current control path,
  helper surface, and editor integration are stable

Approach:

- load or apply named slot bundles / IR capsules into the live image
- make FFI requirements visible before load
- keep the persisted unit aligned with stable slots and canonical IR

Do not build yet:

- no package registry
- no background daemon
- no PTY passthrough layer
- no type system or generalized module system

## 7. Suggested Execution Order

The critical path is:

1. M0
2. M1 + M2
3. M3
4. M3a
5. M4
6. M5
7. M6
8. M7
9. M8
10. M9
11. M10
12. follow-on queue: urgent transport slice 1, runtime hardening, syntax
    tranche 1, VS Code plugin merge/alignment, and immediate control hardening
    plus `RESET` landed; local helper broadening next, then
    workspace/image-loading primitives

Do not swap M7 and M9 unless forced.
Persistence is too central to Frothy’s identity to leave until the end.

## 8. Timebox Cut Ladder

If milestones slip, cut in this order:

1. If M4 slips:
   - keep one-precedence operators only
   - cut parser niceties before touching semantics
2. If M5 slips:
   - keep named-call-only `v0.1`
   - cut nonessential introspection before touching rebinding
3. If M7 slips:
   - cut extra inspection polish before cutting persistence
4. If M9 slips:
   - keep only `gpio.mode`, `gpio.write`, `ms`, and one input primitive
5. If M10 slips:
   - keep exactly the three proof programs and cut all demo breadth
6. If the follow-on queue slips:
   - keep the urgent transport slice and the landed runtime baseline
   - defer helper polish and workspace/image-loading work before reopening
     transport or runtime scope
7. If transport work slips:
   - keep raw REPL plus a short-lived direct session tool
   - do not add daemon, PTY, or shared-owner machinery as a stopgap

## 9. Risk Register

| Risk | Trigger | Impact | Immediate mitigation | Fallback cut | Review date |
|---|---|---|---|---|---|
| R1 Parser/IR delay | M4 not complete by day 2 | blocks all runtime work | keep grammar minimal | cut parser niceties | Gate B |
| R2 Evaluator churn | M5 needs refactor for rebinding | burns time twice | keep stable-slot semantics inside M5 from the start | cut nonessential call flexibility | Gate B |
| R3 Cells bloat | M6 grows beyond fixed top-level storage | semantic sprawl | keep `cells(n)` top-level-only | cut helper surface | Gate C |
| R4 Snapshot rewrite overruns | M7 not done on time | loses Frothy identity | keep walk shallow and slot-owned | cut extra inspection polish | Gate C |
| R5 FFI drift | M9 starts redesigning native ABI deeply | late subsystem rewrite | reuse current Froth entrypoints internally | postpone value-oriented native ABI cleanup | Gate D |
| R6 Fork identity tangle | M1 incomplete when coding starts | accidental release confusion | block deeper implementation until fixed | stop release work entirely | Gate A |
| R7 Syntax rollout drifts into semantics churn | vNext work starts inventing new execution rules | reopens the accepted core | keep rollout as parser/shell desugaring only | stop after syntax tranche 1 | Gate D |
| R8 Transport replacement grows daemon features back | host work starts recreating shared ownership or mux logic | repeats Froth fragility in smaller clothes | keep single-owner direct session per ADR-110 and the urgent slice scope | fall back to raw REPL plus direct scripted control | Gate D |

## 10. Review Gates

Before moving past these points, pause and review.

### Gate A: After M2

Questions:

- is the core still small?
- are the ADRs enough to constrain the runtime?

### Gate B: After M5

Questions:

- does the evaluator still feel elegant?
- has `Code` remained non-capturing and comprehensible?

### Gate C: After M8

Questions:

- does persistence remain transparent?
- can `see` and snapshot format survive later optimization changes?

### Gate D: After M10 and the first follow-on tranche

Questions:

- does the urgent transport slice already feel smaller and safer than the
  inherited control path?
- does the new syntax make Frothy read like its accepted model without adding
  semantics churn?
- are hot-path costs now measured and visibly reduced?
- is the new control transport staying smaller and more legible than inherited
  Froth's daemon and mux stack as tools start to use it?

## 11. Immediate Follow-On Steps

If implementation resumed today, the next steps should be:

- [x] land urgent transport slice 1 from ADR-110: `.control`, `DETACH`,
  `HELLO`, `EVAL`, `WORDS`, `SEE`, and structured events
- [x] ship one tiny direct host tool that opens the serial port itself, enters
  control mode, runs the first slice commands, and exits cleanly
- [x] add host and device smoke coverage for session entry, interrupt, detach,
  and prompt recovery
- [x] land syntax tranche 1 exactly as described in
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`
- [x] add a checked-in benchmark target and record current arithmetic, call,
  slot, snapshot, and FFI baselines
- [x] remove per-call heap churn in eval without changing the semantic model
- [x] prove that raw REPL and structured control can coexist as separate modes
  without daemon ownership
- [x] merge the local VS Code plugin work onto the maintained helper and direct
  control surface
- [x] centralize builtin metadata so base install, inspect behavior, and shell
  output suppression share one source of truth
- [x] stream or chunk `WORDS` and `SEE` replies so inspect no longer depends on
  one payload-sized frame
- [x] make malformed control-session requests fail explicitly without widening
  the transport shape
- [x] land `RESET_REQ = 0x07` with reset-to-base-image semantics on the direct
  control path
- [x] add the missing bad-arity `boot` startup regression
- [x] broaden the local helper beyond editor-owned workflows: flashing,
  new-project creation, shared CLI/session reuse, and file send/apply polish
- [x] add one maintained helper-plus-kernel end-to-end smoke on top of the
  landed direct control path, with final hardware confirmation tracked in the
  current-state block
- [x] land the next-stage language-definition docs for records, module slot
  grouping, `cond`/`case`, Frothy-native `try/catch`, restricted
  binding/place designators, and the explicit recovery-boundary story before
  widening runtime semantics again
- [ ] align repo-local `froth-cli`, release-time `froth`, and intended global
  `frothy` naming notes before broader discovery and release cleanup
- [ ] design slot-bundle / IR-capsule loading only after the helper and editor
  surfaces are stable
