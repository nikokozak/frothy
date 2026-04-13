# FROTH-CellSpace v1 Implementation Checklist

Date: 2026-04-02
Status: planning note
Authority: subordinate to `docs/spec/` and `docs/adr/054-first-froth-cellspace-profile.md`

This note turns ADR-054 into a concrete implementation plan for the current C
runtime. It is not a new language decision. If this checklist conflicts with
ADR-054 or the specs, the ADR/spec wins and this note should be corrected.

Current implementation state in tree:

- Step 1 is landed.
- Step 2 is landed.
- Step 3 is landed.
- Step 4 is landed.
- Step 6 is landed.
- Step 7 is landed.
- The next follow-on proof is the TM1629 rewrite.

## Goal

Land the first `FROTH-CellSpace` slice with the smallest implementation that
still proves the design:

- tagged-cell mutable aggregate storage
- cell-indexed numeric addresses
- honest `wipe` / `restore` behavior
- bounds-checked `@` / `!`
- direct enough to rewrite the pure TM1629 exercise library against it

The plan should **not** try to solve strings, byte buffers, `DOES>`, or named
hardening in the same patch series.

## User-Facing Contract

The user-facing v1 should be:

```froth
'rows create
8 allot

'counter variable

42 rows !
rows @
```

Rules:

1. `create`, `allot`, and `variable` are top-level / defining words in v1.
2. `@` and `!` operate on CellSpace only, not native memory.
3. Addresses are ordinary Numbers representing cell offsets.
4. `cells` exists for readability but is identity in v1 because addressing is
   already cell-indexed.
5. `cell+` advances by one cell.
6. `allot` initializes new cells to numeric zero.

## Scope Gates

Lock these before writing code:

1. No new value tag for CellSpace addresses.
2. No byte-addressed or sub-cell access in v1.
3. No runtime `allot`.
4. No `DOES>`, struct DSL, or defining-word metaprogramming in this tranche.
5. No attempt to remove or redesign the current transient-string machinery in
   this tranche. ADR-056 is a separate follow-on track.
6. No attempt to solve FROTH-Named hardening in this tranche.
7. `mark` / `release` remains heap-only. CellSpace is not region-managed.

## Implementation Shape

### Chosen runtime shape

Use a dedicated CellSpace runtime module rather than open-coding another set of
VM fields directly in primitives.

Recommended shape:

```c
typedef struct {
  froth_cell_t *data;
  froth_cell_t *base_seed;
  froth_cell_u_t used;
  froth_cell_u_t capacity;
  froth_cell_u_t base_mark;
} froth_cellspace_t;
```

Why:

- keeps bounds checks and reset logic out of primitives
- keeps boot/restore/wipe behavior centralized
- makes snapshot code talk to one subsystem instead of raw VM fields

## Step 1: Add Stable Config and Error Surface

Status: landed in tree on 2026-04-02.

Files:

- `CMakeLists.txt`
- `targets/esp-idf/main/CMakeLists.txt`
- `src/froth_types.h`
- `src/froth_repl.c`

Tasks:

1. Add `FROTH_DATA_SPACE_SIZE` as a compile-time config knob.
2. Add one dedicated runtime error for CellSpace exhaustion.
3. Add one dedicated runtime error for using a defining word in the wrong
   context.
4. Add REPL text for both errors.

Locked decisions for this tranche:

1. `FROTH_DATA_SPACE_SIZE` is the exact knob name.
2. Default `FROTH_DATA_SPACE_SIZE` is `256` on both host and ESP-IDF for v1.
   - Do not introduce a target-specific exception in this patch series.
   - If RAM pressure later forces a lower ESP32 default, make that a separate,
     measured follow-up.
3. The runtime error names are locked as:
   - `FROTH_ERROR_CELLSPACE_FULL`
   - `FROTH_ERROR_TOPLEVEL_ONLY`
4. Append them to the existing runtime error block in `src/froth_types.h`
   rather than renumbering anything older.
   - use `24` for `FROTH_ERROR_CELLSPACE_FULL`
   - use `25` for `FROTH_ERROR_TOPLEVEL_ONLY`
5. REPL text is locked as:
   - `cellspace full`
   - `defining word is top-level only`
6. Add `FROTH_DATA_SPACE_SIZE` to both build paths:
   - root `CMakeLists.txt` cache variable + compile definition
   - `targets/esp-idf/main/CMakeLists.txt` compile definition
7. Add one compile-time sanity check:
   - `FROTH_DATA_SPACE_SIZE` must be at least `1`

Behavior split:

