# Frothy ADR-107: Interactive Profile, Boot, And Interrupt

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 7.8, 8.1, 8.2, 8.3, 8.4
**Roadmap milestone(s)**: M2, M8
**Inherited Froth references**: `docs/spec/Froth_Interactive_Development_v0_5.md`, `src/froth_boot.c`

## Context

Frothy is not a batch compiler with a REPL bolted on. The interactive profile
is part of the product. The repo already has inherited boot, interrupt, and
host/device interaction substrate, but Frothy needs a clear accepted contract
for its own prompt behavior and inspection surface.

## Options Considered

### Option A: Keep the live prompt model as a first-class contract

Define REPL, multiline input, prompt recovery, boot hook, interrupt checks, and
inspection operations as required `v0.1` behavior.

Trade-offs:

- Pro: preserves Frothy's main experiential advantage.
- Pro: keeps persistence and inspection tied to everyday use.

### Option B: Treat the REPL as development-only scaffolding

Focus first on batch evaluation and defer interactive discipline.

Trade-offs:

- Pro: smaller early implementation.
- Con: loses the live-image identity Frothy is trying to keep.

### Option C: Leave interactive behavior mostly implementation-defined

Specify only enough to run code.

Trade-offs:

- Pro: gives implementation freedom.
- Con: weakens interrupt, inspection, and recovery guarantees too much.

## Decision

**Option A.**

Frothy `v0.1` requires an explicit interactive contract:

- REPL reads top-level forms and multiline incomplete input
- recoverable errors return to a usable prompt
- Ctrl-C interrupts running evaluation and pending multiline input
- interruption checks occur at safe points, including loop back-edges and IR
  dispatch
- after boot and restore, if `boot` is bound to `Code`, it runs under
  top-level recovery before the prompt
- required inspection entry points are:
  - `save`
  - `restore`
  - `dangerous.wipe`
  - `words`
  - `see`
  - `core`
  - `slotInfo`

## Consequences

- The first implementation must treat interactive recovery as core runtime work,
  not polish.
- `see` and `core` need canonical-IR-aware behavior.
- Interrupt plumbing and safe boot remain substrate worth reusing, but the
  visible Frothy contract is now explicit.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
