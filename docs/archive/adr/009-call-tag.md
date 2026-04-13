# ADR-009: FROTH_CALL Tag for Quotation Call Tokens

**Date**: 2026-02-28
**Status**: Accepted
**Spec sections**: Section 2 (quotation form — literal vs call tokens), Section 4.3 (quote execution)

## Context

Inside a quotation body like `[ foo 'bar ]`, both `foo` (bare identifier) and `'bar` (tick-quoted identifier) resolve to SlotRefs pointing to the same kind of slot. However, their execution semantics differ:

- `foo` is a **call token**: when the quotation executes, the slot should be **invoked** (its implementation is called).
- `'bar` is a **literal token**: when the quotation executes, the SlotRef should be **pushed** onto DS.

The spec is explicit (Section 2, quotation form):

> - Identifier `name` inside a quotation → stored as a *call token* containing the SlotRef for `name`.
> - `'name` inside a quotation → stored as a literal SlotRef token (pushes SlotRef).

With only `FROTH_SLOT` (tag 2), the executor cannot distinguish these two cases — both would be tagged identically in the heap.

## Options Considered

### Option A: New tag value `FROTH_CALL` (tag 6)

Add a seventh tag using one of the two remaining 3-bit tag values (6 and 7). `FROTH_CALL` marks a cell as "invoke this SlotRef" rather than "push this SlotRef."

- `FROTH_SLOT` (tag 2): literal SlotRef — executor pushes it onto DS.
- `FROTH_CALL` (tag 6): call SlotRef — executor invokes the slot's implementation.

Pros: clean separation, no ambiguity, zero-cost check (just a tag comparison), consistent with existing tagging scheme.

Cons: consumes one of two remaining tag values. Only 1 tag slot remains (tag 7).

### Option B: High bit flag in the payload

Keep `FROTH_SLOT` for both cases but steal a bit from the payload to indicate call vs literal.

Pros: doesn't consume a tag value.

Cons: reduces addressable slot range by half, complicates every payload read/write, easy to forget the masking.

### Option C: Separate token-type byte before each cell in quotation bodies

Store a "token kind" byte (literal or call) before each cell in the quotation body.

Pros: unlimited extensibility for future token kinds.

Cons: breaks cell alignment, increases quotation size, complicates iteration.

## Decision

**Option A**: `FROTH_CALL` as tag 6. The tag space exists for exactly this kind of distinction. The cost is one tag value; the benefit is that the executor's dispatch is a simple tag switch with no special-case logic. Tag 7 remains available for future use.

`FROTH_SLOT` (tag 2) retains its meaning as a first-class SlotRef value — something a user can hold on the stack, pass to `call`, use with `def`, etc. `FROTH_CALL` (tag 6) is an internal token type that only appears inside quotation bodies and is never visible to user code on the data stack.

## Consequences

- Quotation executor dispatches on tag: NUMBER/QUOTE/SLOT/STRING → push; CALL → invoke.
- `FROTH_CALL` cells should never appear on DS outside of quotation execution internals. If one leaks, it's a bug.
- Only one tag value (7) remains unassigned.
- The reader stores `'name` as `FROTH_SLOT` and bare `name` as `FROTH_CALL` inside quotation bodies.
- At top level, the reader doesn't need `FROTH_CALL` — it can invoke immediately without storing anything.

## References

- ADR-004 (value tagging)
- ADR-005 (naming conventions)
- Spec Section 2: quotation form token mapping
- Spec Section 4.3: quote execution semantics
