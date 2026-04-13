# ADR-008: Heap Accessor Helpers

**Date**: 2026-02-28
**Status**: Accepted
**Spec sections**: Section 3 (abstract machine — heap), Section 4.3 (quote execution)

## Context

The Froth heap is backed by a `uint8_t` array, but most data stored in the heap is cell-sized (`froth_cell_t`). Every read or write of cell-sized data requires a cast from `uint8_t*` to `froth_cell_t*` at the correct byte offset:

```c
froth_cell_t* ptr = (froth_cell_t*)&froth_heap.data[byte_offset];
```

This pattern is error-prone for two reasons:

1. Forgetting the `&` dereferences a byte value instead of taking an address.
2. Confusing byte offsets with cell indices silently writes to wrong locations (the quotation proof hit both bugs during development).

As more subsystems interact with the heap (reader, quotation execution, slot table, snapshots), this cast will appear everywhere. A wrong cast corrupts the heap silently.

## Options Considered

### Option A: Inline helper function

A `static inline` function in `froth_heap.h` that takes a heap pointer and byte offset, returns a `froth_cell_t*`:

```c
static inline froth_cell_t* froth_heap_cell_ptr(froth_heap_t* heap, froth_cell_u_t byte_offset) {
  return (froth_cell_t*)&heap->data[byte_offset];
}
```

Pros: single point of truth for the cast, zero runtime cost, readable at call sites, compiler can still warn about type misuse. Easy to add bounds checking later behind `#ifdef FROTH_DEBUG`.

Cons: doesn't prevent invalid byte offsets (but neither does the manual cast).

### Option B: Macro

```c
#define FROTH_HEAP_CELL_PTR(heap, offset) ((froth_cell_t*)&(heap)->data[(offset)])
```

Pros: zero overhead, works in any expression context.

Cons: no type checking on arguments, harder to add debug instrumentation, macro hygiene issues.

### Option C: Read/write function pair

Separate `froth_heap_read_cell` and `froth_heap_write_cell` functions that take an offset and return/accept a value.

Pros: could embed bounds checking.

Cons: awkward when you need to iterate over a range of cells (quotation bodies), since you'd call the function per-cell instead of getting a pointer and indexing.

## Decision

**Option A**: inline helper function. It eliminates the cast at every call site, preserves type safety, and naturally supports indexed access (`froth_heap_cell_ptr(heap, offset)[i]`) for walking quotation bodies. Debug bounds checking can be added later without changing call sites.

## Consequences

- All heap cell access goes through `froth_heap_cell_ptr`. Raw casts of `froth_heap.data` should not appear outside of `froth_heap.c` internals.
- Future debug builds can add offset validation (in-bounds, alignment) in one place.
- Does not address the literal-vs-call token distinction in quotation bodies — that's a separate design decision for the reader.

## References

- ADR-007 (linear heap)
- ADR-004 (value tagging)
- Quotation proof in `main.c` (Feb 28 session)
