# ADR-013: PatternRef Byte-Packed Heap Encoding

**Date**: 2026-03-03
**Status**: Accepted
**Spec sections**: 3.4 (Pattern literal), 4.2 (Value classes / PatternRef), 4.3 (Heap objects), 5.10 (pat), 5.11 (perm)

## Context

We need a heap layout for PatternRef before implementing `perm`, `p[...]`, and `pat`. A PatternRef holds an ordered list of `k` indices, where each index is in the range `[0, n)` and `n` is the input window size passed to `perm`. In practice, `n` and `k` are small (typical range 0-7).

QuoteRef uses a cell-array layout: `[length_cell] [body_cells...]` via `froth_heap_allocate_cells`. This is appropriate for quotation bodies because they contain tagged cells (Numbers, SlotRefs, CallRefs, nested QuoteRefs, etc.) that are each a full `froth_cell_t` wide.

PatternRef indices are fundamentally different: they are small non-negative integers (0-7 in typical use). Storing each index as a full cell wastes 3 bytes per index at 32-bit cell width, or 7 bytes at 64-bit. On embedded targets (ESP32 with 520 KB SRAM, RP2040 with 264 KB), heap space is scarce and patterns are frequent — every `dup`, `swap`, `rot`, `over`, etc. compiles to a pattern.

## Options Considered

### Option A: Cell-array layout (same as QuoteRef)

Heap layout: `[length_cell] [index_cell_0] ... [index_cell_k-1]`

Allocated via `froth_heap_allocate_cells(1 + k)`. Each index is a full `froth_cell_t` with tag bits clear (plain Number).

Trade-offs:
- Pro: identical allocation path to QuoteRef — no new code paths
- Pro: can reuse `froth_heap_cell_ptr` for indexed access
- Pro: no alignment concerns (cell-aligned by construction)
- Con: wastes `(sizeof(froth_cell_t) - 1) * k` bytes per pattern (3k bytes at 32-bit, 7k bytes at 64-bit)
- Con: alignment padding before the cell array wastes additional bytes
- Con: pattern indices never need tag bits or the full cell range — overprovisioned

### Option B: Byte-packed layout

Heap layout: `[count_byte] [index_0] ... [index_k-1]` — all `uint8_t`.

Allocated via `froth_heap_allocate_bytes(1 + k)`. No cell alignment needed.

Trade-offs:
- Pro: minimal heap usage — `1 + k` bytes total (e.g. `swap` pattern = 3 bytes vs 12 bytes in Option A at 32-bit)
- Pro: `froth_heap_allocate_bytes` already exists for raw byte allocation
- Pro: no alignment padding — packs tightly against prior allocations
- Pro: cache-friendly — entire pattern fits in a single cache line for any reasonable `k`
- Con: different access pattern from QuoteRef — must read `heap->data[offset]` as `uint8_t` rather than using `froth_heap_cell_ptr`
- Con: maximum pattern length capped at 255 (by `uint8_t` count) — not a real constraint
- Con: maximum index value capped at 255 — not a real constraint

## Decision

**Option B: Byte-packed layout.**

The deciding factors:

1. **4x space reduction at 32-bit.** A `swap` pattern (k=2) costs 3 bytes instead of 12. An `over` pattern (k=3) costs 4 bytes instead of 16. On ESP32 with a 4 KB default heap, this matters.

2. **No new infrastructure needed.** `froth_heap_allocate_bytes` already exists. Reading bytes from `heap->data[offset + j]` is trivial.

3. **The divergence from QuoteRef is intentional and justified.** QuoteRef stores tagged cells that need the full cell width. PatternRef stores small indices that fit in a byte. Using the same layout for both would be false consistency.

4. **Compile-time cap.** `FROTH_MAX_PERM_SIZE` (default 8) bounds both pattern length (`k`) and window size (`n`) at compile time. This enables a fixed-size scratch buffer in `perm` (for snapshotting input items before overwriting) without dynamic allocation or VLAs. The cap is enforced in `pat` and `p[...]` at pattern construction time, and in `perm` for the window size at execution time.

## Heap Layout Detail

For a pattern with `k` indices, `froth_heap_allocate_bytes(1 + k)` returns a byte offset. The bytes at that offset are:

```
offset+0:  count (uint8_t) = k
offset+1:  index[0]        (uint8_t) — deepest output item
offset+2:  index[1]        (uint8_t)
  ...
offset+k:  index[k-1]      (uint8_t) — new TOS
```

The tagged PatternRef cell stores this byte offset as its payload: `FROTH_CELL_PACK_TAG(byte_offset, FROTH_PATTERN)`.

Reading the pattern in `perm`:
```c
uint8_t *pat = &heap->data[byte_offset];
uint8_t k    = pat[0];
// pat[1] through pat[k] are the indices
```

## Consequences

### What becomes easier

- Heap-efficient pattern storage, critical for embedded targets where `dup`/`swap`/`rot`/`over` are defined via `perm` and each definition costs a pattern allocation.
- Simple byte-level access — no casting, no alignment, no tag stripping.

### What becomes harder

- PatternRef access is a different code path from QuoteRef. No reuse of `froth_heap_cell_ptr` for pattern data. This is a minor cost — pattern access is a few lines of byte indexing.

### Constraints on future decisions

- Pattern indices must fit in `uint8_t` (max 255). This is not a meaningful constraint — `perm` window sizes beyond ~8 are pathological.
- `FROTH_MAX_PERM_SIZE` must be set before compilation. Changing it requires a rebuild.
- If FROTH-Perf needs to inline or optimize patterns, the byte-packed format is easy to read at compile time and easy to specialize.

## References

- Froth Language Spec v1.1, Sections 3.4, 4.2, 4.3, 5.10, 5.11
- ADR-007 (linear heap), ADR-008 (heap accessor helpers), ADR-010 (quotation layout)
- ADR-012 (perm TOS-right reading direction)
- `src/froth_heap.h`: `froth_heap_allocate_bytes`
- `src/froth_types.h:78`: `FROTH_PATTERN = 3`
