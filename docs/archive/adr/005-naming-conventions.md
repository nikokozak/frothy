# ADR-005: Naming Conventions — Cell, Number, and Tag Terminology

**Date**: 2026-02-27
**Status**: Accepted
**Supersedes**: Terminology in ADR-004 (tag table updated here; ADR-004 structural decisions remain valid)
**Spec sections**: 3.1 (Values), 3.2 (Stacks)

## Context

The current codebase uses "cell" to mean three different things:

1. The raw machine word / fundamental stack unit (`froth_cell_t`)
2. A tagged value produced by `froth_make_cell`
3. The integer variant specifically (tag `000`, enum value `FROTH_CELL`)

This creates confusing identifiers like `FROTH_CELL_IS_CELL` ("is this cell a cell?") and `froth_make_cell` (which makes any tagged value, not just numbers). The overloading makes the code harder to reason about as more subsystems are built on top.

We need a consistent naming scheme where each concept has exactly one name.

## Options Considered

### Option A: "Value" as the stack unit, "Cell" as numeric variant

Keep `froth_cell_t` as the raw type name, but call the *concept* a "value." The numeric tag variant keeps the name "Cell" from the spec. Produces `FROTH_CELL_IS_CELL` — still confusing.

### Option B: "Cell" as the stack unit, "Number" as the numeric variant

"Cell" means the tagged machine word that lives on the stack — the fundamental unit of the VM. The tag-0 numeric variant becomes "Number." All reference types keep their `*Ref` suffix. The type `froth_cell_t` stays as-is (it's the C type for a cell). Produces `FROTH_CELL_IS_NUMBER` — reads clearly.

### Option C: "Cell" as the stack unit, "Integer" as the numeric variant

Same as B but with "Integer." More precise about the current representation (signed, whole numbers), but less future-proof if a profile ever introduces fixed-point or other numeric types under the same tag.

## Decision

**Option B.** "Cell" is the overarching container; "Number" names the tag-0 numeric variant.

The hierarchy:

- **Cell** (`froth_cell_t`) — a tagged machine word. The fundamental unit on every stack and in every table.
- **Number** (tag `000`) — a signed numeric value. Tag bits are zero, so addition and subtraction work without untagging.
- **QuoteRef** (tag `001`) — index into the heap; a quotation body.
- **SlotRef** (tag `010`) — index into the slot table; a named definition.
- **PatternRef** (tag `011`) — index into the heap; a permutation pattern.
- **StringRef** (tag `100`) — reserved.
- **ContractRef** (tag `101`) — reserved.

Deciding factors:

1. "Cell" as the fundamental unit aligns with Forth tradition (a "cell" is the basic addressable unit).
2. "Number" is broad enough to remain correct if future profiles introduce fixed-point, scaled, or other numeric representations under tag 0.
3. Eliminates all `CELL_IS_CELL` absurdities.

## Consequences

### Renames required

| Before | After | Reason |
|--------|-------|--------|
| `FROTH_CELL` (enum tag 0) | `FROTH_NUMBER` | Tag names the variant, not the container |
| `FROTH_CELL_IS_CELL(val)` | `FROTH_CELL_IS_NUMBER(val)` | Reads correctly now |
| `froth_make_cell()` | `froth_make_tagged()` | It makes any tagged cell, not specifically a number |
| `FROTH_ERROR_CELL_VALUE_OVERFLOW` | `FROTH_ERROR_VALUE_OVERFLOW` | Shorter, still clear |

### Names that stay the same

| Name | Reason |
|------|--------|
| `froth_cell_t` / `froth_cell_u_t` | Correct — it's the C type for a cell |
| `froth_cell_tag_t` | Correct — it's the tag enum for a cell |
| `FROTH_GET_CELL_TAG` | Correct — extracts the tag from a cell |
| `FROTH_STRIP_CELL_TAG` | Correct — removes the tag from a cell |
| `FROTH_PACK_CELL_TAG` | Correct — packs a value and tag into a cell |
| `FROTH_CELL_IS_QUOTE` etc. | Correct — checks if a cell is a QuoteRef |

### Spec update

The spec should be updated to use "Number" instead of "Cell integer" for the tag-0 variant. "Cell" in the spec should refer to the tagged machine word generally, consistent with this ADR.

### ADR-004 alignment

ADR-004's structural decisions (3-bit LSB tagging, tag assignments, arithmetic properties) remain unchanged. Only the *name* of tag 0 changes from "Cell" to "Number" in documentation and code.

## References

- ADR-004: Value Representation — 3-bit LSB Tagging
- Spec Section 3.1: value classes
- Spec Section 3.2: stack contents
