# ADR-054: First FROTH-CellSpace Profile (`create`, `allot`, `@`, `!`, `variable`)

**Date**: 2026-04-02
**Status**: Proposed
**Spec sections**: `docs/archive/spec/Froth_Language_Spec_v1_1.md` Section 3 (Values, Slots, Heap), Section 5 (Core words), Section 8 (FROTH-Named), Section 14 (FROTH-Addr); `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md` Sections 7-10
**Related ADRs**: ADR-017 (`def` accepts any value), ADR-024 (FROTH-Addr), ADR-026 (snapshot persistence implementation), ADR-032 (`mark` / `release`), ADR-043 (transient string buffer), ADR-050 (first FROTH-Named implementation), ADR-051 (binding-intent primitives), ADR-056 (keep `StringRef` for text; PAD/buffers for bytes)

## Context

The TM1629 exercise made a language-design gap impossible to ignore.

Froth currently has:

- **slots** for stable word identity and slot-backed scalar state,
- an immutable **heap** for quotations, patterns, and strings,
- and no user-facing, indexable, mutable aggregate storage.

The practical consequence is that arrays, framebuffers, lookup tables, and
small mutable records get modeled through combinations of:

- many individually named slots,
- quotations used as pseudo-arrays of slot references,
- or FFI escape hatches.

That is workable for tiny examples. It is not a good long-term authoring model.
The language now has a strong story for words and callbacks, but a weak story
for ordinary data.

The design pressure is not theoretical:

- the pure TM1629 Froth library became slot-hungry,
- aggregate state had to be represented indirectly,
- and the lack of a first-class mutable data region became the main reason the
  code felt less direct than the comparative Gforth version.

At the same time, Froth should not simply import raw native pointers into the
core language:

- native machine addresses are already the job of FROTH-Addr (ADR-024),
- snapshot persistence must stay safe and deterministic,
- and Froth should preserve its existing tagged-value model instead of creating
  a split world where stack values and stored values use different
  representations.

The missing piece is therefore not "raw C memory everywhere." It is a
**Froth-managed mutable cell region** that is separate from both slots and the
immutable heap.

Constraints:

1. Keep slots as the mechanism for stable word identity and coherent
   redefinition.
2. Keep heap objects immutable and append-only.
3. Do not introduce garbage collection or hidden heap allocation in hot paths.
4. Keep the region snapshot-safe.
5. Keep the implementation realistic for workshop and thesis scope.
6. Improve directness without turning the core language into raw native-pointer
   programming.

## Options Considered

### Option A: Status quo (slots + quotations + future ad hoc helpers)

Keep aggregate state modeled through slot-backed scalars, quotations of slot
references, and future narrowly targeted helpers.

Trade-offs:

- Pro: no immediate VM work.
- Pro: preserves the current slot/heap-only runtime model.
- Con: keeps arrays and framebuffers as workarounds rather than first-class
  language concepts.
- Con: forces mutable aggregates to consume many slot-table entries.
- Con: keeps Froth dependent on FFI or special-purpose profiles for basic data
  structure work.
- Con: does not solve the directness problem revealed by TM1629.

### Option B: Opaque handle-based mutable buffers (`BufferRef`)

Introduce a managed mutable arena referenced by stable handles, with a
special-purpose access vocabulary such as `buf@`, `buf!`, `buf.len`, etc.

Trade-offs:

- Pro: safe by default; bounds checks are natural.
- Pro: no raw address arithmetic in user code.
- Pro: easy to make snapshot-safe.
- Con: low composability. Every data pattern wants new accessor words or a new
  built-in abstraction.
- Con: feels unlike Forth in the specific area where Forth is strongest:
  direct data layout work.
- Con: more VM surface and more design work than the immediate problem needs.
- Con: does not help the "build data abstractions from a small substrate"
  direction that Froth should preserve.

### Option C: Tagged-cell, Froth-managed CellSpace with Forth-style addressing

Add a fixed-size mutable region of tagged Froth cells. Expose it with a small
Forth-like surface:

- `create`
- `allot`
- `variable`
- `@`
- `!`
- `cells`
- `cell+`

Slots remain the naming mechanism. CellSpace is the storage mechanism.

Trade-offs:

- Pro: directly solves arrays, framebuffers, lookup tables, counters, and other
  mutable aggregate-state cases.
- Pro: keeps the mental model small: words are still named through slots, but
  data is stored in a separate region.
- Pro: composes well with ordinary arithmetic and indexing.
- Pro: tagged storage means CellSpace can hold Numbers, SlotRefs, QuoteRefs,
  StringRefs, and future values uniformly.
- Pro: snapshot integration is straightforward because CellSpace values use the
  same tagged representation as the stack and slots.
