# Frothy Progress

*Last updated: 2026-04-13*

This file is the thin repo-local execution journal for Frothy.
The current-state block in
`docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
remains the authoritative live control surface.

If this file disagrees with the accepted Frothy spec, ADRs, or roadmap, this
file is wrong.

## Current Control Snapshot

- Active milestone: `[~] Next-stage language definition`
- Blocked by: none
- Next artifact: tighten the remaining next-stage draft around records,
  modules, `cond`/`case`, `try/catch`, and binding/place values on top of
  the frozen spoken-ledger tranche 1 baseline
- Next proof: `make test-all && sh tools/frothy/proof_next_stage_docs.sh`

## Recent Landed Work

- The 2026-04-13 transient work-buffer hardening tranche is landed:
  shell multiline accumulation now stays inside one fixed
  `FROTHY_SHELL_SOURCE_CAPACITY` buffer, parser and canonical-IR build
  vectors now stop at explicit compile-time caps instead of growing with
  `realloc`, and snapshot save/restore now borrow one codec-owned payload and
  metadata workspace instead of allocating transient payload, symbol, and
  object-anchor buffers per call. The focused parser, shell, eval, snapshot,
  and FFI gate is green, and `frothy_runtime_bench` now also pins a
  representative parse case.
- Spoken-ledger syntax tranche 1 is landed on 2026-04-13:
  the parser now accepts `name is expr`, `here name is expr`,
  `set place to expr`, bracket blocks with `;`, `to` / `fn with`,
  `:` calls plus `call expr with ...`, `repeat`, `when`, `unless`, `and`,
  and `or`, while the shell now carries the matching continuation rules,
  `show` / `info` / `remember`, and prompt-only simple-call sugar. The first
  slice still lowers onto canonical IR without widening the evaluator or
  snapshot format, and `show` now prefers a normalized source-like code
  surface while `core` stays canonical.
- Spoken-ledger syntax tranche 1 is now the frozen baseline for the next
  process. Follow-on next-stage work should start from this proved surface
  rather than mixing new semantic widening into further tranche hardening.
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
  remaining persistent payload surface: text bytes and cloned IR program
  bodies. Parser growth, snapshot codec work buffers, and shell source
  accumulation are now bounded in tree.
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
  slice is now tighter: spoken-ledger syntax tranche 1 is in tree, while
  records, modules, `cond`/`case`, Frothy-native `try/catch`, and
  binding/place values remain the draft-only next-stage design surface.
- Workspace/image-loading primitives stay deferred until the control surface,
  helper surface, and editor integration story are smaller and clearer.

## Next Artifact

- Tighten the remaining next-stage draft around records, modules,
  `cond`/`case`, `try/catch`, and binding/place values on top of the frozen
  spoken-ledger tranche 1 baseline.

## Next Proof

- `make test-all && sh tools/frothy/proof_next_stage_docs.sh`
