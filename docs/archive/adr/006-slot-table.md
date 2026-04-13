# ADR-006: Slot Table — Flat Array with Linear Scan

**Date**: 2026-02-27
**Status**: Accepted
**Spec sections**: 3.1 (Values — SlotRef), 5.1 (call), 5.2 (def), 5.3 (get)

## Context

The VM needs a structure to map word names to their definitions (slots). A SlotRef's payload is an index into this structure. Name lookup happens at parse time (human speed); runtime access is by direct index.

We need a design that works on ATtiny-class targets (2KB RAM) and avoids dynamic allocation.

## Options Considered

### Option A: Hash table

Constant-time lookup by name. Requires a backing array with empty slots for collision handling, wasting memory proportional to load factor. Typical load factor of 0.7 means ~30% of the array is always empty. Hash function adds code size.

### Option B: Flat array, linear scan by name

Sequential array of slot structs. Find-or-create scans entries comparing names. Direct index access at runtime via SlotRef payload. No wasted space — every allocated entry holds a real slot.

### Option C: Sorted array, binary search

O(log n) lookup instead of O(n). Requires shifting entries on insert to maintain order, which invalidates existing SlotRef indices. Incompatible with stable identity.

## Decision

**Option B.** Flat array with linear scan.

Name lookup is parse-time only (human speed). Once resolved to a SlotRef, all runtime access is O(1) by index. Linear scan over a small table (typically <100 entries on embedded targets) is negligible at parse time.

## Slot Entry Structure

```c
typedef froth_error_t (*froth_primitive_fn_t)(void);

typedef struct {
  const char* name;        // pointer into heap (null-terminated)
  froth_cell_t impl;       // current Froth value binding
  froth_primitive_fn_t prim; // non-NULL for primitive binding
  uint8_t impl_bound;      // impl presence bit (0 = unbound, 1 = bound)
} froth_slot_t;
```

- **`name`**: a `const char*` pointing directly into the heap where the name string was copied. Using a pointer (rather than a heap offset) avoids repeated casting at every `strcmp` call during linear scan. The slot does not own the string — the heap does.
- **`impl`**: a tagged cell holding the slot's current Froth value binding when `impl_bound != 0`. This may be a QuoteRef, Number, SlotRef, PatternRef, StringRef, or any other Froth value accepted by `def`.
- **`prim`**: a C function pointer typedef'd as `froth_primitive_fn_t`. If non-NULL, `call` invokes this directly before consulting `impl`. The signature `froth_error_t (*)(void)` gives primitives full access to the global stacks; returning a non-OK error triggers the error recovery path.
- **`impl_bound`**: explicit impl-binding presence bit. This keeps "unbound slot" separate from any valid cell encoding, including numeric zero.

### Dispatch rule

When `call` receives a SlotRef:
1. Index into the slot table.
2. If `prim != NULL`, invoke the primitive.
3. Otherwise, if `impl_bound != 0`, inspect `impl`: QuoteRefs execute, non-callable values are pushed.
4. Otherwise, signal `ERR.UNDEF`.

## Capacity and Occupancy Tracking

Fixed at compile time via `FROTH_SLOT_TABLE_SIZE` (CMake cache variable, default 128). A `slot_pointer` counter tracks the next free index. Slots are allocated sequentially and never freed, so all entries below `slot_pointer` are valid.

Attempting to create a slot when `slot_pointer >= FROTH_SLOT_TABLE_SIZE` returns `FROTH_ERROR_SLOT_TABLE_FULL`.

Empty slots are identified by `name == NULL`. The `slot_pointer` counter serves as the scan boundary for `find_name` and the insertion point for `create`.

## Name Storage

Name strings are stored in the linear heap (ADR-007), null-terminated. The caller (`froth_slot_create`) allocates `strlen(name) + 1` bytes via `froth_heap_allocate_bytes`, copies the name with `strcpy`, and stores the resulting `const char*` in the slot entry. If the heap is full, `create` propagates `FROTH_ERROR_HEAP_OUT_OF_MEMORY` before touching the slot table.

## API

Separate find and create operations (single-purpose functions):

- **`froth_slot_find_name(name, &index)`** — linear scan from 0 to `slot_pointer`. Returns `FROTH_OK` and writes the index, or `FROTH_ERROR_SLOT_NAME_NOT_FOUND`.
- **`froth_slot_create(name, heap, &index)`** — allocates name in heap, initializes slot entry, advances `slot_pointer`. Returns `FROTH_ERROR_SLOT_TABLE_FULL` or `FROTH_ERROR_HEAP_OUT_OF_MEMORY` on failure.
- **`froth_slot_get_impl(index, &impl)`** / **`froth_slot_get_prim(index, &prim)`** — read slot fields. Return `FROTH_ERROR_SLOT_INDEX_EMPTY` if the index has no assigned slot.
- **`froth_slot_set_impl(index, impl)`** / **`froth_slot_set_prim(index, prim)`** — write slot fields. Same empty-check.

## Consequences

- SlotRef indices are stable for the lifetime of the VM. Redefinition changes `impl`/`prim` but not the index.
- Name lookup is O(n) at parse time. Acceptable for expected table sizes (<256 entries).
- Slot table depends on the heap for name storage — heap must be initialized first.
- Primitive-bound, value-bound, and temporarily unbound placeholder slots coexist in the same table.
- No deallocation of individual slots. A slot, once created, exists forever (consistent with Forth tradition).
- Error variants added to `froth_error_t`: `FROTH_ERROR_SLOT_NAME_NOT_FOUND`, `FROTH_ERROR_SLOT_TABLE_FULL`, `FROTH_ERROR_SLOT_INDEX_EMPTY`.

## References

- Spec Section 3.1: SlotRef as an object reference type
- Spec Section 5.2: `def` binds any Froth value to a slot
- Spec Section 5.3: `get` retrieves a slot's current implementation
- ADR-004 / ADR-005: value tagging (SlotRef is tag 2)
- ADR-007: linear heap (name storage)
