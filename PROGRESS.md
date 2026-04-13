# Frothy Progress

*Last updated: 2026-04-12*

This file is the thin repo-local execution journal for Frothy.
The current-state block in
`docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the authoritative live control surface.

If this file disagrees with the accepted Frothy spec, ADRs, or roadmap, this
file is wrong.

## Current Control Snapshot

- Active milestone: `[~] Next-stage language definition`
- Blocked by: none
- Next artifact: align the control docs with the accepted ADR stack, then
  advance `docs/spec/Frothy_Language_Spec_vNext.md` as the active next-stage
  language-definition draft
- Next proof: `make test-all && rg -n "Next-stage language definition|draft next-stage spec plus ADR|ADR-112|ADR-113" docs/roadmap/Frothy_Development_Roadmap_v0_1.md PROGRESS.md TIMELINE.md docs/spec/Frothy_Language_Spec_vNext.md docs/adr/112-next-stage-language-growth-and-recovery-boundary.md docs/adr/113-manifest-owned-project-target-selection.md`

## Recent Landed Work

- The 2026-04-12 real-device F1 closeout is confirmed:
  the checked-in `tools/frothy/proof_f1_control_smoke.sh <PORT>` path now
  succeeds end to end on target hardware through public-CLI
  `new --target -> build -> flash -> apply runtime ->
  reconnect/verify`, so helper proof coverage is no longer a blocker.
- The 2026-04-12 project-target authority slice is landed:
  `froth new` still defaults to posix, but `froth new --target <board>` may
  seed the new manifest for hardware scaffolds while existing project
  `build`/`flash` commands reject `--target` and keep `froth.toml` as the sole
  target authority. Frothy ADR-113 records that split.
- The 2026-04-12 lean F1 proof-coverage slice is landed:
  CLI integration now drives the real `froth tooling control-session`
  entrypoint against a local Frothy runtime, and
  `tools/frothy/proof_f1_control_smoke.sh <PORT>` now extends past the
  existing control smoke into a public-CLI temp-project
  `new -> build -> flash -> apply runtime -> reconnect/verify` path.
- The 2026-04-12 interrupt-regression proof slice is landed:
  the shared `frothycontrol` smoke now covers raw multiline cancel before
  `.control` and raw `Ctrl-C` while a control session is idle, local-runtime
  integration tests pin those two handoff boundaries directly, and the ESP32
  M10 proof now exercises UART `Ctrl-C` during the safe-boot window before the
  final cells-and-cleanup phase.
- The 2026-04-12 top-level mutation parser gap is landed:
  `frothy_parser` now accepts top-level `set place = expr`, so prompt-level
  `set unit = 2` and `set frame[0] = 5` match the accepted mutation model,
  with parser/eval coverage and the accepted `v0.1` spec grammar aligned to
  the implementation.
- The 2026-04-12 shell interrupt fix is landed:
  `frothy_shell` now treats a raw `Ctrl-C` byte the same way it treats a
  signaled interrupt while reading a line, so pending multiline input drops
  back to the primary prompt on raw serial paths again. The maintained
  `proof-ctrlc` helper now covers that raw-byte continuation case in addition
  to the signal-driven host path.
- Syntax tranche 1 is landed on 2026-04-12:
  `frothy_parser` accepts block-local `here`, top-level
  `name(args) = expr`, top-level `name(args) { block }`, and `boot { block }`,
  while `frothy_shell` now carries the matching continuation logic and bare
  command sugar for the accepted REPL surface. The refreshed M8 REPL and
  inspect smokes prove the merged shell path on top of the runtime-hardening
  baseline.
- The 2026-04-12 runtime hardening slice is landed:
  `frothy_runtime_bench` and `make bench-frothy` are checked in,
  `FROTHY_EVAL_VALUE_CAPACITY=256` and `FROTHY_OBJECT_CAPACITY=128` now bound
  evaluator scratch and runtime object metadata, `src/frothy_eval.c` uses a
  reset-safe arena instead of per-call heap churn for locals and arg buffers,
  and `docs/roadmap/F1_Runtime_Hardening_Benchmark_Notes.md` records the
  before/after host numbers.
- M9 is closed with the checked-in board-FFI closeout note and the narrow
  shipped Frothy hardware surface.
- M10 is closed with the checked-in ESP32 proof bundle and transcript.
- The 2026-04-12 urgent transport slice 1 is landed:
  raw prompt `.control` now enters Frothy-owned structured mode with
  `HELLO`, `EVAL`, `WORDS`, `SEE`, `DETACH`, structured
  `OUTPUT` / `VALUE` / `ERROR` / `INTERRUPTED` / `IDLE`, and prompt recovery
  without daemon ownership.
- The direct control proof tool is checked in under
  `tools/cli/internal/frothycontrol` and exposed through
  `froth tooling control-smoke`.
- The maintained test surface is now explicit:
  `make test` is the fast local gate, `make test-all` is the exhaustive local
  gate, and the heavy lanes are split into reusable runner-owned targets.
- The maintained host test path no longer depends on Python session helpers,
  and the maintained `make` targets or CI do not depend on `idf.py`.
- Frothy control smoke is now wired into `tools/frothy/proof.sh`, with host
  proof always exercised and device proof available through the same driver
  when a serial port is provided.
- The 2026-04-12 phase 2 sanity pass is landed:
  the tracked CLI SDK mirror is gone, the CLI now embeds a generated archive
  payload via `make sdk-payload`, Frothy proof runs through the single
  `tools/frothy/proof.sh` driver, and Frothy base-image assembly is
  centralized in `src/frothy_base_image.c`.
- The CLI now embeds a generated SDK archive payload instead of a tracked
  mirrored kernel tree, and build/test/release flows generate that payload
  through `make sdk-payload`.
- Frothy shell proofs now run through `tools/frothy/proof.sh`, and Frothy
  base-image assembly has one explicit owner in `src/frothy_base_image.c`
  rather than being split across snapshot glue and boot setup.
- The repo control surface is now Frothy-first:
  `README.md`, `Makefile`, and the kernel proof path point at `build/Frothy`
  and explicit Frothy vs legacy test buckets.
- Dead milestone residue and stale review artifacts were removed, and the live
  TM1629 fixture moved out of `tmp/` into `tests/legacy/tm1629d`.
- The 2026-04-12 repo-prune tranche is landed:
  the maintained tree no longer builds the legacy `Froth` host runtime, the
  CLI and build path run through direct Frothy control instead of the daemon
  stack, legacy shell tests and fixtures are gone from the active repo
  surface, and old Froth docs now live under `docs/archive/`.
- Frothy shell idle reporting is now tied to the actual primary-prompt state,
  and snapshot internals are split between public entrypoints and codec
  machinery without changing payload format.

## Active Follow-On Note

- The dated milestone ladder stops at M10. Future work is a rolling follow-on
  queue, not fake date-bound polish.
- The urgent transport slice is landed. The first real editor or CLI
  iteration loop should build on the direct single-owner control path, not on
  the inherited fragile control surface.
- The merged VS Code helper/editor path is now part of the baseline, not an
  incoming follow-on.
- Syntax tranche 1 is now closed on top of the landed runtime baseline, and
  the immediate control hardening plus reset slice is closed on top of that.
- Runtime hardening is now landed. The next bounded-memory follow-on is the
  remaining dynamic payload and work-buffer surface: text bytes, cloned IR
  program bodies, parser growth, snapshot codec payload buffers, and shell
  source accumulation.
- Transport work should simplify ownership and framing. ADR-110 replaces the
  inherited daemon-plus-mux direction with a direct single-owner control
  session.
- The helper broadening slice is now landed in tree:
  shared CLI/session reuse plus flashing, new-project creation, and file
  send/apply workflows all sit on the maintained direct-control path.
- The helper proof surface is now checked in:
  host integration covers the real `tooling control-session` entrypoint, and
  the maintained F1 control smoke grows a public-CLI flash/apply/reconnect
  device path. That checked-in device proof is now confirmed on target
  hardware.
- The next language-definition artifact after the active helper broadening
  slice is tightening the draft next-stage spec around the already-accepted
  ADR-112 boundary: counted iteration, fixed-layout records, module images
  built from stable slots, Frothy-native `try/catch`, and the current
  recovery-boundary story that explains why Frothy still has no Froth-style
  language-level global `catch`.
- Workspace/image-loading primitives stay deferred until the control surface,
  helper surface, and editor integration story are smaller and clearer.

## Next Artifact

- Confirm the checked-in helper-owned flash/apply proof on real hardware,
  then close F1 and move to the queued next-stage language-definition
  artifact.

## Next Proof

- `make build`
- `ctest --test-dir build --output-on-failure -R '^frothy_(parser|eval|snapshot|ffi)$'`
- `cd tools/cli && go test ./internal/frothycontrol ./cmd`
- `cd tools/vscode && npm test`
- `./build/frothy_runtime_bench`
- `sh tools/frothy/proof_f1_control_smoke.sh --host-only`
- `sh tools/frothy/proof_f1_control_smoke.sh <PORT>`
- `tools/frothy/proof_m8_repl_smoke.sh`
- `bash tools/frothy/proof_m8_inspect_smoke.sh`
