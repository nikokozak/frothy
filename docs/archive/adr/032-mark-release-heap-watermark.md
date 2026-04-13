# ADR-032: mark/release Heap Watermark

**Date**: 2026-03-14
**Status**: Accepted
**Spec sections**: FROTH-Region (not yet in spec)

## Context

Froth has no garbage collector. Every quotation, string, or pattern allocated at the REPL persists on the heap until the process exits. During interactive sessions (workshops especially), casual experimentation can exhaust the heap.

We need a way for users to reclaim scratch heap memory without restarting.

## Options Considered

### Option A: Single-level watermark

`mark` saves the current heap pointer into a single field on the VM. `release` restores it. A second `mark` silently overwrites the first. `release` without a prior `mark` throws an error.

Pros: trivial to implement (one VM field, two primitives, one error code). Honest about what it is: a REPL convenience, not a memory management system.

Cons: doesn't compose. A library word can't internally use mark/release without conflicting with the caller's mark.

### Option B: Nested watermark stack

`mark` pushes the current heap pointer onto a dedicated mark stack. `release` pops and restores. Marks nest naturally.

Pros: composable. A helper word can mark/release scratch space without affecting the caller.

Cons: nesting gives the appearance of safe composability, but watermark-based release can't protect against dangling references. A nested `release` that frees memory still pointed to by the data stack or slot bindings silently corrupts state. True composable regions need scope tracking (FROTH-Region-Strict), not just watermarks.

### Option C: Automatic region scoping

Tie heap lifetime to quotation execution scope. Allocations inside a region are automatically released when the region exits.

Pros: safe by construction.

Cons: substantial complexity, needs design work around what happens to values that escape the region. Premature for current needs.

## Decision

Option A. Single-level watermark.

The workshop use case is simple: mark at the start of experimentation, release when done. Nothing on the current roadmap needs nested marks. The composability gap is real but is better addressed by FROTH-Region-Strict as a separate future feature with proper scope tracking.

Implementation details:
- VM field: `froth_cell_u_t mark_offset`, initialized to `(froth_cell_u_t)-1` (sentinel for "no mark set")
- `mark` saves `vm->heap.pointer` into `mark_offset`
- `release` restores `vm->heap.pointer` from `mark_offset`, then resets sentinel
- `release` without prior `mark` throws `FROTH_ERROR_NO_MARK` (error code 19)
- No guard against releasing below boot watermark. `mark` can only be called from user code (post-`boot_complete`), so the mark value is always at or above the boot watermark.
- Dangling references after `release` are the user's responsibility.

## Consequences

- Workshop users can reclaim heap during interactive sessions.
- Does not compose. Library authors cannot use mark/release internally without coordinating with callers.
- FROTH-Region-Strict remains the path to safe, composable regions. This decision does not constrain that design.
- Users must clear the data stack of heap-allocated values before calling `release`, or accept dangling references.

## References

- FROTH-Region-Strict (deferred, TIMELINE.md)
