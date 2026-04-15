# ADR-038: Streaming Snapshot Serializer (Format v2)

**Date**: 2026-03-17
**Status**: Accepted
**Spec sections**: Froth_Snapshot_Overlay_Spec_v0_5, ADR-026 (snapshot persistence), ADR-027 (platform storage API)
**Depends on**: Current ESP32 NVS bug fix (in progress)

## Context

The snapshot serializer (ADR-026) uses ~3.5KB of working memory: 1KB name lookup table, 600B object lookup table, 1KB output buffer, plus walk stack and reader tables. This blew the ESP32 task stack (3.5KB default), causing a LoadProhibited crash on `save`.

A static BSS allocation was applied as a band-aid (Mar 17). This is acceptable for ESP32 (170KB+ DRAM) but unacceptable for tighter targets (RP2040 with 264KB, STM32 with 64-256KB, MSP430 with 2-16KB). Persistence backends are straightforward on these targets (~30 lines each), but the serializer's memory footprint blocks deployment.

The root cause: the format uses compact sequential IDs (name_id 0, 1, 2...; object_id 0, 1, 2...) that require building complete lookup tables before emission. The entire payload is buffered in RAM before writing to flash.

Other embedded Forths (Mecrisp, Zeptoforth, AmForth, FlashForth) avoid this problem entirely by compiling directly into flash. Froth's serialize-overlay-to-blob approach is more portable (position-independent, ABI-versioned, CRC-checked), but the implementation must respect embedded RAM constraints.

## Options Considered

### Option A: Keep format v1, static BSS workspace

Move the lookup tables to a static struct in BSS. ~2.8KB permanently allocated on snapshot-capable targets.

Trade-offs:
- Pro: no format change, no algorithm change. Minimal code churn.
- Con: 2.8KB of static RAM on every snapshot-capable target, even those with only 16KB total.
- Con: does not scale. Increasing FROTH_SLOT_TABLE_SIZE or FROTH_SNAPSHOT_MAX_OBJECTS increases the workspace proportionally.

### Option B: Streaming serializer with raw runtime identifiers (format v2)

Eliminate lookup tables by using raw slot indices and heap offsets as identifiers in the format. Stream output to flash through a small chunk buffer. Replace the object lookup table with a visited bitset.

Trade-offs:
- Pro: ~344 bytes writer, ~280 bytes reader. 10x reduction from 3.5KB.
- Pro: scales with heap size and object count, not with table capacity.
- Con: format change (v2). Not backward-compatible with v1 snapshots.
- Con: snapshot payload is slightly larger (raw offsets wider than compact IDs). Negligible at typical sizes (<20 slots, <10 objects).

### Option C: Streaming serializer with compact IDs, two-pass over flash

Keep compact IDs but stream pass 2 output directly to flash. Pass 1 still builds lookup tables in RAM, pass 2 reads them and streams.

Trade-offs:
- Pro: smaller snapshots (compact IDs).
- Con: still needs the lookup tables for pass 1. Only eliminates the 1KB output buffer.
- Con: complexity of reading back from flash for pass 2 if needed.

## Decision

**Option B.** Streaming serializer with raw runtime identifiers. Format v2.

Option A is the shipped band-aid. Option B is the real fix. The format is still private (no external consumers, no deployed snapshots that need migration). The v1 format version field in the header enables detection and rejection of old snapshots.

## Format v2 Layout

### Header (unchanged size, new fields)

Same 50-byte envelope. `format_version` bumped to 0x0005. New field: `core_abi_crc` covers cell_bits + endianness + format_version + slot layout fingerprint.

### Payload

Objects first (dependency postorder), then slot records.

**Quote record:**
```
kind:u8 = FROTH_QUOTE
old_offset:u16
body_cell_count:u16
body_cells: [u32 * body_cell_count]  (raw tagged cells, heap refs use old offsets)
```

**String record:**
```
kind:u8 = FROTH_BSTRING
old_offset:u16
byte_len:u16
bytes: [u8 * byte_len]
```

**Pattern record:**
```
kind:u8 = FROTH_PATTERN
old_offset:u16
byte_len:u8
bytes: [u8 * byte_len]
```

**Slot record (names inlined, not separate heap objects):**
```
slot_index:u8
name_len:u8
name_bytes: [u8 * name_len]
impl_cell:u32  (raw tagged cell)
```

**Footer:**
```
object_count:u16
slot_count:u16
payload_crc32:u32
```

Footer is written last. The header's payload_len and payload_crc32 fields are populated after the full payload is streamed. Header is written at offset 0 as the atomic commit point (same as v1).

### Key format differences from v1

