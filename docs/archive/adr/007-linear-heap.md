# ADR-007: Linear Heap — Single Byte Buffer with Aligned Cell Allocation

**Date**: 2026-02-27
**Status**: Accepted (supersedes original dual-heap design)
**Spec sections**: 3.1 (Values — QuoteRef, PatternRef), 5.2 (def), FROTH-Region (mark/release)

## Context

The VM needs dynamic storage for variable-sized data: name strings (byte sequences) and quotation/pattern bodies (cell sequences). These are allocated at parse/definition time and never individually freed. We need a design that is memory-efficient on ATtiny-class targets (2KB RAM) and avoids `malloc`.

## Options Considered

### Option A: Single heap mixing bytes and cells

One `uint8_t` backing array holds both strings and cell sequences. All allocations come from the same pool.

Trade-off: cell allocations that follow string allocations may land on unaligned offsets. Requires alignment padding, wasting a few bytes per transition. However, a single pool avoids the problem of pre-splitting a fixed memory budget between two heaps.

### Option B: Two separate heaps (name_heap, cell_heap)

Separate backing arrays: one for names, one for quotation/pattern bodies. No alignment issues since each heap only stores one element type.

Trade-off: must decide the split upfront at compile time. If one fills up while the other has room, you can't borrow. Wastes memory if the split is wrong.

## Decision

**Option A.** Single heap, single backing array, alignment handled by the allocation API.

Originally we planned Option B (two heaps), but reconsidered: on constrained targets, pre-splitting memory between two pools risks wasting space if the split is wrong. A single pool with occasional alignment padding (at most `sizeof(froth_cell_t) - 1` bytes per cell allocation following a string) wastes far less in practice.

## Heap Structure

```c
typedef struct {
  uint8_t* data;           // backing array
  froth_cell_u_t pointer;  // next free byte offset
} froth_heap_t;
```

The heap is a dumb byte buffer. It does not know or care what is stored in it. Callers are responsible for interpreting stored data (casting, null termination, length prefixes).

A single global instance backed by a static `uint8_t[FROTH_HEAP_SIZE]` array. Capacity is compile-time configurable via `FROTH_HEAP_SIZE` (CMake cache variable, default 4096).

## Allocation API

- **`froth_heap_allocate_bytes(size, heap, &offset)`** — allocates `size` bytes, returns starting byte offset. No alignment.
- **`froth_heap_allocate_cells(size, heap, &offset)`** — aligns pointer up to `sizeof(froth_cell_t)`, then allocates `size * sizeof(froth_cell_t)` bytes. Returns the aligned byte offset.

Both return `froth_error_t` — `FROTH_OK` on success, `FROTH_ERROR_HEAP_OUT_OF_MEMORY` if insufficient space.

The heap takes a `froth_heap_t*` parameter (passed by the caller), following the same pattern as the stack API.

## Alignment

Cell allocation aligns the pointer before allocating:

```
aligned = (pointer + (align - 1)) & ~(align - 1)
```

Where `align = sizeof(froth_cell_t)`. This is portable across all cell widths (16/32/64-bit). Bytes between the old pointer and the aligned offset are wasted padding — at most `sizeof(froth_cell_t) - 1` bytes per cell allocation that follows a byte allocation.

## Pointer Casting

The backing array is `uint8_t[]`, but callers store different types by casting:

- **Name strings**: caller allocates `strlen + 1` bytes, casts `(char*)(heap->data + offset)`, writes with `strcpy`. The `const char*` is stored in the slot entry.
- **Quotation/pattern bodies**: caller allocates `n` cells via `allocate_cells`, casts `(froth_cell_t*)(heap->data + offset)` to read/write cell-sized tokens. The alignment guarantee ensures this cast is safe on all target architectures.

## Storage Conventions (caller responsibility, not heap responsibility)

- **Name strings**: null-terminated. Caller requests `strlen(name) + 1` bytes.
- **Quotation bodies**: first cell is the length (token count), followed by that many cells of tokens. QuoteRef payload holds the byte offset.
- **Pattern bodies**: same layout as quotation bodies. PatternRef payload holds the byte offset.

The heap does not add null terminators, length prefixes, or any metadata. It only reserves space.

## Consequences

- The heap is a dumb byte buffer. It knows nothing about what's stored in it.
- Single pool means no wasted capacity from a bad split. All variable-sized data competes for the same memory.
- Callers are responsible for interpreting stored data (casting, length conventions).
- No individual deallocation. `mark`/`release` (FROTH-Region, deferred) will reset the pointer to reclaim bulk allocations.
- `FROTH_ERROR_HEAP_OUT_OF_MEMORY` added to `froth_error_t`.

## References

- ADR-006: Slot table — names stored in heap via `froth_heap_allocate_bytes`
- ADR-004 / ADR-005: value tagging — QuoteRef and PatternRef payloads index into the heap
- Spec Section 3.1: QuoteRef, PatternRef as immutable heap objects
- Spec FROTH-Region: mark/release for bulk heap reclamation (deferred)
