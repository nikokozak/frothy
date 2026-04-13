# ADR-026: Snapshot Persistence Implementation

**Date**: 2026-03-08
**Status**: Accepted
**Spec sections**: Froth_Snapshot_Overlay_Spec_v0_5 (entire), Froth_Language_Spec_v1_1 sections on persistable form

## Context

Froth needs to survive power cycles. The spec defines an overlay model: base-layer words (primitives, stdlib, FFI) are rebuilt from C every boot; overlay words (user definitions created via `def` after boot) are serialized to a binary snapshot and restored on next boot.

The spec provides the format (header + payload with name table, dependency-ordered objects, and slot bindings) and the algorithms (A/B atomic writes, single-pass restore). This ADR records the implementation-level decisions that the spec leaves to the implementor, plus two deliberate spec deviations.

Nine decisions are bundled here because they're tightly coupled — each one constrains the others.

## Decisions

### 1. Overlay tracking: per-slot flag

**Options considered:**

- **(A) Slot table watermark.** Record `slot_pointer` after boot. Slots at index >= watermark are overlay. Simple, but cannot track redefinition of base words (e.g., user redefines `+`).
- **(B) Per-slot flag.** Add `uint8_t overlay` to `froth_slot_t`. Set it in `def` when `vm->boot_complete` is true.

**Decision: (B).** The 1-byte cost per slot is negligible (128 bytes at `FROTH_SLOT_TABLE_SIZE=128`, likely free due to struct padding). Handles base-word redefinition correctly. Requires a `boot_complete` flag on the VM so that `def` during stdlib loading doesn't mark slots as overlay.

### 2. Snapshot token/object tags reuse `froth_tag_t` values (spec update)

Snapshot token tags and object kind values reuse the in-memory `froth_tag_t` enum directly (NUMBER=0, QUOTE=1, SLOT=2, PATTERN=3, BSTRING=4, CONTRACT=5, CALL=6). This eliminates a separate mapping layer between on-wire and in-memory representations.

**Decision:** Both token tags and `obj_kind` use the same numeric values as `froth_tag_t`. CALL (tag 6) is used for slot invocation inside quotation bodies; SLOT (tag 2) is used for push-reference (`'foo`). The spec has been updated to reflect this.

### 3. Magic bytes: 8 bytes, `FRTHSNAP\0`

The spec says `magic` is 8 bytes but the string `FROTHSNAP` is 9 characters.

**Decision:** 8-byte magic field: 7 ASCII bytes `FRTHSNAP` followed by a null byte. Matches the spec's 8-byte field size. Header total: 50 bytes, consistent with spec.

### 4. All value types persistable (spec deviation)

The spec restricts slot binding `impl_kind` to `1 = QUOTE`. Since Froth's `def` accepts any value (ADR-017), a slot could hold a NUMBER, SLOT reference, PATTERN, BSTRING, or CONTRACT — not just quotations.

**Decision:** Extend `impl_kind` to cover all value types:

| impl_kind | Value type | Encoding after impl_kind byte |
|---|---|---|
| 0x00 | NUMBER | `cell_bytes` signed integer |
| 0x01 | QUOTE | u32 `obj_id` |
| 0x02 | SLOT | u16 `name_id` |
| 0x03 | PATTERN | u32 `obj_id` |
| 0x04 | BSTRING | u32 `obj_id` |
| 0x05 | CONTRACT | u32 `obj_id` |

**Rationale for deviation:** Forcing `42 'answer def` to be wrapped as `: answer 42 ;` is a usability tax with no real benefit. Serialization cost per additional type is ~10 lines. The non-persistable things remain: function pointers (`prim` field, always base-layer) and NativeAddr values (not yet implemented, spec flags as non-persistable).

**Slot binding record** (reserves spec fields for forward compatibility):

| Field | Size | Meaning |
|---|---|---|
| `slot_name_id` | u16 | name table index |
| `impl_kind` | u8 | value type (table above) |
| `impl_ref` | variable | obj_id, name_id, or literal (per impl_kind) |
| `contract_obj_id` | u32 | object ID of CONTRACT, or `0xFFFFFFFF` for none |
| `meta_flags` | u16 | reserved, write 0 |
| `meta_len` | u16 | reserved, write 0 |

