# Froth Snapshot and Overlay Dictionary Specification

**Status:** Draft for review  
**Version:** 0.5 (Candidate)
**Date:** 2026-02-26  
**Profiles defined:** `FROTH-Snapshot` (required for persistence), `FROTH-Overlay` (persistence model)  
**Related specs:** `FROTH-Core`, `FROTH-Base`, `FROTH-Interactive` (Direct/Link Modes), `FROTH-FFI`

---

## Scope and intent

This document specifies a **deterministic, embedded-safe persistence model** for Froth based on an **overlay dictionary**. The overlay model exists to satisfy two requirements simultaneously:

1. **Durable persistence across firmware rebuilds/updates.** A snapshot must not embed raw pointers to primitives or native code that may move between builds.
2. **Simplicity and predictability.** No garbage collection, no filesystem requirement, and no background activity. Save/restore must be deterministic.

The overlay dictionary model is a **persistence strategy**, not a language change: Froth evaluation semantics are unchanged. A conforming implementation provides `save/restore/wipe` words and a snapshot image format, plus a defined boot-time reconstruction algorithm.

---

## Definitions

### Base layer
The **base layer** is the system state created at boot by firmware, consisting of:

- FROTH-Core + FROTH-Base words
- FFI-registered primitives and bindings
- Any other firmware-provided libraries installed at initialization

The base layer is **not persisted** in snapshots.

### Overlay layer
The **overlay layer** is the mutable layer created after boot by user interaction or host tools. It consists of:

- Slot bindings created/changed by `def` after base initialization
- Heap objects that those bindings depend on (quotes, patterns, contracts, etc.)

The overlay layer **is persisted** in snapshots.

### Persistable implementations and objects

This snapshot format is intentionally conservative.

**Persistable slot implementations:** the snapshot stores callable slot implementations only as:

- `QUOTE` (a quotation callable / QuoteRef)

In other words: **slot bindings are persisted as QUOTE**. If the runtime supports additional callable representations (threaded/native), those are treated as **performance caches** and are not directly serialized (see Canonical persistable form below).

**Persistable heap objects:** the payload may contain object records of the following kinds:

- `QUOTE` (QuoteRef)
- `PATTERN` (PatternRef)
- `CONTRACT` (ContractRef)
- `STRING` (StringRef; only if FROTH-String-Lite is enabled)

Snapshots remain pointer-free: all references are encoded by name IDs and object IDs.


### “Overlay ownership”
A slot binding is an **overlay binding** if it was created or modified after base initialization (or after the last restore) such that it differs from the base behavior.

Implementations MUST track overlay ownership by one of:

- a per-slot flag set by `def`, or
- a range check (“impl pointer points into overlay heap region”), or
- any equivalent mechanism that reliably distinguishes base vs overlay for `save`.

---

## High-level behavior

### Boot sequence (normative)
On boot, a conforming `FROTH-Snapshot` implementation MUST perform:

1. Initialize VM core state (stacks, slot table, heap allocator).
2. Register base words/primitives (Core+Base).
3. Register FFI bindings.
4. If a valid snapshot exists in the active snapshot slot:
   - Restore the overlay from that snapshot (Section 8).
5. If slot `autorun` is bound after restore:
   - Execute it under `catch`:
     - Evaluate: `[ 'autorun call ] catch`
   - Errors MUST be caught and reported; they MUST NOT prevent entry to the REPL.

Safe-boot bypass mechanisms are specified in `FROTH-Interactive`. This spec recommends safe-boot be implemented before enabling auto-restore by default.

### Required persistence words
A conforming implementation MUST provide:

| Word | Stack effect | Meaning |
|---|---:|---|
| `save` | `( -- )` | Persist the current overlay to the active snapshot slot |
| `restore` | `( -- )` | Replace current overlay by restoring from the active snapshot slot |
| `wipe` | `( -- )` | Erase snapshots and return system to base-only state |

**Notes**

- `restore` MUST leave the system in a usable REPL state even if restore fails (error reported, base state intact).
- `wipe` MUST be a “factory reset” to base-only state.

Optional multi-slot words may be provided (`save-to`, `restore-from`, `snapshots`) but are not required by v0.5.

---

## Key invariants and safety properties

