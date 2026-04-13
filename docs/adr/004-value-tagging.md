# ADR-004: Value Representation — 3-bit LSB Tagging

**Date**: 2026-02-26
**Status**: Accepted
**Spec sections**: 3.1 (Values), 3.2 (Stacks), 5.1 (call)

## Context

The spec requires that a Froth value is either a Cell (signed integer) or an object reference (QuoteRef, SlotRef, PatternRef, and optionally StringRef and ContractRef). The VM must be able to distinguish these at runtime — `call` needs to know if it's got a QuoteRef or a SlotRef, `perm` needs to know it's got a PatternRef, arithmetic needs to reject non-integers, etc.

We need a representation that fits in a single `froth_cell_t` (see ADR-001), works on targets as small as ATtiny (2KB RAM), and doesn't require heap allocation or pointer chasing for type checks.

## Options Considered

### Option A: Tagged union struct (type enum + value)

Each stack slot is a struct with an enum tag and a cell-sized value. Clean C, full integer range preserved, no bit manipulation.

Trade-offs: struct is at least 2× the size of a raw cell due to the tag field plus alignment padding. On a 32-bit system, each stack entry goes from 4 bytes to 8. On a 16-bit system, 2 to 4. For a target like ATtiny with 2KB of RAM, this is a hard sell — it effectively halves your usable stack depth.

### Option B: 1-bit tag (integer vs. reference) with indirect typing

Use a single bit to distinguish "integer" from "reference." References point to a struct in memory that carries the actual type. Saves tag space.

Trade-offs: every type check on a reference requires a memory load to read the type field. On hardware with no cache (ATtiny, Cortex-M0), that's a significant cost on every `call` invocation. Also requires allocating the type+value structs somewhere, reintroducing memory management complexity.

### Option C: 3-bit LSB tagging

Reserve the lowest 3 bits of every cell for a type tag. The remaining bits carry the payload (integer value or reference index). Tag `000` is assigned to Cell (integer) so that addition, subtraction, and comparison work without untagging.

Trade-offs: integer range is reduced. On 32-bit cells, signed integers get 29 bits (±268 million) — plenty for embedded work. On 16-bit cells, signed integers get 13 bits (±4096) — tight but usable for small targets. Bit manipulation (mask, shift) required on every type check and when extracting integer values for multiplication/division.

3 bits give 8 possible tags. We need 4 now (Cell, QuoteRef, SlotRef, PatternRef) with room for 4 more (StringRef, ContractRef, and two reserved for future profiles).

## Decision

**Option C.** 3-bit LSB tagging with Cell at tag `000`.

The deciding factors:

1. **Memory**: one cell per value, same as a plain integer. No stack size penalty on tiny targets.
2. **Performance**: type checks are a mask operation. Integer arithmetic (add, subtract, compare) works directly with no untagging because the tag is `000`.
3. **Headroom**: 8 tags is enough for all spec-defined types plus room for future extensions.

Tag assignments:

| Tag (binary) | Tag (decimal) | Type       | Status       |
|--------------|---------------|------------|--------------|
| `000`        | 0             | Cell       | FROTH-Core   |
| `001`        | 1             | QuoteRef   | FROTH-Core   |
| `010`        | 2             | SlotRef    | FROTH-Core   |
| `011`        | 3             | PatternRef | FROTH-Core   |
| `100`        | 4             | StringRef  | Reserved     |
| `101`        | 5             | ContractRef| Reserved     |
| `110`        | 6             | (unused)   | Reserved     |
| `111`        | 7             | (unused)   | Reserved     |

## Consequences

- All values on DS, RS, and CS are `froth_cell_t` with the low 3 bits indicating the type. No separate type tracking.
- Integer range is reduced: 29-bit signed for 32-bit cells, 13-bit signed for 16-bit cells. The 13-bit limitation on 16-bit targets should be documented as a known constraint.
- The `froth_cell_t` type stays the same (signed integer), but we need macros to tag, untag, and check values. These go in `froth_types.h`.
- Arithmetic words (`+`, `-`, `*`, `/mod`) must verify both operands are Cells (tag `000`) and signal `ERR.TYPE` otherwise. Addition and subtraction can skip untagging since `000 + 000 = 000`. Multiplication and division need to shift.
- Object references use the payload bits as an index (into a slot table, heap, or pattern table — details TBD in future ADRs). With 29 bits of index on 32-bit cells, we can address ~500 million objects, which is more than enough.
- Future types (StringRef, ContractRef) just claim an unused tag value. No structural changes needed.

## References

- Spec Section 3.1: "Implementations MUST be able to distinguish these classes for core operations"
- Spec Section 3.2: DS holds Values (Cell or object reference)
- ADR-001: cell width is compile-time configurable — tagging must work across all supported widths
- Classic prior art: Lua, Ruby, most Lisps use some variant of tagged value representation