CONTRACT attachment and metadata are not used in Stage 1/2 but the fields are present on the wire so that a future contract-aware version can read current snapshots without a format bump.

### 5. Static snapshot buffer

**Decision:** A static `uint8_t` buffer sized by CMake variable `FROTH_SNAPSHOT_SIZE`, default 2048. Used for RAM round-trip (Stage 1). Stage 2 writes directly to files on POSIX.

No dynamic allocation. The buffer lives in BSS (zero-initialized, free on embedded).

### 6. Deduplication via heap offset mapping

Two slots might reference the same heap object (e.g., same quotation assigned to two names). Emitting it twice wastes space and breaks object ID references.

**Decision:** During serialization, maintain a mapping from **heap offset -> object ID**. Before emitting an object, check whether that heap offset has already been assigned an ID. If so, reuse the ID.

Implementation: flat array of `(heap_offset, obj_id)` pairs, linear scan. Object counts are small (typically < 100); a hash map is unnecessary.

### 7. POSIX file paths

**Decision:** CMake variables `FROTH_SNAPSHOT_PATH_A` and `FROTH_SNAPSHOT_PATH_B`, defaulting to `"froth_a.snap"` and `"froth_b.snap"`. Used by the POSIX platform backend for A/B atomic writes.

### 8. Explicit traversal stack

The serializer must walk quotation bodies recursively (quotations can nest). C recursion is unsafe on embedded targets with small stacks.

**Decision:** Explicit traversal stack, fixed size. Default 16 entries (bounded by maximum practical quotation nesting depth). CMake-configurable if needed.

Stack entry: `(heap_offset, body_index)` — enough to resume walking a quotation body after descending into a nested object.

### 9. Snapshot error codes: range 200-299

Consistent with existing ranges (runtime 1-99, reader 100-199, FFI 300+).

| Code | Name | Meaning |
|---|---|---|
| 200 | `FROTH_ERROR_SNAPSHOT_OVERFLOW` | Buffer read/write past end |
| 201 | `FROTH_ERROR_SNAPSHOT_FORMAT` | Bad magic, version, or unknown tag |
| 202 | `FROTH_ERROR_SNAPSHOT_UNRESOLVED` | Heap offset not in object table |
| 203 | `FROTH_ERROR_SNAPSHOT_BAD_CRC` | Header or payload CRC mismatch |
| 204 | `FROTH_ERROR_SNAPSHOT_INCOMPAT` | ABI hash mismatch |
| 205 | `FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT` | No valid snapshot in storage |
| 206 | `FROTH_ERROR_SNAPSHOT_BAD_NAME` | Name exceeds max length |

*Updated Mar 11: renumbered and clarified during Stage 2. Removed dead `SNAP_OOM` (regular `HEAP_OUT_OF_MEMORY` covers it). Added `NO_SNAPSHOT` and `BAD_NAME`. Each code now has exactly one meaning.*

## Consequences

- `froth_slot_t` gains an `overlay` field (1 byte).
- `froth_vm_t` gains a `boot_complete` flag. `main.c` sets it after `froth_ffi_register` + stdlib eval.
- Heap gains a `base_watermark` field (recorded after boot). `wipe` resets to it.
- Snapshot format is self-contained: no dependency on slot table indices or heap addresses.
- Redefinition of base words is persistable (per-slot flag handles it).
- Stage 1 (RAM round-trip) can be tested without any file I/O, CRC, or A/B logic.
- Slot binding records reserve `contract_obj_id`, `meta_flags`, and `meta_len` fields (written as `0xFFFFFFFF`, `0`, `0`). Future contract support won't require a format bump.
- The CALL tag deviation (0x07) means our snapshots are not interoperable with a strict spec-only reader. Acceptable since no other implementations exist.

## Implementation staging

**Stage 1 (today):** Overlay tracking, serializer, deserializer, RAM round-trip proof. No CRC, no header, no file I/O.

**Stage 2 (tomorrow):** Full header with CRC32, A/B file-backed persistence, `save`/`restore`/`wipe` primitives, boot-time restore, `autorun` under `catch`.

## References

- Spec: `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md`
- Concepts guide: `docs/archive/concepts/persistence.md`
- ADR-017: `def` accepts any value
- ADR-016: stable explicit error codes