### No raw pointers persisted (normative)
Snapshot payloads MUST NOT contain:

- raw machine addresses to code or data in RAM/flash,
- function pointers,
- pointers into the running VM heap.

All references in the snapshot MUST be encoded as:

- name references (string table IDs), or
- object IDs within the snapshot payload.

### Atomic save (normative)
`save` MUST be atomic with respect to power loss:

- A power failure during `save` MUST NOT corrupt the last valid snapshot.

Conforming implementations MUST use one of:

- double-buffered snapshots (A/B images with generation counters), or
- an append-only journal with commit marker and CRC.

This spec defines an A/B scheme in Section 7.

### Deterministic restore (normative)
`restore` MUST either:

- succeed completely, or
- fail without leaving partial overlay state installed.

If restore fails, the VM MUST end in a base-only consistent state.

---

## Memory model for overlay without GC

A conforming implementation MUST provide a linear heap allocator. No garbage collector is required or assumed.

### Recommended heap partitioning (informative)
To simplify overlay management, implementations SHOULD reserve a **base watermark** after initialization and allocate overlay objects above it.

- Base initialization allocates any required core objects.
- Record `heap_base_mark`.
- Overlay allocations occur after `heap_base_mark`.

On `wipe`, set heap pointer back to `heap_base_mark` and clear overlay slot bindings.

On `restore`, rebuild base (or reset to base watermark) and then load overlay objects and bindings.

---

## Snapshot storage layout (A/B atomic scheme)

### Snapshot slot
A **snapshot slot** is a reserved non-volatile region containing two images:

- Image A
- Image B

Each image has:

- a fixed-size header (Section 6.2)
- a variable-size payload

### Header format (normative)
All integers in the snapshot header are **little-endian**.

Header fields:

| Field | Size | Meaning |
|---|---:|---|
| `magic` | 8 bytes | ASCII `FROTHSNAP` |
| `fmt_version` | u16 | Snapshot format version (0x0004 for v0.4+) |
| `flags` | u16 | Flags; see below |
| `cell_bits` | u8 | 16, 32, or 64 |
| `endian` | u8 | 0 = little-endian, 1 = big-endian (writer endian; reader must accept both if it can) |
| `abi_hash` | u32 | Kernel ABI hash (Section 6.3) |
| `generation` | u32 | Monotonic generation counter |
| `payload_len` | u32 | Payload length in bytes |
| `payload_crc32` | u32 | CRC32 over payload bytes |
| `header_crc32` | u32 | CRC32 over header with this field zeroed |
| `reserved` | 16 bytes | Must be zero for format 0x0004 |

**Header validity:** An image is valid iff:

- `magic` matches,
- `fmt_version` matches,
- `payload_len` fits in slot bounds,
- `payload_crc32` matches computed CRC32,
- `header_crc32` matches computed CRC32,
- and `abi_hash` is acceptable (Section 6.3).

### ABI hash (normative)
`abi_hash` is a 32-bit hash used to detect incompatible restore attempts. It MUST incorporate at least:

- `cell_bits`
- endianness assumptions (if any)
- token encoding version
- object layout version for QuoteRef/PatternRef/ContractRef/StringRef as represented in the snapshot

**Policy:** By default, restore MUST refuse snapshots whose `abi_hash` differs from the running firmware’s `abi_hash` and return `ERR.SNAP.INCOMPAT`.

Implementations MAY provide an “attempt best-effort restore” mode for development, but MUST default to safe refusal.

---

## Selecting the active snapshot image

When restoring from a snapshot slot:

1. Read headers A and B.
2. Determine validity of each (Section 6.2).
3. If neither valid: no snapshot exists.
4. If only one valid: choose it.
5. If both valid: choose the one with the larger `generation` value.

When saving:

1. Read headers A and B, determine current winner.
2. Write the next generation into the *inactive* image region (payload first, then header).
3. After writing payload and computing CRCs, write header last as the commit step.

---

## Payload format (format 0x0004)

The payload encodes the overlay dictionary in a **pointer-free**, **dependency-ordered** binary form.

### Payload structure (normative)
All integers in payload are little-endian.

Payload begins with:

| Field | Size | Meaning |
|---|---:|---|
| `name_count` | u16 | number of interned names |
| names | variable | name table entries |
| `obj_count` | u32 | number of objects |
| objects | variable | object records |
| `slot_count` | u32 | number of overlay slot bindings |
| slots | variable | slot binding records |

### Name table
Each name table entry is:

| Field | Size | Meaning |
|---|---:|---|
| `name_len` | u16 | length in bytes |
| `name_bytes` | name_len | UTF-8 bytes |

**Constraints:**
- `name_len` MUST be > 0.
- Names SHOULD be <= 63 bytes for embedded friendliness; longer names MAY be rejected with `ERR.SNAP.BADNAME`.

**Name IDs:** names are indexed 0..name_count-1 in the order stored.

### Object records (dependency order)
Objects are indexed by **object ID** 0..obj_count-1.

**Dependency rule (normative):**
- Any object reference inside an object with ID `k` MUST refer only to IDs `< k`.

This allows single-pass restoration without two-phase patching.

Each object record begins with:

| Field | Size | Meaning |
|---|---:|---|
| `obj_kind` | u8 | uses `froth_tag_t` values: 1=QUOTE, 3=PATTERN, 5=CONTRACT, 4=BSTRING |
| `obj_id` | u32 | MUST equal the next sequential ID |
| `obj_len` | u32 | payload length of this object record (bytes) |
| `obj_payload` | obj_len | kind-specific payload |

#### QUOTE object payload
| Field | Size | Meaning |
|---|---:|---|
| `tok_count` | u16 | number of tokens |
| tokens | variable | tokens |

Each token is encoded as:

Token tags reuse `froth_tag_t` values directly:

| Token tag | Meaning | Encoding |
|---:|---|---|
| 0x00 | NUMBER literal | tag + `cell_bytes` signed integer |
| 0x01 | QUOTE reference | tag + u32 `obj_id` |
| 0x02 | SLOT reference (push) | tag + u16 `name_id` |
| 0x03 | PATTERN reference | tag + u32 `obj_id` |
| 0x04 | BSTRING reference | tag + u32 `obj_id` |
| 0x05 | CONTRACT reference | tag + u32 `obj_id` |
| 0x06 | CALL (invoke slot) | tag + u16 `name_id` |

`cell_bytes` = `cell_bits/8` from snapshot header.

#### PATTERN object payload
Patterns correspond to Froth `perm` patterns.

| Field | Size | Meaning |
|---|---:|---|
| `n_in` | u16 | input width `n` |
| `n_out` | u16 | output width |
| `indices` | u16[n_out] | output selectors in 0..n_in-1 |

#### CONTRACT object payload (optional use)
Contracts in this format primarily preserve arity.

| Field | Size | Meaning |
|---|---:|---|
| `n_in` | u8 | number of inputs |
| `n_out` | u8 | number of outputs |
| `kind_count` | u8 | = n_in + n_out |
| `kinds` | u8[kind_count] | kind codes |

Kind codes are built-in in this format:

- 0 = ANY
- 1 = CELL
- 2 = SLOT
- 3 = QUOTE
- 4 = PATTERN

If the restoring VM does not support a kind code, it MUST treat it as ANY.

#### STRING object payload

A STRING object encodes an immutable byte string (StringRef). The bytes are stored verbatim.

| Field | Size | Meaning |
|---|---:|---|
| `byte_len` | u32 | number of bytes |
| `bytes` | byte[byte_len] | raw bytes (recommended UTF-8) |

**Notes:**

- Froth string semantics are byte-based; UTF-8 is a convention for human text, not a requirement for protocol payloads.
- Writers SHOULD keep string literals modest in size on very small devices.


### Slot binding records
Slot records encode overlay bindings.

Each slot record:

| Field | Size | Meaning |
|---|---:|---|
| `slot_name_id` | u16 | name of slot |
| `impl_kind` | u8 | 1=QUOTE (this format only) |
| `impl_obj_id` | u32 | object ID of QUOTE callable |
| `contract_obj_id` | u32 | object ID of CONTRACT, or 0xFFFFFFFF for none |
| `meta_flags` | u16 | reserved (0) |
| `meta_len` | u16 | reserved (0) |

**Rule:** Slot bindings MUST be applied after all objects are loaded.