- Pro: no new tag is required if addresses are modeled as CellSpace offsets
  carried as Numbers.
- Con: requires new VM state, new primitives, and snapshot-format extension.
- Con: classic Forth footguns reappear unless managed access is checked.
- Con: `create`/`allot` needs an explicit rule about definition-time versus
  runtime allocation.

### Option D: Raw untagged byte-addressed data space

Add a classic byte-addressed region of raw machine cells and let `@` / `!`
convert between raw stored values and tagged stack values.

Trade-offs:

- Pro: closest to classic Forth.
- Pro: naturally supports packed byte-level layouts.
- Con: creates a representation boundary between stack values and stored
  values.
- Con: makes storing non-numeric Froth values awkward or impossible.
- Con: complicates snapshotting because raw storage is no longer self-typing.
- Con: pulls the design toward native-pointer semantics when FROTH-Addr already
  exists for that boundary.

## Decision

**Option C: introduce a first FROTH-CellSpace profile.**

The deciding factor is conceptual separation:

- **slots are for words and stable names**
- **CellSpace is for mutable aggregate data**

That is the simplest correction to the current model that preserves Froth's
existing strengths.

### 1. Add an optional `FROTH-CellSpace` profile

`FROTH-CellSpace` is an optional profile layered on top of the current core.
It adds a fixed-capacity mutable data region owned by the VM.

This profile is intentionally narrow:

- it is for **cell-oriented mutable data**,
- it is not a raw native-address profile,
- and it is not a general-purpose runtime allocator.

### 2. CellSpace stores full tagged Froth cells

The data region stores `froth_cell_t` values, not raw integers and not raw
bytes.

Reason:

- Froth already uses tagged cells on DS, RS, and in slot implementations.
- A mutable region that stores only raw integers would create a type boundary.
- Tagged storage lets arrays hold Numbers, SlotRefs, QuoteRefs, PatternRefs,
  StringRefs, and future types uniformly.

This also means `@` and `!` move the same kind of values Froth already knows
how to print, type-check, and serialize.

### 3. Addressing is cell-indexed in v1

CellSpace addresses are **cell offsets**, not byte addresses.

In v1:

- address `0` means "CellSpace cell 0"
- address arithmetic is in cell units
- `@` and `!` operate on one full cell each

This keeps the implementation small:

- no byte alignment rules,
- no sub-cell read/write surface,
- no accidental overlap between partial writes and tagged cells.

Packed byte-oriented protocol work remains the job of:

- FROTH-Addr for true native-address access, or
- FFI/buffer-oriented work in future profiles.

### 4. CellSpace addresses are carried as Froth Numbers

This ADR does **not** introduce a new `DataAddr` tag.

Instead:

- slots created by `create`/`variable` push a Number that represents a
  CellSpace cell offset,
- `@` and `!` interpret their address argument as a CellSpace offset,
- FROTH-Addr remains the distinct story for native machine addresses.

Reasons:

1. tag pressure is already real in the current value model,
2. CellSpace offsets are VM-internal and snapshot-portable,
3. keeping them as Numbers avoids spending a new value tag on a
   VM-managed-only concept.

The safety boundary comes from the word surface:

- `@` and `!` are **CellSpace** words,
- `@8` / `@16` / `@32` / `!8` / `!16` / `!32` remain the future
  **native-address** words in FROTH-Addr.

### 5. Core CellSpace surface

The first profile surface is:

- `create ( slot -- )`
- `allot ( n -- )`
- `variable ( slot -- )`
- `@ ( addr -- value )`
- `! ( value addr -- )`

Recommended tiny helpers:

- `cells ( n -- n' )`
- `cell+ ( addr -- addr' )`

`+!`, `cell-`, `2@`, `2!`, `constant`, and struct/array-defining words are
explicitly outside this ADR's required kernel surface.

#### `create`

`create` binds the supplied slot to the current CellSpace address and stamps it
as `(0 -- 1)` metadata, because invoking the slot pushes its address.

In v1, the intended authoring surface is the existing ticked style:

```froth
'rows create
8 allot
```

Classic Forth `create name` syntax is deferred. It requires reader/evaluator
work that is not needed to prove the model.

#### `allot`

`allot` reserves `n` cells starting at the current CellSpace `here`, then
advances `here` by `n`.

Newly allotted cells are initialized to Froth numeric zero.

In v1, `allot` is **definition-time / top-level only**. Executing it inside an
ordinary quotation or running word is an error.

Reason:

- the immediate pressure is fixed layout, not general runtime allocation,
- definition-time-only CellSpace is much easier to reason about for embedded
  persistence and demo reliability,
