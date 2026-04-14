# Frothy Agent Guide

## Purpose

This file is stable repo guidance for agents working in Frothy.

- Use it for mission, trust order, workflow, and engineering standards.
- Keep fast-moving status out of this file. Status belongs in the roadmap
  current-state block, `PROGRESS.md`, and `TIMELINE.md`.

## Mission

Frothy is a small live lexical language for programmable devices.

Frothy is embedded-device-first.
Host/local/POSIX paths exist to support development speed and tooling, but they
are secondary niceties, not the product center.

It keeps Froth's strongest substrate traits:

- stable top-level identity
- live image semantics
- save / restore / wipe
- interruptibility and safe boot
- transparent inspection
- host-first development with ESP32-class targets

It does not preserve Froth's old stack-visible user model as product policy.

## Authority Order

When sources disagree, use this order:

1. `docs/spec/Frothy_Language_Spec_v0_1.md`
2. Frothy ADRs in `docs/adr/100-*.md`
3. the current-state block in `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
4. `PROGRESS.md`
5. `TIMELINE.md`
6. current implementation
7. inherited Froth docs and ADRs, but only when intentionally reused as
   substrate reference

If there is tension between these sources, name the exact files and sections in
conflict before changing code or docs.

## Froth Versus Frothy

The original Froth repo at `/Users/niko/Developer/Froth` is reference material.

- Froth specs, ADRs, AGENTS guidance, progress notes, and roadmap are not
  authoritative for Frothy semantics or Frothy priorities.
- Froth source and docs are authoritative only where Frothy explicitly reuses
  substrate behavior or workflow constraints.
- Do not continue the old Froth milestone plan.
- Build Frothy beside inherited code first when a replacement path does not
  exist yet.
- Once the Frothy-owned replacement path exists and proves itself, delete the
  legacy path promptly instead of carrying both.

## Working Set

Do not force the same startup ritual on every task.

For targeted work:

1. Read this file.
2. Read the roadmap current-state block in
   `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`.
3. Read only the authority docs directly relevant to the task.

Examples:

- semantics, persistence, inspection, or recovery work:
  `docs/spec/Frothy_Language_Spec_v0_1.md` plus the relevant Frothy ADRs
- FFI or board-surface work:
  `docs/adr/108-frothy-ffi-boundary.md` and
  `docs/roadmap/Frothy_M9_Board_FFI_Closeout.md`
- forward-direction language work:
  `docs/spec/Frothy_Language_Spec_vNext.md`,
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, and the relevant
  Frothy `100`-series ADRs
- workspace/image-flow work:
  `docs/adr/115-first-workspace-image-flow-tranche.md` and
  `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`
- repo-control or priority-surface work:
  `PROGRESS.md`, `TIMELINE.md`, and
  `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`

Run the broader read pass when:

- the task is broad or open-ended
- context is stale
- the task touches semantics, persistence, FFI, roadmap intent, or repo policy
- you are being asked to audit or repair the control surface itself

Broader read pass:

1. `docs/spec/Frothy_Language_Spec_v0_1.md`
2. `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
3. relevant Frothy ADRs in `docs/adr/100-*.md`
4. `PROGRESS.md` and `TIMELINE.md`
5. `docs/spec/Frothy_Language_Spec_vNext.md` and
   `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md` if the task concerns
   explicit future direction

State the active milestone, blockers, next artifact, and proof command from
the roadmap current-state block when asked, when context is stale, or when
editing the control docs.

## Engineering Baseline

The codebase should read like a strong small open-source systems library.

Design priority order:

1. real embedded-device behavior
2. language/runtime integrity on the maintained hardware path
3. host/local convenience

If those priorities conflict, favor the embedded-device path.

Quality references:

- SQLite
- Lua
- musl libc
- libuv

Emulate their virtues, not their formatting.

Required habits:

- prefer short functions with one clear job
- keep headers small and declarative
- make invariants explicit
- keep ownership and lifetime obvious for every heap object
- require clear `init`, `reset`, and `free` stories
- keep error paths visible and boring
- preserve obvious control flow over abstraction layering
- use structs and enums before macros
- use macros sparingly and only when they remove real repetition
- keep the public C surface small
- separate platform code from runtime/language code cleanly
- avoid hidden allocation in hot execution paths
- keep inherited Froth substrate only while it is the sole live implementation
  or an accepted temporary shim; once the Frothy-owned path is real and
  proved, prune the legacy surface aggressively

