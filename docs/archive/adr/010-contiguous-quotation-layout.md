# ADR-010: Two-Pass Contiguous Quotation Layout

**Date**: 2026-03-01
**Status**: Accepted
**Spec sections**: Quotation / QuoteRef semantics

## Context

The evaluator builds quotation bodies on the heap as it encounters `[ ... ]` in the token stream. The executor reads quotation bodies by indexing from the length cell: `heap[offset]` is the length, `heap[offset+1]` through `heap[offset+N]` are the body cells.

The original single-pass approach wrote cells to the heap as tokens were consumed. This caused a problem with nested quotations: the inner quotation's cells (length + body) were allocated *between* the outer quotation's length cell and its remaining body cells, breaking the contiguous layout the executor assumed.

**Symptoms observed:**
- Nested quotation test produced wrong output (`[S:def]` instead of `[Q:40]`) — executor read interleaved inner-quotation cells as if they were outer body cells.
- Forward-reference word definitions caused segfaults (exit code 139) — executor walked into unrelated heap data.

## Options Considered

### Option A: Temporary buffer

Collect body cells into a separate buffer, then `memcpy` into the heap once the closing `]` is found. Simple but requires an extra buffer whose size must be bounded, and on embedded targets (ESP32) memory is tight.

### Option B: Two-pass with reader save/restore

Pass 1: count direct children by scanning tokens, skipping nested brackets by depth tracking. Save and restore the reader position. Pass 2: pre-allocate the exact contiguous block on the heap, then fill in body cells. Nested quotations are built recursively *after* the outer block is reserved, so they land after the outer body on the heap.

### Option C: Patch-back

Write a placeholder length, emit body cells, then patch the length cell at the end. This was the original approach — it fails because nested quotation allocations interleave with the outer body.

## Decision

**Option B: Two-pass with reader save/restore.**

The reader struct is small (a pointer and a position) so save/restore is cheap. No extra buffer needed — the heap itself is the only storage. The count pass is fast (no allocation, no slot resolution, just token scanning). The fill pass does the real work and writes directly into pre-allocated heap slots.

The deciding factor was memory efficiency: no temporary buffer, no risk of sizing it wrong, and the reader is trivially copyable.

## Consequences

- Quotation bodies are always contiguous on the heap. The executor can index freely.
- Nested quotations land *after* their parent's body block on the heap, which is fine — the parent holds a tagged QuoteRef pointing to the nested block's offset.
- The token stream is scanned twice for every quotation. For deeply nested quotations this multiplies, but in practice quotation bodies are short and the reader is just advancing through an in-memory string.
- `count_quote_body` must correctly track bracket depth to skip nested quotations as single items. A bug here would cause miscount and heap corruption.
- The reader must be fully value-copyable (no internal pointers to external state beyond the input string). This is true today and must remain so.

## References

- `src/froth_evaluator.c`: `count_quote_body`, `froth_evaluator_handle_open_bracket`
- ADR-007 (linear heap), ADR-008 (heap accessor helpers)
