# Frothy Agent Guide

## Purpose

This file is stable repo guidance for agents working in Frothy.

- Use it for mission, trust order, workflow, and engineering standards.
- Keep fast-moving status out of this file. Status belongs in the roadmap
  current-state block, `PROGRESS.md`, and `TIMELINE.md`.

## Mission

Frothy is a small live lexical language for programmable devices.

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
- Build Frothy beside inherited code first. Do not rip out the old runtime
  before the replacement path exists and proves itself.

## Session Start

At the start of a session, or whenever context feels stale:

1. Read `docs/spec/Frothy_Language_Spec_v0_1.md`.
2. Read `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`.
3. Read relevant Frothy ADRs in `docs/adr/100-*.md`.
4. Read `PROGRESS.md` and `TIMELINE.md`.
5. State the active milestone, blockers, next artifact, and proof command from
   the roadmap current-state block.

## Engineering Baseline

The codebase should read like a strong small open-source systems library.

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
- prefer additive migration beside inherited Froth substrate before deletion

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

Validation must stay proportional to the change.

- Add tests and proofs only when they protect basic user-visible behavior,
  likely regressions, or clear data-loss boundaries.
- Prefer one good check at one layer over multiple overlapping checks for the
  same behavior.
- Keep docs and control-surface proofs lightweight and direct. Prove the live
  snapshot is truthful; do not build wording-police harnesses unless wording is
  itself the contract.
- If a test or proof failure would not correspond to a meaningful product
  failure, the check is probably too heavy for the current repo stage.

## Workflow Expectations

- Keep the roadmap current-state block truthful. It is the live control surface.
- Update `PROGRESS.md` after meaningful work lands.
- Update `TIMELINE.md` when milestone status, target dates, or slips change.
- Keep README and repo-control docs aligned with actual repo policy.
- At the end of a tranche, run enough review and validation to catch major
  issues in the changed surface. Stop when core behavior is covered and the
  remaining risk is normal, not theoretical.
- Do not pile on overlapping review loops, test matrices, or proof scripts once
  the tranche is already well covered.
- Keep commit messages terse.
- Do not add `Co-Authored-By` lines.

## What Not To Do

- Do not guess at Frothy semantics.
- Do not let inherited Froth docs silently override Frothy control docs.
- Do not treat old Froth ADR-054 / ADR-055 / ADR-056 sequencing as the Frothy
  plan.
- Do not hide hot-path allocation behind helper layers.
- Do not start deleting inherited substrate before the parallel Frothy path
  exists.
- Do not build multiple assurance layers for the same behavior without a clear
  product need.
- Do not turn low-probability or compatibility-only edges into large permanent
  maintenance surfaces unless the user explicitly wants that tradeoff.