- bad address for `@` / `!` => `FROTH_ERROR_BOUNDS`
- negative `allot` count => `FROTH_ERROR_BOUNDS`
- overflow of the fixed CellSpace => `FROTH_ERROR_CELLSPACE_FULL`
- `create` / `allot` / `variable` from quoted/runtime context =>
  `FROTH_ERROR_TOPLEVEL_ONLY`

Definition of done:

- host and ESP-IDF builds both see `FROTH_DATA_SPACE_SIZE`
- new errors print clear text in the REPL

## Step 2: Add `froth_cellspace.{h,c}` and VM Storage

Status: landed in tree on 2026-04-02.

Files:

- new `src/froth_cellspace.h`
- new `src/froth_cellspace.c`
- `src/froth_vm.h`
- `src/froth_vm.c`
- root `CMakeLists.txt`
- `targets/esp-idf/main/CMakeLists.txt`

Tasks:

1. Introduce the new runtime module with helpers:
   - `froth_cellspace_init`
   - `froth_cellspace_allot`
   - `froth_cellspace_fetch`
   - `froth_cellspace_store`
   - `froth_cellspace_capture_base_seed`
   - `froth_cellspace_reset_to_base`
2. Add static storage in `froth_vm.c`:
   - active CellSpace array
   - base-seed copy array
3. Thread a `froth_cellspace_t cellspace` field into `froth_vm_t`.
4. Wire the module storage in `froth_vm.c`; runtime init happens at boot.

Exact runtime shape for this tranche:

```c
typedef struct {
  froth_cell_t *data;
  froth_cell_t *base_seed;
  froth_cell_u_t used;
  froth_cell_u_t capacity;
  froth_cell_u_t base_mark;
} froth_cellspace_t;
```

Header/API guidance:

1. `src/froth_cellspace.h` should depend only on `froth_types.h`.
   - do not make the CellSpace module depend on the VM, slots, snapshots, or
     the evaluator
2. Use these helper signatures unless implementation pressure forces a trivial
   rename:

```c
void froth_cellspace_init(froth_cellspace_t *cellspace);
froth_error_t froth_cellspace_allot(froth_cellspace_t *cellspace,
                                    froth_cell_t count,
                                    froth_cell_t *base_addr_out);
froth_error_t froth_cellspace_fetch(const froth_cellspace_t *cellspace,
                                    froth_cell_t addr,
                                    froth_cell_t *value_out);
froth_error_t froth_cellspace_store(froth_cellspace_t *cellspace,
                                    froth_cell_t addr,
                                    froth_cell_t value);
void froth_cellspace_capture_base_seed(froth_cellspace_t *cellspace);
void froth_cellspace_reset_to_base(froth_cellspace_t *cellspace);
```

3. Addresses crossing the primitive boundary stay as signed `froth_cell_t`
   Numbers.
   - primitives should not need to reinterpret unsigned indices themselves
4. Stored CellSpace values are opaque tagged cells.
   - `froth_cellspace_store` does not type-check the stored value
   - `froth_cellspace_fetch` and `froth_cellspace_store` only validate the
     address

VM/storage guidance:

1. `src/froth_vm.c` owns the backing arrays:
   - one active CellSpace array of `FROTH_DATA_SPACE_SIZE` cells
   - one base-seed copy array of `FROTH_DATA_SPACE_SIZE` cells
2. `src/froth_vm.h` adds a `froth_cellspace_t cellspace` field to the VM.
3. `froth_vm.c` should wire the pointers and capacity in the static VM
   initializer so the storage ownership is obvious.
4. `froth_cellspace_init` should be callable at boot to reset runtime state.
   - set `used = 0`
   - set `base_mark = 0`
   - zero both the active and base-seed arrays for deterministic boot and test
     behavior

Required helper semantics:

1. `allot(count)`:
   - accepts `count >= 0`
   - returns the old `used` as `base_addr_out`
   - advances `used` by `count`
   - zero-initializes only the newly exposed range
   - returns `FROTH_ERROR_BOUNDS` on negative `count`
   - returns `FROTH_ERROR_CELLSPACE_FULL` if `used + count > capacity`
2. `fetch(addr)` / `store(addr, value)`:
   - accept only `0 <= addr < used`
   - return `FROTH_ERROR_BOUNDS` on negative or out-of-range addresses
3. `capture_base_seed()`:
   - copies `[0, used)` from `data` to `base_seed`
   - sets `base_mark = used`
   - does not change `used`
4. `reset_to_base()`:
   - copies `[0, base_mark)` from `base_seed` back into `data`
   - sets `used = base_mark`
   - does not attempt allocator-like free lists or region semantics

