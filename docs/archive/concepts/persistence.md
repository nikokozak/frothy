# Persistence in Froth: Concepts and Implementation Guide

This document covers the underlying concepts you need to understand before implementing Froth's snapshot persistence system. Read it end-to-end before opening the spec. It's structured bottom-up: low-level ideas first, then how they compose into the full system.

---

## 1. What "persistence" means here

Persistence = surviving a power cycle. When you type `: double dup + ;` in the REPL and then unplug the device, that definition is gone. Persistence makes it stick.

The naive approach would be "dump all of RAM to flash." This doesn't work because:

- **Pointers are meaningless across boots.** If a slot's `prim` field points to `0x20004A10` (the C function for `+`), that address may be different after a firmware update. Persisting it would crash.
- **The base layer is redundant.** Core words like `+`, `emit`, `def` are rebuilt from C every boot. Saving them wastes space and creates version-mismatch risk.
- **Flash is small.** On an ESP32, you might have 4-64 KB for snapshots. You can't afford waste.

So Froth uses an **overlay model**: only persist what the user created, and encode it in a pointer-free format that can be reconstructed on any compatible firmware.

---

## 2. The overlay model

Think of two layers stacked:

```
┌─────────────────────────────────┐
│  Overlay (user definitions)     │  ← persisted
├─────────────────────────────────┤
│  Base (core + FFI primitives)   │  ← rebuilt from C every boot
└─────────────────────────────────┘
```

**Base layer** — created at boot by `froth_ffi_register()` calls in `main.c`. This includes all 34 kernel primitives, the stdlib (`core.froth`), and board FFI bindings. After boot completes, the base is frozen.

**Overlay layer** — everything the user creates after boot via `def` (directly or through `: ;` sugar). This is what `save` serializes and `restore` reconstructs.

### Base watermark

The heap is a linear bump allocator. Base objects (stdlib quotations, primitive names) are allocated during boot. After boot, you record the heap pointer — that's the **base watermark**:

```
heap: [  base objects  |  overlay objects  ...  free space  ]
                       ^
                  base_watermark
```

- `wipe` = reset `heap.pointer` to `base_watermark`, clear overlay slot bindings. Instant factory reset.
- `restore` = wipe first, then rebuild overlay from snapshot.

You also need to know which **slots** are overlay-owned. Two practical approaches:

1. **Base watermark on the slot table.** Record `slot_pointer` after boot. Slots with index >= that value are overlay slots.
2. **Per-slot flag.** Add an `overlay` bit to `froth_slot_t`. Set it whenever `def` modifies a slot after boot.

Option 1 is simpler but doesn't handle redefining base words (e.g., the user redefines `+`). Option 2 handles that case. The spec allows either.

---

## 3. Serialization fundamentals

Serialization = converting in-memory data structures into a flat byte sequence that can be stored, transmitted, and later reconstructed. The key challenges:

### 3a. No raw pointers

In memory, a quotation like `[ 1 double ]` is stored as:

```
heap offset 200: [2]              ← length (2 tokens)
heap offset 204: [0x00000008]     ← tagged NUMBER(1)
heap offset 208: [0x0000001E]     ← tagged CALL(slot_index=3, "double")
```

That `CALL(3)` means "invoke slot index 3." But slot indices are assigned at boot time in whatever order `froth_ffi_register` and the stdlib happen to run. After a firmware update, `double` might be slot 5, not slot 3. So you **cannot persist slot indices**.

Instead, you persist slot **names**. The snapshot says:

```
token: SLOT_REF, name = "double"
```

At restore time, you look up "double" by name (or create it), get its current slot index, and write that index into the reconstructed quotation.

### 3b. Name table (string interning)

If 10 quotations all reference `double`, you don't want to store the string "double" 10 times. So the snapshot uses a **name table**: a list of unique strings, each assigned a numeric ID:

```
Name table:
  ID 0: "double"
  ID 1: "square"
  ID 2: "greet"
```

Then tokens reference names by ID: `SLOT_REF(name_id=0)` means "double". This is called **string interning** — you intern a string once, then refer to it cheaply everywhere.

### 3c. Object IDs and dependency order

Quotations can contain other quotations:

```
: foo [ 1 2 + ] call ;
```

The outer quotation's body contains a reference to the inner `[ 1 2 + ]` quotation. Both need to be serialized. But when restoring, you need to allocate the inner quotation first, so that when you build the outer quotation, you can embed a reference to it.