---

## Restore algorithm (normative)

Given a selected valid snapshot image:

1. Validate header and payload CRCs.
2. Validate `abi_hash` compatibility (Section 6.3). If incompatible, fail with `ERR.SNAP.INCOMPAT`.
3. Reset VM to base-only state:
   - clear overlay slot bindings
   - reset overlay heap region to base watermark (or re-init VM and re-register base)
4. Parse name table into an array of strings.
5. **Intern slots:**
   - For each slot record, intern `slot_name_id` into the slot table, creating the slot if needed.
6. **Load objects in order:**
   - For each object record ID 0..obj_count-1:
     - allocate the object in overlay heap (QuoteRef/PatternRef/ContractRef/StringRef)
     - decode payload
     - resolve references:
       - SLOT tokens: map `name_id` → slot (intern by name)
       - OBJ references: map `obj_id` to previously allocated object pointer (guaranteed by dependency rule)
7. **Apply slot bindings:**
   - For each slot record:
     - set slot.impl to the QUOTE object referenced by `impl_obj_id`
     - mark slot as overlay-owned
     - if `contract_obj_id != 0xFFFFFFFF`, attach contract metadata
8. Return to REPL prompt in a consistent state.

If any step fails (parse error, OOM, unresolved reference):

- The implementation MUST abort restore, reset to base-only state, report an error, and return to the REPL.

---

## Save algorithm (normative, high level)

On `save`, the VM MUST serialize the overlay in a manner that produces a payload matching Section 8.

### Selecting overlay slots

Implementations MUST consider all slots that are overlay-owned.

For each overlay-owned slot, the writer MUST determine a **persistable canonical implementation**:

- If the slot’s current implementation is a persistable `QUOTE`, use it.
- Otherwise, if the slot has a canonical persistable `QUOTE` (e.g., retained by FROTH-Perf promotion), use that.
- Otherwise, `save` MUST fail with `ERR.SNAP.NONPERSIST` and MUST NOT commit a partial snapshot.

This policy makes persistence robust: snapshots preserve **meaning**, and performance representations are reconstructed after restore.


### Reachability and object emission
The writer MUST traverse from selected overlay slot implementations and emit all reachable objects (quotes/patterns/contracts) that are required to interpret them.

The writer MUST emit objects in **dependency order** such that:

- nested quotes/patterns/contracts appear before objects that reference them.

A depth-first postorder traversal is sufficient.

### Name interning
All slot names and any token slot references MUST be interned into the name table once and referenced by `name_id`.

### Atomic commit
Save MUST use the A/B scheme (Section 7), writing payload first and header last.

---

## Error reporting

This spec defines the following recommended error codes:

- `ERR.SNAP.INCOMPAT` — ABI hash mismatch
- `ERR.SNAP.BADCRC` — header or payload CRC mismatch
- `ERR.SNAP.FORMAT` — parse failure
- `ERR.SNAP.OOM` — out of memory during restore
- `ERR.SNAP.BADNAME` — invalid name table entry
- `ERR.SNAP.UNRESOLVED` — unresolved object reference (violated dependency rule)

Exact numeric values are implementation-defined but SHOULD be stable within a platform ecosystem.

---

## Extensions and future stages (non-normative)

This 0x0004 format is intentionally conservative. Future versions may add:

- Persistable implementation kinds beyond `QUOTE` (e.g., threaded code, native stubs via stable IDs)
- User-defined kind persistence for FROTH-Checked
- Compression of payload sections
- Multiple snapshot slots and snapshot metadata listing
- Symbolic vs SlotID encoding modes (for size vs compatibility tradeoffs)

**Compatibility rule:** future format versions MUST change `fmt_version` and MUST NOT silently reinterpret 0x0004 payloads.

---

## Rationale (informative)

The overlay model prevents persisting primitive pointers and other unstable addresses, which is essential for snapshots to remain safe across firmware rebuilds/updates. It is also consistent with the “device is the computer” model: `wipe` becomes a true factory reset to “base image,” and `save/restore` are deterministic without requiring a filesystem.

This spec complements the interactive development proposal by giving `save/restore/wipe` a stable, pointer-free definition suitable for embedded targets without GC.