- No separate name table. Slot names are inlined in slot records.
- No sequential name_id or object_id. Raw slot indices (u8) and heap offsets (u16) are the identifiers.
- Objects are emitted in dependency postorder. Quote body cells reference heap objects by their original (pre-save) heap offsets. The reader maps old offsets to new offsets during allocation.
- Slot records reference heap objects by old heap offsets (embedded in the impl_cell's tagged payload). The reader patches these using the relocation map.

## Serializer Algorithm

### Writer (~344 bytes working memory)

1. **Erase inactive A/B slot.**

2. **Emit objects in dependency postorder.** Walk each overlay slot's impl. For heap-referencing impls (quote, pattern, string), DFS into the object graph. Emit children before parents. Track emitted objects with a visited bitset (heap_bytes / 32 bytes, aligned to object start granularity).

3. **Emit slot records.** Walk overlay slots in index order. For each: emit slot_index, inline name, raw impl cell.

4. **Emit footer** (object count, slot count, CRC32).

5. **Write header** at offset 0 with payload length, CRC32, generation, ABI hash.

All output streams through a 64-byte chunk buffer. When the buffer fills, flush to the inactive flash slot at the current write offset. CRC32 computed incrementally as bytes are emitted.

**Working memory:**

| Component | Bytes |
|-----------|-------|
| Chunk buffer | 64 |
| Visited bitset (2KB heap) | 64 |
| DFS walk stack (50 frames * 4B) | 200 |
| CRC32 state | 4 |
| Write offset, counts, scratch | 12 |
| **Total** | **~344** |

### Reader (~280 bytes working memory)

1. **Read header.** Validate magic, version, ABI hash.

2. **Read objects.** For each object record: allocate on heap, record `(old_offset, new_offset)` in relocation map. Patch any heap-offset references in quote body cells using the relocation map (earlier objects are already mapped).

3. **Read slot records.** For each: look up slot by name (create if needed), set impl. Patch the impl cell's heap offset through the relocation map if it references a heap object.

4. **Validate footer CRC.**

**Working memory:**

| Component | Bytes |
|-----------|-------|
| Input chunk buffer | 64 |
| Relocation map (50 objects * 4B) | 200 |
| CRC32 state | 4 |
| Scratch | 12 |
| **Total** | **~280** |

Relocation map entries are `(u16 old_offset, u16 new_offset)` pairs, appended as objects are read and linear-scanned for lookups. At ~50 objects, linear scan is faster than any fancier structure.

## Constraints and Limits

- **Visited bitset:** sized as `FROTH_HEAP_SIZE / 32` bytes. For 4KB heap = 128 bytes. For 1KB heap = 32 bytes. CMake-configurable.
- **DFS walk stack depth:** capped at `FROTH_SNAPSHOT_MAX_OBJECTS` (default 50). If a graph exceeds this depth, `save` returns `FROTH_ERROR_SNAPSHOT_OVERFLOW`. In practice, overlay graphs are shallow (<5 levels).
- **Heap offsets as u16:** limits heap to 64KB. Current max is 4KB. No concern.
- **Slot indices as u8:** limits slot table to 256 entries. Current max is 128. No concern.
- **Slot index stability:** raw slot indices are only valid for the same base image (same primitives, same stdlib, same board FFI). The ABI hash in the header catches mismatches. A reflash that changes the base layer invalidates saved snapshots.
- **Names inlined:** slot names appear in slot records, not as separate heap objects. Name identity does not matter for the snapshot (names are looked up by string equality on restore, not by pointer).

## Consequences

- Format v1 snapshots are rejected on v2 firmware (version mismatch). This is a clean break. No migration needed because the format is private.
- The static BSS workspace (band-aid) can be removed once v2 is implemented. The workspace struct in `froth_snapshot.h` shrinks to ~344 bytes.
- Platform storage backends (ADR-027) work unchanged. The streaming writer still uses `platform_snapshot_write` with offset-based calls.
- Persistence becomes viable on RP2040, STM32, and other targets with limited RAM.
- The `platform_snapshot_read` API should support streaming reads (small chunks) for the reader. Current offset-based API already supports this.

## Implementation Plan

1. Fix current ESP32 NVS serialization bug (in progress).
2. Implement format v2 writer (streaming, bitset, DFS walk stack).
3. Implement format v2 reader (relocation map, inline names).
4. Update `froth_snapshot_prims.c` to use v2.
5. Bump `FROTH_SNAPSHOT_VERSION` to 0x0005.
6. Remove the static BSS workspace.
7. Verify with existing POSIX smoke tests + ESP32 hardware test.

## References

- ADR-026: Snapshot persistence implementation (format v1)
- ADR-027: Platform snapshot storage API
- Snapshot design review (Mar 17): confirmed approach, identified visited bitset sizing, DFS depth worst case, u16 relocation pairs
- Mecrisp-Stellaris, Zeptoforth, AmForth: compile-to-flash persistence models (prior art research, Mar 17)
- ESP32Forth (ueforth): raw heap dump persistence model (prior art research, Mar 17)