Implementation notes:

1. It is acceptable for cells above `used` to retain stale bytes after reset.
   - they are out of bounds by definition
   - future `allot` must zero any range it newly exposes
2. Do not add overlay ownership, snapshot traversal, or persistence policy to
   this module.
   - those belong to boot/snapshot wiring later
3. Do not introduce byte-addressing helpers, sub-cell access, or pointer-like
   arithmetic here.

Required semantics:

- `allot(n)` returns the old `used` as the base address, then advances `used`
- negative `n` is rejected
- new cells are initialized to tagged numeric zero
- `fetch` / `store` accept only `0 <= addr < used`
- `reset_to_base` restores `[0, base_mark)` from `base_seed` and sets
  `used = base_mark`

Do not:

- add byte access
- add allocator-like free/reclaim behavior
- add overlay tracking inside CellSpace itself

Definition of done:

- the VM owns a fixed mutable tagged-cell region
- the runtime can zero-init, allot, fetch, store, and reset it without
  primitives knowing about its internals

## Step 3: Wire Boot, Reset, and Wipe Semantics

Status: landed in tree on 2026-04-02.

Files:

- `src/froth_boot.c`
- `src/froth_primitives.c`
- `src/froth_snapshot_reader.c`

Tasks:

1. Initialize CellSpace at boot before loading stdlib/board libs.
2. After base boot completes, capture the CellSpace base seed and mark.
3. Keep base-created CellSpace non-overlay by preserving the current
   `boot_complete` ordering.
4. Update `dangerous-reset` to restore CellSpace base state.
5. Update snapshot restore reset-to-base path to restore CellSpace base state.
6. Ensure `wipe` ends in the same base CellSpace state as a fresh boot.

Exact call-site plan:

1. In `src/froth_boot.c`, call `froth_cellspace_init(&froth_vm.cellspace)`
   before evaluating:
   - `froth_lib_core`
   - board pins
   - board lib
2. Keep the existing base-boot sequence shape:
   - register primitives / board / snapshot words
   - `platform_init`
   - `froth_tbuf_init`
   - `froth_cellspace_init`
   - evaluate stdlib / board pins / board lib
   - capture CellSpace base seed
   - set `boot_complete = 1`
   - capture heap watermark
   - proceed to safe-boot / restore / user program / autorun
3. The CellSpace base snapshot is captured exactly once per boot.
   - `restore`
   - `wipe`
   - `dangerous-reset`
   - user programs
   do **not** recapture or mutate `base_mark` / `base_seed`

Reset-path guidance:

1. `froth_prim_dangerous_reset` in `src/froth_primitives.c` must call
   `froth_cellspace_reset_to_base(&vm->cellspace)` as part of the base reset
   path.
2. `reset_overlay_to_base` in `src/froth_snapshot_reader.c` must become the
   single shared "return runtime to base image" helper for:
   - heap watermark reset
   - overlay-slot reset
   - CellSpace base reset
3. `wipe` should keep its current shape:
   - erase snapshot slot A
   - erase snapshot slot B
   - delegate to the dangerous-reset path
4. Step 3 does **not** require a transactional restore rewrite.
   - the goal here is to make the existing reset-to-base path CellSpace-aware
   - the actual CellSpace payload load comes in Step 7

Behavioral rules to keep explicit while implementing:

1. Base-created CellSpace is part of the firmware image, not overlay state.
2. That means `boot_complete` must still flip only **after** stdlib / board
   code has had a chance to run defining words.
3. `wipe` must restore the captured base CellSpace image, not merely set
   `used = 0`.
4. `restore` will later rely on the same base-reset helper before applying the
   saved full-prefix CellSpace image.
5. Do not try to fake base CellSpace recreation by re-evaluating board or
   stdlib Froth after `wipe`.

Recommended boot order:

1. init VM core
2. init CellSpace
3. load stdlib / board pins / board lib
4. capture `cellspace.base_mark` and `cellspace.base_seed`
5. set `boot_complete = 1`
6. proceed to restore / user program / autorun

Important rule:

- base-created CellSpace must be real base state, not reconstructed by ad hoc
  library re-evaluation after `wipe`
- Step 3 only wires the reset/capture path; actual snapshot serialization of
  CellSpace remains Step 7

Definition of done:

- `dangerous-reset` and `wipe` both restore the captured CellSpace base image
- the shared reset-to-base helper used by snapshot restore is CellSpace-aware
- the boot path captures CellSpace base state before `boot_complete = 1`