Bias toward:

- simplicity
- efficiency
- robustness
- elegance
- literal reuse of working substrate where it helps

Avoid:

- speculative abstractions
- framework creep
- semantic drift before ADRs
- mixed naming and style conventions across files

## Documentation Discipline

Reliance on local documentation is mandatory.

- Read the permanent Frothy spec and relevant Frothy ADRs before answering
  questions about semantics, persistence, inspection, FFI, or roadmap intent.
- Treat `docs/spec/README.md` and `docs/adr/README.md` as part of the authority
  map.
- Treat `docs/spec/Frothy_Language_Spec_vNext.md`,
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`, and
  `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md` as
  forward-direction and queue-shaping docs only. They do not override the
  accepted `v0.1` contract or accepted ADRs.
- Use inherited Froth material through
  `docs/reference/Froth_Substrate_References.md`, not by assuming old Froth
  policy still applies.

## ADR Discipline

Create or update a Frothy ADR before or alongside changes that affect:

- language semantics
- runtime value representation
- persistence format or restore rules
- public FFI surface
- build or release policy
- inspection behavior
- major repo workflow or authority boundaries

Do not add new Frothy decisions to the inherited `001`-`060` Froth ADR stack.
Use the Frothy `100`-series.

## Validation

Default validation paths:

- `cmake -S . -B build && cmake --build build`
- `make test`

Real-device validation policy:

- Before sign-off on any task in this repo, run at least one proof on a real
  connected ESP32-class target.
- For device-facing work, that proof must exercise the changed surface
  directly.
- For host-only, docs, control-surface, or refactor work, a real-device sanity
  proof is still mandatory before sign-off so the maintained hardware path is
  never left unexercised.
- Local POSIX or `connect --local` runs are supplementary only. They do not
  count as final validation and must not be presented as if they prove
  real-device behavior.
- If a real-device proof could not be run, say so explicitly and do not imply
  the work is fully validated.

For docs and control-surface changes, default to focused grep or checked-in
sanity scripts that prove the intended authority split and live queue are
actually true. Do not default to heavier build or test runs unless the change
genuinely needs them.

## Workflow Expectations

- Keep the roadmap current-state block truthful. It is the live control surface.
- Update `PROGRESS.md` after meaningful work lands.
- Keep `PROGRESS.md` short and operational. It is not a changelog.
- Update `TIMELINE.md` when milestone status or queue priority changes.
- Keep `TIMELINE.md` as a movable checkbox ledger. Reordering the queue is
  expected; if context would be lost, add or update a referenced doc rather
  than expanding the timeline into narrative sprawl.
- Keep README and repo-control docs aligned with actual repo policy.
- At every reasonable opportunity, prune legacy Froth code, naming, syntax,
  compatibility shims, docs, and architecture from the maintained repo
  surface. Do not keep stale parallel paths once the Frothy-owned replacement
  is working and proved.
- Treat real-device proof as higher priority than local convenience. Do not
  stop at POSIX-only validation for any task in this repo.
- Keep the repo and its docs honest about product priority: Frothy is for
  embedded devices first, and local/POSIX support is a secondary development
  aid.
- At the end of a tranche, run agent reviews repeatedly until they stop
  surfacing major issues and you are comfortable with the work. Do not treat a
  first green test pass or a single review round as tranche closeout.
- Keep commit messages terse.
- Do not add `Co-Authored-By` lines.

## What Not To Do

- Do not guess at Frothy semantics.
- Do not let inherited Froth docs silently override Frothy control docs.
- Do not treat old Froth ADR-054 / ADR-055 / ADR-056 sequencing as the Frothy
  plan.
- Do not hide hot-path allocation behind helper layers.
- Do not keep inherited substrate, naming, or compatibility layers around once
  the Frothy-owned replacement path exists and is proved.
- Do not treat local/POSIX behavior as equal evidence for real-device
  behavior, and do not let host niceties outrank the embedded-device path.