The snapshot solves this with **object IDs** and a **dependency ordering rule**: objects are numbered 0, 1, 2, ... and any reference inside object K must point to an object with ID < K. This means a single forward pass through the object list can reconstruct everything — you never encounter a reference to something you haven't built yet.

In practice, this means you serialize in **depth-first postorder**: visit children before parents. If `foo` contains `[ 1 2 + ]`, serialize `[ 1 2 + ]` as object 0, then `foo`'s body as object 1 (which references object 0).

### 3d. Tagged tokens in the snapshot

Snapshot token tags reuse `froth_tag_t` values directly — no separate encoding layer:

| Tag byte | Meaning | Followed by |
|---|---|---|
| 0x00 (NUMBER) | Number literal | `cell_bits/8` bytes (signed integer) |
| 0x01 (QUOTE) | Quote reference | u32 object_id |
| 0x02 (SLOT) | Slot reference (push) | u16 name_id |
| 0x03 (PATTERN) | Pattern reference | u32 object_id |
| 0x04 (BSTRING) | String reference | u32 object_id |
| 0x05 (CONTRACT) | Contract reference | u32 object_id |
| 0x06 (CALL) | Call (invoke slot) | u16 name_id |

SLOT (tag 2) pushes a slot reference onto the stack (`'foo`). CALL (tag 6) invokes the slot (`foo` inside a quotation body).

---

## 4. The snapshot binary format

The snapshot is a self-contained binary blob. It has two parts:

### 4a. Header (fixed size)

```
Offset  Size  Field
0       8     Magic: "FROTHSNAP" (ASCII, no null terminator... actually 9 bytes? Check spec.)
```

Wait — `FROTHSNAP` is 9 characters but the spec says 8 bytes. Let me re-read... The spec says `magic` is 8 bytes, ASCII `FROTHSNAP`. That's 9 characters. This is likely a spec inconsistency — `FROTHSNP` (8) or the field is 9 bytes. You'll want to pin this down in your ADR. A practical choice: use 8 bytes `FRTHSNAP\0` or the first 8 bytes of `FROTHSNA`. Or just make magic 9 bytes. It's your call — the spec is draft v0.5.

The rest of the header:

```
8       2     fmt_version (u16 LE, 0x0004)
10      2     flags (u16, reserved = 0)
12      1     cell_bits (8/16/32/64)
13      1     endian (0=little, 1=big)
14      4     abi_hash (u32)
18      4     generation (u32, monotonic counter)
22      4     payload_len (u32)
26      4     payload_crc32 (u32)
30      4     header_crc32 (u32, computed with this field zeroed)
34      16    reserved (all zeros)
─────────────
Total: 50 bytes
```

### 4b. Payload (variable size)

```
┌──────────────────────┐
│ name_count (u16)     │
│ name entries...      │  ← the string intern table
├──────────────────────┤
│ obj_count (u32)      │
│ object records...    │  ← quotations, patterns, strings
├──────────────────────┤
│ slot_count (u32)     │
│ slot bindings...     │  ← which slot gets which object
└──────────────────────┘
```

Each section is self-describing (length-prefixed), so you can parse them sequentially.

---

## 5. CRC32 — what it is and how it works

CRC32 (Cyclic Redundancy Check, 32-bit) is an error-detection code. It answers one question: "did the data change since I computed this checksum?"

### The concept

Think of it as a fingerprint for a byte sequence. You feed in N bytes, you get out a 4-byte number. If any byte changes (bit flip, truncation, corruption), the CRC will almost certainly be different.

It's **not** cryptographic — it doesn't protect against intentional tampering. But for detecting accidental corruption (power loss during flash write, bad sectors, bit rot), it's excellent and fast.

### How it works (simplified)

CRC treats the input as a giant polynomial over GF(2) (binary field where addition is XOR). It divides this polynomial by a fixed "generator polynomial" and takes the remainder. The standard CRC32 generator is `0xEDB88320` (reflected form).

You don't need to understand the math. In practice:

```c
uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}
```

That's the entire algorithm — no libraries needed. For better performance, you can use a 256-entry lookup table (trades 1 KB RAM for ~8x speed), but the bitwise version above is fine for small payloads.

### How Froth uses CRC32

Two CRCs protect a snapshot:

1. **payload_crc32**: computed over the raw payload bytes. Detects corruption in the variable-size data.
2. **header_crc32**: computed over the header bytes **with the header_crc32 field itself set to zero**. Detects corruption in the header.

On restore, you recompute both CRCs and compare. If either mismatches, the snapshot is corrupt — refuse to load it.

### The "zero the field" trick

To compute header_crc32:

1. Fill in all header fields normally.
2. Set the `header_crc32` field to 0x00000000.
3. Compute CRC32 over the entire header (including the zeroed field).
4. Write the result back into `header_crc32`.

To verify:

1. Read the stored `header_crc32` value.
2. Zero that field in a copy (or in place temporarily).
3. Recompute CRC32 over the header.
4. Compare.

This is a standard pattern for self-checksumming headers.

---

## 6. A/B double buffering — atomic writes without a filesystem

### The problem

Flash memory writes are not atomic. If power dies mid-write, you get a half-written blob — the old data is gone and the new data is incomplete. Your snapshot is toast.

### The solution: two images

Keep two snapshot images in flash (or two files, on POSIX):

```
┌─────────────┐  ┌─────────────┐
│  Image A    │  │  Image B    │
│  gen: 3     │  │  gen: 2     │
│  (valid)    │  │  (valid)    │
└─────────────┘  └─────────────┘
```

Each image has a **generation counter** — a monotonically increasing number. The image with the higher generation is the "current" one.

**To save:**

1. Determine which image is current (higher generation, or the only valid one).
2. Write to the **other** image (the stale/inactive one):
   - Write payload bytes first.
   - Compute CRCs.
   - Write header last — this is the **commit point**.
3. The new image now has generation = old_max + 1.

**Why this is safe:**

- If power dies while writing the payload: the header hasn't been written yet, so this image is invalid (no magic bytes). The other image is still valid.
- If power dies while writing the header: the header is partially written, so magic/CRC won't match. Still invalid. The other image is still valid.
- Only after the header is fully written does the new image become valid. And the old image is still valid too (we never touched it). So we always have at least one good image.

**To restore:**

1. Read both headers.
2. Check validity of each (magic, CRCs).
3. If both valid, pick the one with the higher generation.
4. If only one valid, use it.
5. If neither valid, no snapshot exists.

### Generation counter

The generation counter must be monotonically increasing. On save, you set it to `max(gen_a, gen_b) + 1`. This ensures tie-breaking is deterministic and a new save always wins.

### POSIX implementation

On POSIX (your development platform), "flash" is just files. You can use two files (`froth_a.snap`, `froth_b.snap`) or a single file with two fixed-size regions. Two files is simpler.

---

## 7. ABI hash — compatibility guard

The **ABI hash** is a single u32 that fingerprints the binary format assumptions. Its purpose: prevent loading a snapshot saved by firmware with a different cell size, endianness, or object layout.

It should incorporate at least:

- `cell_bits` (32-bit Froth can't load a 16-bit snapshot)
- Endianness
- Snapshot format version
- Object layout version (if QuoteRef encoding changes, old snapshots are invalid)

A simple approach: hash the concatenation of these values. Even simpler: pack them into a u32 directly:

```c
#define FROTH_ABI_HASH ((uint32_t)(               \
    ((uint32_t)FROTH_CELL_SIZE_BITS)         |     \
    ((uint32_t)FROTH_ENDIAN        << 8)     |     \
    ((uint32_t)FROTH_SNAP_FMT_VER << 16)           \
))
```

If any of these values differ between the saving and restoring firmware, the hashes won't match, and restore refuses with `ERR.SNAP.INCOMPAT`.

---

## 8. Endianness — byte order in multi-byte values

A 32-bit integer like `0x12345678` is stored as multiple bytes. The question is: which byte comes first?

- **Little-endian** (LE): least-significant byte first: `78 56 34 12`. x86, ARM (usually), ESP32, RP2040 all use LE.
- **Big-endian** (BE): most-significant byte first: `12 34 56 78`. Network protocols traditionally use BE.

The Froth snapshot spec mandates **little-endian** for all integers in the header and payload. Since your targets (ESP32, RP2040, x86) are all LE, you can write values directly without byte-swapping:

```c
// Writing a u32 in LE on a LE machine — just memcpy
uint32_t val = 42;
memcpy(dest, &val, sizeof(val));
```

If you ever port to a BE machine, you'd need byte-swap helpers (`htole32`, `le32toh`), but for now, direct writes are fine.

---

## 9. Walking the heap: how to find what to serialize

The save algorithm needs to find all overlay objects and serialize them. But the heap is a flat byte array — there's no linked list of objects. How do you find them?

**You don't walk the heap.** You walk the **slot table**.

The algorithm is:

1. For each overlay-owned slot, look at its `impl` value.
2. If it's a QUOTE cell (tag = 1), the payload is a heap offset pointing to a quotation.
3. Follow that offset to the quotation body on the heap. Read the length cell, then each body cell.
4. For each body cell, check its tag:
   - NUMBER: serialize directly as a literal.
   - CALL/SLOT: the payload is a slot index — look up the slot's name, intern it in the name table, serialize as a name reference.
   - QUOTE: the payload is a heap offset to a nested quotation — recursively process it, assign an object ID, serialize as an object reference.
   - PATTERN: the payload is a heap offset to a pattern — read the pattern bytes, serialize as a pattern object.
   - BSTRING: the payload is a heap offset to a string — read length + bytes, serialize as a string object.

This is a tree walk rooted at each overlay slot. Depth-first postorder gives you the dependency ordering for free.

### Deduplication

Two slots might reference the same quotation (e.g., `'foo get` and `'bar get` both return the same QuoteRef). You need to track which heap offsets you've already serialized to avoid duplicating objects. A simple approach: maintain a mapping from heap offset → object ID. Before serializing an object, check if you've already seen that offset.

---

## 10. Reconstructing objects at restore time

The restore algorithm is essentially serialization in reverse:

1. Parse the name table — build an array of strings.
2. For each object record (in order):
   - Allocate space on the heap.
   - Decode the payload.
   - For SLOT references: look up the name (by name_id), find or create the slot, write the slot index into the reconstructed cell.
   - For object references: look up the object ID in your mapping (heap offset of that already-reconstructed object), write the heap offset into the cell.
3. For each slot binding: look up the slot by name, set its `impl` to the reconstructed QUOTE's tagged cell.

Because objects are in dependency order, step 2 always has all referenced objects available before they're needed.

### The object ID → heap offset mapping

During restore, you build an array: `heap_offsets[obj_id] = byte_offset_in_heap`. As you allocate each object, record where it landed. When a later object references `obj_id = 3`, you look up `heap_offsets[3]` to get the heap offset, tag it appropriately, and embed it.

---

## 11. The CALL vs SLOT distinction

Inside a quotation body, there are two kinds of slot references:

- **CALL** (tag 6): "invoke this slot when the quotation executes." E.g., in `[ 1 double ]`, `double` is a CALL — it gets executed.
- **SLOT** (tag 2): "push this slot's value onto the stack." E.g., in `[ 'double ]`, `'double` is a SLOT — it's a reference, not an invocation.

Both reference the same slot table entry. The difference is just the tag. The snapshot uses two distinct token tags to preserve this: **0x02 for SLOT** (push), **0x07 for CALL** (invoke). Both are followed by a u16 `name_id`. At restore time, the tag directly tells you which in-memory tag to use — no ambiguity, no context-dependent reconstruction.

---

## 12. Persisting all value types

Since `def` accepts any value (ADR-017), a slot could hold a plain number, a string, a pattern, or another slot reference — not just quotations. The spec says only QUOTE implementations are persistable, but we extend this: **all value types are persistable.**

The rationale: if users can't persist numbers and strings as slot values, they'll have to wrap everything in quotations (`: answer 42 ;` instead of `42 'answer def`). This is a tax on usability with no real benefit — the serialization cost per type is ~10 lines of code.

**Persistable impl_kinds in slot binding records:**

| impl_kind | Value type | Encoding after impl_kind byte |
|---|---|---|
| 0x00 | NUMBER | `cell_bytes` signed integer (the raw number) |
| 0x01 | QUOTE | u32 `obj_id` |
| 0x02 | SLOT | u16 `name_id` |
| 0x03 | PATTERN | u32 `obj_id` |
| 0x04 | BSTRING | u32 `obj_id` |
| 0x05 | CONTRACT | u32 `obj_id` |

At restore time, each impl_kind reconstructs the appropriate tagged cell: NUMBER values are tagged with `froth_make_cell(..., FROTH_NUMBER)`, QUOTE/PATTERN/BSTRING/CONTRACT values look up the object by ID and tag with the corresponding tag, SLOT values look up the name and tag with `FROTH_SLOT`.

**The only truly non-persistable things are:**
- Function pointers (the `prim` field) — always base-layer, never overlay-owned.
- NativeAddr values (ADR-024, FROTH-Addr) — not yet implemented; the spec explicitly flags them as non-persistable.

---

## 13. Implementation staging

The timeline has this prioritized:

### Stage 1 (Mar 9): RAM round-trip — the minimum viable persistence

- Add base watermark tracking (record `heap.pointer` and `slot_pointer` after boot).
- Write a serializer that walks overlay slots and emits a payload to a `uint8_t` buffer.
- Write a deserializer that reads the buffer, rebuilds objects on the heap, and applies slot bindings.
- **Skip** CRC, header, A/B, and file I/O. Just prove the round-trip works in memory.
- **Proof**: `': double [ dup + ] def`, serialize to buffer, wipe (reset to watermark), deserialize from buffer, `5 double` → `10`.

### Stage 2 (Mar 10): File-backed + full format

- Add the header (magic, fmt_version, cell_bits, abi_hash, generation, CRCs).
- Implement CRC32.
- Add file I/O on POSIX (`froth_a.snap`, `froth_b.snap`).
- Implement A/B image selection.
- Wire up `save`, `restore`, `wipe` as Froth primitives.
- Add to boot sequence: restore on startup, `autorun` under catch.

### What to defer

- ESP32 NVS/flash backend — same payload format, different I/O layer.
- CONTRACT serialization — no contracts implemented yet.
- Compression — payloads are small.

---

## 14. Connecting to what you already know

| Persistence concept | Froth analog you've already built |
|---|---|
| String interning (name table) | Slot table's name storage in the heap |
| Tree walking (reachability) | `emit_cell` in REPL display / `see` primitive |
| Length-prefixed records | Quotation layout: `[length][tok0][tok1]...` |
| Byte-level encoding | String-Lite heap layout: `[len_cell][bytes][\0]` |
| Two-pass processing | Evaluator's count pass + build pass for quotations |
| Bump allocator reset | What `wipe` does — just move the pointer back |

You're not starting from scratch. The serializer is structurally similar to `see` (walk a quotation, emit its contents) — except instead of printing text, you're emitting binary records. The deserializer is structurally similar to the evaluator's quotation builder — except instead of reading tokens from the reader, you're reading them from a binary buffer.

---

## 15. Resolved decisions (ADR fodder)

These decisions are resolved and should go into the persistence ADR:

1. **Overlay tracking**: per-slot flag (`uint8_t overlay` on `froth_slot_t`). Supports redefining base words. Cost: 1 byte per slot (128 bytes at current config, likely free due to struct padding). Requires a `boot_complete` flag on the VM so that `def` during stdlib loading doesn't set the overlay bit. After `main.c` finishes `froth_ffi_register` + stdlib eval, set `vm->boot_complete = 1`.
2. **CALL vs SLOT encoding**: two distinct snapshot token tags — 0x02 for SLOT (push reference), 0x07 for CALL (invoke).
3. **Magic bytes**: 9 bytes, literal ASCII `FROTHSNAP`. Alignment is irrelevant because the snapshot is parsed via `memcpy`/byte reads, never cast over a struct.
4. **All value types persistable**: NUMBER, QUOTE, SLOT, PATTERN, BSTRING, CONTRACT. See section 12 for impl_kind table. Spec deviation from QUOTE-only, justified by usability.
5. **Buffer allocation**: static buffer sized by CMake variable `FROTH_SNAPSHOT_SIZE` (default 2048). Used for RAM round-trip proof. Stage 2 writes directly to file on POSIX.
6. **Deduplication**: tracked by heap offset during serialization. Mapping from heap offset → object ID prevents duplicate object emission.
7. **POSIX file paths**: CMake variables `FROTH_SNAPSHOT_PATH_A` and `FROTH_SNAPSHOT_PATH_B`, defaulting to `froth_a.snap` and `froth_b.snap`.
8. **Traversal**: explicit stack (not recursion) for safety on embedded targets. Fixed-size stack (16-32 entries) bounded by maximum quotation nesting depth.
9. **Snapshot error codes**: new range 200–299 in the error enum. Codes: INCOMPAT, BADCRC, FORMAT, SNAP_OOM, BADNAME, UNRESOLVED, NONPERSIST.