## Step 4: Add Primitive Surface

Status: landed in tree on 2026-04-02.

Files:

- `src/froth_primitives.c`
- `src/froth_primitives.h` if you choose non-static declarations
- `src/lib/core.froth`

Kernel words in this step:

- `create ( slot -- )`
- `allot ( n -- )`
- `variable ( slot -- )`
- `@ ( addr -- value )`
- `! ( value addr -- )`

Recommended follow-on helpers in the same or immediate next patch:

- `cells ( n -- n )`
- `cell+ ( addr -- addr' )`
- `+! ( delta addr -- )`

Implementation choices:

1. `create`, `allot`, and `variable` should be C primitives in v1.
   - Do **not** try to define `variable` in `core.froth`.
   - If `variable` were a Froth word, it would execute under a quotation frame,
     which would immediately violate the v1 top-level-only rule.
2. `@` and `!` should be ordinary runtime primitives.
3. `cells`, `cell+`, and `+!` can be defined in `core.froth` once `@` / `!`
   exist.

Locked Step 4 decisions:

1. The primitive names are exactly:
   - `create`
   - `allot`
   - `variable`
   - `@`
   - `!`
2. These remain the CellSpace words, not native-address words.
   - do not overload them with FROTH-Addr semantics
   - future raw/native memory access stays on the width-specific `@8` / `@16` /
     `@32` / `!8` / `!16` / `!32` track
3. Top-level-only enforcement for v1 is locked to:
   - `vm->cs.pointer == 0`
4. `create` and `variable` are ordinary slot-binding operations.
   - they should reject primitive redefinition exactly the same way `value` /
     `def` already do
   - they should honor `boot_complete` exactly the same way other binders do
5. `create` and `variable` stamp arity `(0 -- 1)`.
6. `allot` does not bind any slot and has no DS output.
7. `@` and `!` do not type-check the fetched/stored value beyond validating the
   address argument.
8. `cells`, `cell+`, and `+!` stay out of C in this tranche.

Primitive details:

Recommended C primitive names:

- `froth_prim_create`
- `froth_prim_allot`
- `froth_prim_variable`
- `froth_prim_fetch`
- `froth_prim_store`

It is fine if they stay file-local in `src/froth_primitives.c`.
Do not add evaluator hooks or new parser entry points.

Recommended internal helpers in `src/froth_primitives.c`:

1. A top-level guard helper, for example:
   - `require_toplevel_only(vm)`
2. A slot-address binder helper, for example:
   - `bind_slot_to_cellspace_addr(vm, slot_cell, addr)`
3. An address pop/check helper for `@` / `!`, for example:
   - `pop_cellspace_addr(vm, &addr)`

The exact helper names are not important.
The important point is that top-level checks, slot-binding policy, and address
validation should not be open-coded five different ways.

Current-code style note:

- prefer reusing the existing `prepare_slot_binding(...)` and
  `bind_slot_impl(...)` helpers for `create` / `variable`
- if you add a CellSpace-specific binder helper, it should wrap those existing
  paths rather than create a second slot-binding policy

### `create`

- input must be a SlotRef
- context must be top-level (`vm->cs.pointer == 0` in v1)
- bind the slot impl to the current `cellspace.used` as a Number
- stamp arity `(0 -- 1)`
- do **not** allocate cells itself

Implementation shape:

1. Pop one DS item: `slot_cell`
2. Reject non-SlotRef with `FROTH_ERROR_TYPE_MISMATCH`
3. Reject non-top-level execution with `FROTH_ERROR_TOPLEVEL_ONLY`
4. Read the current address from `vm->cellspace.used`
5. Convert that address to a tagged Number
6. Reuse the existing slot-binding path rather than bypassing slot policy
   entirely
   - primitive redefinition check still applies
   - overlay flag still comes from `boot_complete`
7. Set slot arity to `(0 -- 1)`
8. Leave no DS output

Clarification:

- `create` is a binder, not an allocator.
- If a user writes:

```froth
'rows create
8 allot
```

the slot binding happens in `create`; the storage reservation happens in
`allot`.

### `allot`

- input must be a non-negative Number
- context must be top-level
- reserve that many cells
- initialize the new prefix to numeric zero
- no DS output

Implementation shape:

1. Pop one DS item: `count_cell`
2. Reject non-Number with `FROTH_ERROR_TYPE_MISMATCH`
3. Reject non-top-level execution with `FROTH_ERROR_TOPLEVEL_ONLY`
4. Delegate all capacity / negative-count handling to
   `froth_cellspace_allot(...)`
