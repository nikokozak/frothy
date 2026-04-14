# Frothy ADR-101: Stable Top-Level Slot Model

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 2.1, 3.1, 4.2, 4.3, 4.4, 7.1, 7.2, 7.3
**Roadmap milestone(s)**: M2, M5, M7
**Inherited Froth references**: `src/froth_slot_table.h`, `src/froth_slot_table.c`

## Context

Frothy keeps one central Froth strength: stable top-level identity. The user
model changes from visible stacks and quotations to values, places, lexical
blocks, and callable `Code`, but top-level rebinding must still preserve the
identity that makes liveness and persistence work.

The runtime needs one clear rule for:

- top-level definitions
- lookup and rebinding
- overlay shadowing of base-image bindings
- what old callers observe after a redefinition

## Options Considered

### Option A: Stable slot identity for every top-level name

Each top-level name resolves to one stable slot identity. Rebinding changes the
current value stored in that slot, not the slot identity.

Trade-offs:

- Pro: preserves Froth's best live-image property.
- Pro: makes overlay persistence and rebinding coherent.
- Pro: lets old callers observe new behavior through the same slot.

### Option B: Fresh top-level binding object on every redefine

Treat redefinition as replacement rather than stable identity.

Trade-offs:

- Pro: conceptually simple in a batch language.
- Con: breaks the live-image model Frothy is trying to preserve.
- Con: complicates overlay restore and call indirection.

### Option C: Separate value and function namespaces

Keep stable identity, but split callable names from ordinary values.

Trade-offs:

- Pro: familiar to some languages.
- Con: directly violates Frothy's one-namespace model.
- Con: complicates `Code` as an ordinary value.

## Decision

**Option A.**

Frothy has one top-level namespace of stable named slots.

- A top-level name always refers to the same slot identity.
- Top-level `name is expr` creates or rebinds the value stored in that slot.
- `Code` is a value like any other and lives in the same namespace.
- Lookup proceeds from locals outward to the top-level slot set.
- `set` mutates an existing place, which may be a local, a top-level name, or
  an indexed cells element.
- Rebinding a base-image name installs an overlay value on the same stable slot.
  `dangerous.wipe` and failed `restore` return that slot to the boot-rebuilt base
  value.

## Consequences

- Redefinition remains coherent and observable by existing callers.
- Overlay persistence can key bindings by symbol identity instead of transient
  object identity.
- Frothy can reuse the inherited slot-table substrate without reusing the old
  stack-centric language model.
- Any future module or namespace layer must preserve stable slot identity at
  the top-level image boundary.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