- and it keeps CellSpace from becoming a second general-purpose heap.

#### `variable`

`variable` is shorthand for `create 1 allot`, with the single cell initialized
to zero.

It exists to collapse ceremony for the common "one mutable cell with a name"
case.

#### `@` and `!`

`@` reads one tagged cell from CellSpace.

`!` writes one tagged cell to CellSpace.

Neither word performs native memory access. They are managed-region words only.

### 6. Managed CellSpace access is bounds-checked

Managed CellSpace should not silently corrupt VM state.

Therefore:

- `@` and `!` bounds-check against the currently allocated prefix
  `[0, cellspace_used)`,
- out-of-range access throws a bounds error,
- negative addresses are rejected,
- `allot` beyond capacity throws a dedicated CellSpace-full error.

This is a deliberate divergence from classic Forth's unchecked memory model.

Froth can preserve Forth-like directness without inheriting every Forth
footgun. Raw unsafe access remains the boundary-layer job of FROTH-Addr and
FFI.

### 7. Capacity is fixed and compile-time configurable

CellSpace capacity is fixed by CMake:

- `FROTH_DATA_SPACE_SIZE`
- default target for v1: `256` cells

The VM tracks:

- `cellspace_used`
- `cellspace_base_mark`

Implementations may also track:

- a boot-time base seed copy for wipe/restore support (see below).

### 8. Snapshot persistence integrates CellSpace as a first-class region

CellSpace is not a side channel. It is part of the language state.

The snapshot format therefore gains a dedicated CellSpace section:

- `cellspace_used`
- serialized cell values for the allocated prefix

The serializer must treat CellSpace as another root set:

- heap objects reachable from CellSpace values must be traversed just like heap
  objects reachable from slot implementations,
- transient strings and future non-persistable values stored in CellSpace are
  rejected using the same rules already applied to slots.

#### Why persist the full allocated prefix?

Slot snapshots can track overlay ownership per slot. CellSpace is different:

- base-defined regions may be **mutated** by user code,
- simply persisting the overlay suffix would lose mutations to base-created
  cells,
- and a mutable region is easier to reason about if its current image is
  serialized directly.

Therefore the snapshot stores the **full allocated CellSpace prefix**, not just
an overlay suffix.

#### Boot / wipe / restore rules

The VM records a `cellspace_base_mark` after base boot completes.

If base code allocated any CellSpace cells before `boot_complete`, the runtime
must be able to restore those cells on `wipe`.

The reference design therefore records a `cellspace_base_seed` copy of
`[0, cellspace_base_mark)`.

Visible semantics:

- `wipe` restores base CellSpace contents and resets `cellspace_used` to the
  base mark,
- `restore` resets to the base seed, then overlays the saved CellSpace image,
- `save` writes the current full allocated prefix.

This keeps `wipe` honest even when base-defined CellSpace values were mutated
during the session.

### 9. CellSpace does not replace slots

This ADR is not a rollback of slots or ADR-017.

After this change:

- slots remain the naming and redefinition mechanism,
- slot-backed scalar values remain valid and useful,
- CellSpace becomes the preferred mechanism for indexed mutable aggregates.

The correction is separation, not replacement.

## Consequences

- Froth gains a direct way to express arrays, framebuffers, lookup tables,
  ring-buffer state, and small mutable records.
- TM1629-style code can move away from quotations-of-slots and many individual
  scalar slots.
- The language becomes more Forth-like in data layout work without collapsing
  native-address safety boundaries.
- Snapshot complexity increases: CellSpace becomes an additional persisted VM
  region.
- `wipe` semantics require explicit handling of base-created CellSpace state,
  not just resetting a bump pointer.
- The `BufferRef` direction is no longer the default answer to mutable
  aggregates. If a future byte-oriented or handle-based buffer profile is still
  useful, it should be built **after** CellSpace proves insufficient for a
  specific class of problems.

## Non-goals

- No byte-addressed or sub-cell access in v1
- No runtime `allot`
- No `DOES>` in this ADR
- No struct/record DSL in this ADR
- No replacement of FROTH-Addr for native machine addresses
- No claim that CellSpace alone fixes FROTH-Named's current static-subset
  ceiling

## References

- `docs/archive/spec/Froth_Language_Spec_v1_1.md`
- `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md`
- `docs/archive/adr/017-def-accepts-any-value.md`
- `docs/archive/adr/024-native-address-profile.md`
- `docs/archive/adr/026-snapshot-persistence.md`
- `docs/archive/adr/043-transient-string-buffer.md`
- `docs/archive/adr/050-staged-first-froth-named-implementation.md`
- `docs/archive/concepts/froth-cellspace-v1-implementation-checklist-2026-04.md`