5. Discard the returned base address in the primitive path
6. Leave no DS output

Error split stays explicit:

- negative count => `FROTH_ERROR_BOUNDS`
- fixed-region overflow => `FROTH_ERROR_CELLSPACE_FULL`

### `variable`

- input must be a SlotRef
- context must be top-level
- equivalent to "bind current here address, then allot 1"
- stamp arity `(0 -- 1)`

Implementation shape:

1. Pop one DS item: `slot_cell`
2. Reject non-SlotRef with `FROTH_ERROR_TYPE_MISMATCH`
3. Reject non-top-level execution with `FROTH_ERROR_TOPLEVEL_ONLY`
4. Call `froth_cellspace_allot(..., 1, &base_addr)` first
   - this gives one authoritative path for zero-init and capacity checks
5. Bind `slot_cell` to `base_addr` as a tagged Number
6. Set slot arity to `(0 -- 1)`
7. Leave no DS output

Why prefer "allot then bind" over open-coding `create 1 allot` in C:

- one allocation path owns zero-init and overflow behavior
- the primitive does not need to duplicate the "current here" arithmetic
- if `allot(1)` fails, the slot binding never changes

### `@`

- input must be a Number address
- fetch one tagged cell
- reject negative or out-of-range addresses with `FROTH_ERROR_BOUNDS`

Implementation shape:

1. Pop one DS item: `addr_cell`
2. Reject non-Number with `FROTH_ERROR_TYPE_MISMATCH`
3. Delegate address validation and load to `froth_cellspace_fetch(...)`
4. Push the returned tagged cell to DS

Clarification:

- `@` reads CellSpace only.
- It does not inspect, copy, or coerce the stored value.
- Stored QuoteRefs, SlotRefs, StringRefs, and other tagged cells remain intact.

### `!`

- input order: `( value addr -- )`
- `addr` must be a Number within the allocated prefix
- store the tagged value directly, no conversion

Implementation shape:

1. Pop `addr_cell` first
2. Pop `value_cell` second
3. Reject non-Number `addr_cell` with `FROTH_ERROR_TYPE_MISMATCH`
4. Delegate address validation and store to `froth_cellspace_store(...)`
5. Leave no DS output

Clarification:

- `!` stores the tagged cell exactly as supplied.
- It is valid to store Numbers, SlotRefs, QuoteRefs, PatternRefs, StringRefs,
  and future persistable tagged values.
- It must not reinterpret the value as raw bytes or raw addresses.

Recommended stdlib helpers:

```froth
: cells ( n -- n ) ;
: cell+ ( addr -- addr' ) 1 + ;
: +! ( delta addr -- ) dup @ rot + swap ! ;
```

Stdlib placement and scope:

1. Define these in `src/lib/core.froth`, not in C.
2. Keep the helper set intentionally tiny in this patch:
   - `cells`
   - `cell+`
   - `+!`
3. Defer `cell-`, `2@`, `2!`, `constant`, or any struct DSL.

Primitive table / help-text guidance:

1. Register all five new primitives in the existing `froth_primitives[]` table.
2. Give them explicit stack-effect strings and short help text.
3. `info` updates belong to Step 6, not Step 4.
4. If the new primitive functions remain file-local `static`, no
   `src/froth_primitives.h` prototype additions are required.

Testing expectations for this step:

1. The primitive patch should be usable directly from the REPL before any
   snapshot work lands.
2. The following interactions should work once Step 4 is done:

```froth
'rows create
8 allot
42 rows !
rows @

'counter variable
7 counter !
counter @
```

3. The following should fail clearly:

```froth
: bad 'tmp create ;
: bad2 4 allot ;
: bad3 'tmp variable ;
```

Definition of done:

- the REPL can define CellSpace-backed scalars and arrays without any evaluator
  special cases
- using `create` / `allot` / `variable` inside a running definition fails
  clearly instead of silently mutating global state

## Step 5: Keep Reader/Evaluator Changes Minimal

Files:

- `src/froth_reader.c`
- `src/froth_evaluator.c`

Expected outcome:

- no syntax changes are required for v1
- the existing ticked-surface plan is enough

Tasks:

1. Confirm `@` and `!` already tokenize as ordinary identifiers.
2. Do not add classic `create name` parsing in this tranche.
3. Do not add evaluator special-casing for CellSpace if the primitive path is
   sufficient.

Why:

- ADR-054 explicitly chose the existing ticked surface for v1
- minimizing evaluator work reduces risk before the workshop

Definition of done:

- `'rows create 8 allot` works with current reader/evaluator behavior

## Step 6: Add Observability

Status: landed in tree on 2026-04-03.

Files:

- `src/froth_primitives.c`
- optionally `src/froth_link.c`

Tasks:

1. Extend `info` to report CellSpace usage:
   - used cells
   - total capacity
   - base-mark split explicitly, not optionally
2. Keep `see` behavior unchanged unless it needs small help text updates.

Locked Step 6 decisions:

1. `info` output order becomes:
   - version / cell width
   - heap line
   - CellSpace line
   - slots line
2. The exact CellSpace line shape should be:

```text
cellspace: <used> / <capacity> cells (base <base_mark>)
```

3. Use:
   - `vm->cellspace.used`
   - `vm->cellspace.capacity`
   - `vm->cellspace.base_mark`
4. Do not add a new primitive for this.
   - extend the existing `froth_prim_info`
5. Do not change `see`.
6. Do not change link/protocol info payloads in this tranche.
   - host tooling can continue scraping plain REPL text if needed

Implementation shape:

1. In `src/froth_primitives.c`, extend `froth_prim_info(...)` only.
2. Emit the new CellSpace line after the heap line and before the slots line.
3. Keep the existing heap "user bytes" reporting unchanged.
4. If `base_mark == used`, still print the `(base <n>)` suffix.
   - avoid conditional formatting branches that make tests brittle

Recommended test coverage for Step 6:

1. `info` on a fresh boot contains:
   - `cellspace: 0 /`
2. after:

```froth
'rows create
3 allot
```

   `info` should contain:
   - `cellspace: 3 /`
3. The CellSpace line should appear between the heap and slots lines.

Why:

- once arrays exist, users need quick confirmation of capacity and current use
- this helps workshop demos and failure diagnosis

Definition of done:

- `info` shows CellSpace alongside heap/slot usage

## Step 7: Extend Snapshot Format for Full CellSpace Prefix Persistence

Status: landed in tree on 2026-04-03.

Files:

- `src/froth_snapshot.h`
- `src/froth_snapshot.c`
- `src/froth_snapshot_writer.c`
- `src/froth_snapshot_reader.c`
- `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md` (after implementation settles)

This is the most delicate part of ADR-054.

Locked Step 7 decisions:

1. Bump `FROTH_SNAPSHOT_VERSION` from `0x0004` to `0x0005`.
2. Stop treating `1024` as the payload ceiling.
3. Replace the old `FROTH_SNAPSHOT_MAX_BYTES` assumption with an exact
   payload-capacity constant derived from the block size.
4. Persist the full allocated CellSpace prefix `[0, used)`, not only the
   post-base suffix.
5. Keep the existing names / objects / bindings sections in the same order, and
   append CellSpace after them.
6. Reuse one generic persistable-cell encoder/decoder for:
   - overlay slot bindings
   - CellSpace cells
7. `CALL` remains invalid serialized data.
8. For this tranche, the supported stored kinds are exactly the kinds the
   current snapshot object pipeline already supports end-to-end:
   - Number
   - QuoteRef
   - PatternRef
   - StringRef (`BSTRING`)
   - SlotRef
   - ContractRef only if object emit/load support is added in the same patch
9. `wipe` semantics must remain "restore base CellSpace image", not "set used
   to zero".

### 7a. Fix the current snapshot capacity assumption first

Before serializing CellSpace, remove the old hidden 1024-byte assumption:

- `src/froth_snapshot.h` currently hardcodes `FROTH_SNAPSHOT_MAX_BYTES 1024`
- a 256-cell 32-bit CellSpace prefix alone already consumes 1024 bytes

Tasks:

1. Replace `FROTH_SNAPSHOT_MAX_BYTES` with:

```c
#define FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES \
  (FROTH_SNAPSHOT_BLOCK_SIZE - FROTH_SNAPSHOT_HEADER_SIZE)
```

2. Update `froth_snapshot_workspace_t.ram_buffer` to use
   `FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES`.
3. Update:
   - writer emit helpers
   - restore payload_len check
   to use `FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES`.
4. Add compile-time checks in `src/froth_snapshot.h`:
   - `FROTH_SNAPSHOT_BLOCK_SIZE > FROTH_SNAPSHOT_HEADER_SIZE`
   - full-prefix CellSpace lower bound must fit:

```c
2 + 4 + 4 + 4 + (FROTH_DATA_SPACE_SIZE * sizeof(froth_cell_t))
```

   That lower bound corresponds to:
   - zero names
   - zero objects
   - zero overlay bindings
   - one `cellspace_used` field
   - a fully allocated numeric CellSpace prefix
5. If that lower bound does not fit, fail at compile time.
6. Keep `FROTH_SNAPSHOT_BLOCK_SIZE=2048` unless a real target later proves a
   smaller block is still viable.

The plan should not ship a default CellSpace that cannot possibly be saved.

### 7b. Bump the snapshot format version

Tasks:

1. Set `FROTH_SNAPSHOT_VERSION` to `0x0005`.
2. Do not add compatibility shims for `0x0004`.
   - old snapshots should fail cleanly as incompatible/old-format images
3. Let the ABI hash change with the format version naturally.

### 7c. Add CellSpace payload section

Recommended payload tail:

1. existing names
2. existing objects
3. existing overlay slot bindings
4. `cellspace_used` (`u32`)
5. serialized CellSpace cells for `[0, cellspace_used)`

Persist the **full allocated prefix**, not just post-base suffix.

Exact write/read shape:

1. In `src/froth_snapshot_writer.c`, extend
   `froth_snapshot_write_payload(...)` to:
   - `emit_names(...)`
   - `emit_objects(...)`
   - `emit_bindings(...)`
   - `emit_cellspace(...)`
2. Add a new writer helper:
   - `emit_cellspace(snapshot, &vm->cellspace, name_table, object_table)`
3. `emit_cellspace(...)` must:
   - emit `cellspace.used` as `u32`
   - then emit one persistable cell record per index `[0, used)`
4. In `src/froth_snapshot_reader.c`, extend `froth_snapshot_load(...)` to:
   - `reset_overlay_to_base(...)`
   - `read_names(...)`
   - `load_objects(...)`
   - `load_bindings(...)`
   - `load_cellspace(...)`
5. Add a new reader helper:
   - `load_cellspace(reader, names, objects, &vm->cellspace)`
6. `load_cellspace(...)` must:
   - read `cellspace_used` as `u32`
   - reject `cellspace_used > cellspace.capacity` with
     `FROTH_ERROR_SNAPSHOT_FORMAT`
   - decode and store exactly `cellspace_used` cells into
     `cellspace.data[0..used)`
   - set `cellspace.used = cellspace_used`
7. Do not recapture `base_seed` or mutate `base_mark` during snapshot load.

### 7d. Reuse one generic "persistable cell" encoder

Current snapshot code has slot-binding-specific encoding paths. CellSpace needs
the same value universe again.

Refactor toward shared helpers:

- `emit_persistable_cell`
- `decode_persistable_cell`

Supported stored kinds in CellSpace should match the persisted tagged-value
surface:

- Number
- QuoteRef
- PatternRef
- StringRef
- SlotRef
- ContractRef only if the current snapshot path gains object emit/load support
  in this same patch

Internal `CALL` cells should still be treated as invalid serialized data.

Exact refactor target:

1. In `src/froth_snapshot_writer.c`:
   - replace `emit_binding_impl(...)` with
     `emit_persistable_cell(...)`
   - make both `emit_bindings(...)` and `emit_cellspace(...)` call it
2. In `src/froth_snapshot_reader.c`:
   - replace `decode_binding_impl(...)` with
     `decode_persistable_cell(...)`
   - make both `load_bindings(...)` and `load_cellspace(...)` call it
3. Do not create a second encoder path that drifts from bindings.
4. `emit_quote_token(...)` / `load_quote_token(...)` should stay separate.
   - quotation token encoding still needs to preserve `CALL` tokens inside quote
     bodies
   - the generic persistable-cell helper is for normal stored values only

### 7e. Collect dependencies from CellSpace values

The writer currently walks overlay slots and quotations. CellSpace adds a new
root set.

Tasks:

1. Scan `[0, cellspace.used)` during dependency collection.
2. If a cell contains:
   - `SlotRef`: add its name to the name table
   - `PatternRef` / `StringRef` / `ContractRef`: add object
   - `QuoteRef`: add the quote object **and** recursively collect the quote's
     dependencies
3. Keep transient-string rejection for the current implementation until ADR-056
   actually lands in code.

Important:

- current `collect_cell_dependencies` is not enough for direct QuoteRefs
- CellSpace can hold a QuoteRef as a normal stored value, so quote recursion
  must be explicit

Exact implementation shape:

1. Replace the current narrow `collect_cell_dependencies(...)` helper with one
   helper that has access to the VM, for example:
   - `collect_persistable_cell_dependencies(vm, cell, name_table, object_table)`
2. That helper should:
   - recurse into `collect_quote_dependencies(...)` when the cell is a direct
     QuoteRef
   - add names for SlotRefs
   - add objects for PatternRef / StringRef
   - reject transient strings
3. Then extend `collect_snapshot_dependencies(...)` in two passes:
   - existing overlay slot scan
   - new CellSpace scan across `[0, vm->cellspace.used)`
4. During the overlay slot scan, reuse the same persistable-cell dependency
   helper rather than open-coding special cases again.

### 7f. Reset/load behavior

Tasks:

1. `reset_overlay_to_base` must restore heap, slots, and CellSpace base state.
2. After names/objects/bindings load, load the CellSpace image.
3. Overwrite the active prefix and set `used` to the serialized count.

Behavioral rules to keep explicit:

1. Snapshot load starts from the already-captured base CellSpace image.
2. The loaded prefix may overwrite both:
   - base-created cells
   - user-created cells
3. Cells above the restored `used` are irrelevant after load.
   - bounds checks make them unreachable
   - do not add an expensive zeroing pass here
4. `wipe` still means:
   - erase both snapshot slots
   - return to the captured base image through the shared reset path

Recommended test additions in this tranche:

1. `info` reflects CellSpace use after `create`/`allot`.
2. `save` / `restore` round-trip numeric CellSpace contents.
3. `save` / `restore` round-trip at least one non-numeric stored value
   (prefer SlotRef or QuoteRef).
4. `wipe` restores base-created CellSpace contents, not just `used = 0`.
5. Oversized `cellspace_used` in a malformed snapshot is rejected.

Definition of done:

- save/restore/wipe round-trip CellSpace correctly
- base-created CellSpace survives `wipe` honestly
- impossible size combinations fail at build time, not at the workshop bench

## Step 8: Add Focused Kernel Tests

Files:

- new `tests/kernel/test_cellspace.sh`
- `tests/kernel/test_persistence.sh`
- maybe `tests/kernel/test_tm1629.sh`

Minimum kernel test coverage:

1. `create` + `allot` basic addressing:
   - first region starts at `0`
   - second region starts after the first
2. `variable` creates one-cell mutable storage
3. `@` / `!` store and fetch tagged Numbers
4. `@` / `!` store and fetch non-numeric tagged values (at least one SlotRef or
   QuoteRef case)
5. negative address => bounds error
6. address `>= used` => bounds error
7. `allot` past capacity => `CELLSPACE_FULL`
8. `create` / `allot` / `variable` inside a running definition => `TOPLEVEL_ONLY`
9. `save` / `restore` round-trip CellSpace state
10. `wipe` restores base CellSpace state, not just `used = 0`

For the base-state wipe test, use a tiny controlled boot fixture rather than
waiting for TM1629. The test should prove base-seed restoration directly.

## Step 9: Rewrite the Pure TM1629 Library as the Proof

Files:

- `tests/legacy/tm1629d/pure/tm1629d.froth`
- `tests/legacy/tm1629d/README.md`
- `tests/kernel/test_tm1629.sh`

Rewrite goals:

1. Replace slot-backed row/next-row pseudo-arrays with CellSpace-backed storage.
2. Remove quotation-of-slot indirection for row access.
3. Keep the public API stable.
4. Re-run the existing proof scenarios after the rewrite.

This is the success criterion for ADR-054, not just a nice extra.

Definition of done:

- the library reads more directly than the current slot-heavy version
- the kernel test still passes
- the rewrite demonstrates that mutable aggregates no longer need language
  workarounds

## Step 10: Documentation Sync After Code Lands

Files:

- `docs/archive/spec/Froth_Language_Spec_v1_1.md`
- `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md`
- `PROGRESS.md`
- `TIMELINE.md`

Tasks:

1. Promote ADR-054 from proposed once implementation and tests are real.
2. Update the main language spec with the final v1 CellSpace surface and
   context rules.
3. Update the snapshot spec for the new payload shape/version.
4. Record landed helper words and the TM1629 proof in progress docs.

## Suggested Patch Order

Do this in four reviewable patches, not one lump:

1. Config + errors + `froth_cellspace.{h,c}` + VM integration
2. Primitive surface + `info` + focused kernel tests (no snapshots yet)
3. Snapshot integration + wipe/reset/base-seed tests
4. TM1629 rewrite proof + doc sync

This keeps the risk front-loaded and makes it obvious which part broke if the
work stalls.

## Explicit Non-goals for This Checklist

- No byte/PAD profile
- No string redesign work from ADR-056
- No named-backend hardening from ADR-055
- No classic `create name` reader syntax
- No `DOES>`
- No generalized struct or record system
