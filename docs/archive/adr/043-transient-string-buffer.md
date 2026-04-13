# ADR-043: Transient String Buffer and String Storage Abstraction

**Date**: 2026-03-19
**Status**: Accepted
**Spec sections**: FROTH-String-Lite (Section 8), FROTH-Snapshot (Section 5, non-persistable values), ADR-023 (String-Lite heap layout), ADR-019 (FFI public API), ADR-032 (mark/release)

## Context

Froth uses a linear bump-only heap for all allocations. There is no free, no compaction, no GC. String literals are heap-allocated at parse time. Once allocated, they persist until `reset` or `release`.

This is fine for quotation bodies and pattern definitions, which are created once and referenced forever. It is not fine for strings, which are frequently created at runtime and immediately discarded. Three sources of unbounded runtime string allocation exist or are imminent:

1. **String literals in evaluated source.** The host-centric workflow (ADR-037) sends source code to the device repeatedly via EVAL. Each string literal in EVAL source (`"up"`, `"down"`, `"left"`) allocates permanently on the heap. A device sitting in a command loop fills the heap with unreferenced dead strings.

2. **FFI-produced strings.** WiFi SSIDs, I2C data formatted as strings, UART input. Each call to the planned `froth_push_bstring` FFI function creates a string. Polling or event handling produces strings continuously.

3. **Future runtime string constructors.** Any word that produces a new string at runtime (formatting, assembly). FROTH-String-Lite (spec Section 8) intentionally excludes concat and slicing, but even narrow constructors like number-to-string formatting produce unbounded allocations in a loop.

The existing mitigation (`mark`/`release`, ADR-032) is inadequate for this problem. `mark`/`release` is all-or-nothing: everything allocated between mark and release is reclaimed, including slot assignments, quotation definitions, and any other heap work. You cannot selectively keep one value from a mark/release region. This makes it usable only for fully isolated side-effect-only work (emit a string and forget it). The moment you need to extract a value from a transient computation (assign a number to a slot, keep one string out of many), the mechanism fails.

### The core design tension

Froth's linear heap model means the default is permanent. Making something temporary requires ceremony (`mark`/`release`). For strings, this is backwards. The common case is: produce a string, use it briefly, discard it. The rare case is: produce a string and keep it forever. The default should match the common case.

## Options Considered

### Option A: Document the problem, rely on mark/release and heap sizing

Do nothing. Users manage heap pressure via `mark`/`release` for isolated work and `FROTH_HEAP_SIZE` tuning for overall capacity.

Pros: no implementation work. No new concepts.

Cons: `mark`/`release` cannot selectively retain values, making it impractical for real-world string processing. REPL and EVAL command loops leak strings indefinitely. WiFi, HTTP, and I2C string-producing words are unusable in any sustained program. This effectively makes Froth unable to work with strings in any non-trivial way.

### Option B: Transient-by-default strings with a scratch ring buffer

All runtime-produced strings are transient by default. They live in a fixed-size circular buffer separate from the heap. Making a string permanent requires an explicit action (naming it via `def`). The common path (produce string, use it, discard it) allocates nothing on the heap.

Pros: fixes the leak. The common case is zero-cost. The persistence boundary (`def`) is intuitive: named things live, unnamed things are temporary. Compatible with existing heap model.

Cons: transient strings can go stale if the buffer wraps. Requires stale-access detection. `def` becomes type-aware for one case. New module and new concept for users to learn.

### Option C: Full descriptor table for all strings

Every string (permanent and transient) is accessed through a fixed descriptor table. The BSTRING payload becomes a descriptor index. Each descriptor holds a pointer, length, storage kind, and generation. Storage kinds: `PERM_HEAP`, `SCRATCH_RING`, with future expansion to `BLOB_CHAIN`, `BORROWED_EXT`, etc.

Pros: uniform access model. Future storage backends are a descriptor change, not a consumer rewrite. Clean abstraction.

Cons: permanent strings that live forever on the heap and never move gain an indirection cost on every access. Descriptor table is fixed-size, which caps the total number of live strings (permanent + transient). A program with many string literals could exhaust the table. Significantly more implementation work. Touches every string consumer, the reader, the evaluator, the snapshot system.

### Option D: Hybrid (permanent = heap offset, transient = descriptor-backed)

Permanent strings (quotation-body literals) stay as direct heap offsets, exactly as ADR-023 specifies. Transient strings (top-level literals, FFI-produced, runtime-produced) use a small descriptor table backed by a scratch ring buffer.

A flag bit in the BSTRING payload distinguishes the two encodings. All string access goes through a single resolver function (`froth_bstring_resolve`) that dispatches based on the flag bit. Permanent strings resolve by direct heap offset (fast path, no change from today). Transient strings resolve through the descriptor table with generation checking.

Pros: minimal churn to existing permanent-string code paths. No indirection overhead on permanent strings. Descriptor table is small (only needs to hold concurrently-live transient strings, not all strings ever created). The resolver abstraction makes future migration to full-descriptor (Option C) a contained change. Ships fast.

Cons: two code paths behind the resolver. The flag bit costs one bit of payload range (irrelevant in practice). Not as architecturally pure as Option C. Future migration to full-descriptor requires touching the resolver and the evaluator, but not string consumers (they already go through the resolver).

## Decision

**Option D: Hybrid with resolver abstraction.** Permanent strings stay as heap offsets. Transient strings use a descriptor table + scratch ring. All consumers go through `froth_bstring_resolve`.

Deciding factors:

1. **Schedule.** The workshop is Mar 24. This unblocks I2C, WiFi, and every string-returning FFI binding. Options A and C don't ship in time. Option B is close to D but without the abstraction boundary that makes future expansion safe.

2. **The resolver is the non-negotiable piece.** Whether permanent strings use descriptors or heap offsets is an implementation detail hidden behind `froth_bstring_resolve`. If every consumer calls the resolver, migrating to full-descriptor later is a change to one function. The abstraction boundary matters more than the initial encoding choice.

3. **No overhead on permanent strings.** String literals in quotation bodies are the hot path in compiled Froth programs. Adding descriptor indirection to every access of `"Hello"` inside a `: greet "Hello" s.emit ;` that runs in a loop is pure cost for zero benefit. Those strings never move, never expire, never change storage kind.

4. **Descriptor table pressure is bounded.** Transient strings are short-lived by design. The table only needs to hold the strings that are concurrently alive on the data stack and in active computations. 32 entries is generous for the expected workload.

5. **Future path is clear.** The descriptor entry struct reserves a `kind` field. Adding `BLOB_POOL` or `BORROWED_EXT` later is a new case in the resolver and a new allocation backend, not a rewrite of string consumers. Option C is always available as a future migration if the hybrid proves limiting.

### Transient string buffer design

**Scratch ring buffer.** A fixed-size circular byte buffer on the VM, separate from the heap. Compile-time configurable: `FROTH_TBUF_SIZE` (default 1024 bytes). Strings are written sequentially with wrapping. Each string in the buffer is stored as: `[length (2 bytes)] [bytes...] [\0]`.

**Descriptor table.** Fixed array of 32 entries on the VM (`FROTH_TDESC_MAX`, configurable). Each entry:

```c
typedef struct {
    uint16_t ring_offset; /* offset into scratch ring (to the bytes, past length header) */
    uint16_t len;         /* byte count */
    uint32_t generation;  /* allocation generation, for stale detection */
    uint8_t kind;         /* storage kind: 0 = free, 1 = SCRATCH_RING */
} froth_tdesc_t;
```

The `kind` field reserves space for future storage backends (blob pool, borrowed external) but v1 only implements `SCRATCH_RING`. Permanent strings are not descriptor-backed in the hybrid design; they remain heap-direct. When `def` or `s.keep` promotes a transient string, the result is a heap-direct permanent cell. The transient descriptor is not freed (see aliasing rule under lifecycle rules); it stays live until ring reclamation.

**Allocation algorithm.** Transient string allocation proceeds in three steps, in this order:

1. **Determine write region.** Compute the byte span needed: `len + 2 (length header) + 1 (null terminator)`. Determine where in the ring this will be written starting from the current write cursor, including wrapping.

2. **Reclaim conflicting descriptors.** Scan all active descriptors. Any descriptor whose `[ring_offset, ring_offset + len)` range overlaps with the region about to be written is tombstoned: kind set to 0 (free). This is the reclamation invariant. The ring never writes without first freeing conflicting descriptors. A tombstoned descriptor's index may still appear in BSTRING cells on the data stack; those cells will fail the generation check on next access.

3. **Claim a free descriptor.** Scan the descriptor table for a free entry (kind == 0). If none is found after reclamation, return `FROTH_ERROR_TRANSIENT_FULL` (error code 23). Otherwise, write the bytes to the ring, populate the descriptor (ring offset, length, generation, kind = `SCRATCH_RING`), and return a BSTRING cell with the descriptor index encoded in the payload.

This ordering matters. Reclamation must happen before the free-descriptor scan, because wrapping into an old region frees descriptors that the scan can then find.

**Generation counter.** Global monotonic `uint32_t` on the VM (`tbuf_generation`), incremented on each transient allocation. Each descriptor records its full `uint32_t` generation at allocation time.

**Stale detection.** The resolver performs two checks:

1. `desc.kind != 0` — the descriptor is still live (not tombstoned by ring reclamation).
2. `desc.generation` (full 32-bit) matches the truncated generation stored in the BSTRING cell payload (see payload encoding below), after masking the full generation to the same bit width.

The second check guards against ABA: a descriptor index freed by ring reclamation and later reallocated to a different string. Without the generation check, a stale cell with the same descriptor index would silently resolve to the wrong string.

**ABA window.** The cell payload has limited bits for the truncated generation (see payload encoding). On 32-bit cells, this is 23 bits, which wraps after 8,388,608 allocations. At sustained maximum allocation rate (one per millisecond), the ABA window reopens after ~2.3 hours. At more realistic rates (one per 10ms for REPL/EVAL work, one per 100ms for sensor polling), the window is 23 hours to 9.7 days. This is a known limitation of the hybrid encoding. Mitigations:

- On 64-bit cells, the truncated generation is 56 bits. ABA is not a practical concern.
- The full-descriptor migration (Option C) eliminates the truncation by storing generation only in the descriptor, not the cell.
- For the thesis scope (workshop demos, sensor polling, interactive development), the 23-bit window is acceptable. Long-running production deployments on 32-bit cells should be aware of this limit.

### Payload encoding

The BSTRING tag (tag 4) stays. The payload uses its highest bit as a storage-class flag:

- Bit clear (0): permanent string. Payload is a heap offset. Identical to current ADR-023 encoding.
- Bit set (1): transient string. Remaining bits encode a descriptor table index (low bits, 5 bits for up to 32 entries) and a truncated generation (remaining bits, for ABA detection at the cell level).

"Highest payload bit" is relative to the cell width, which is compile-time configurable (ADR-001). On 32-bit cells with 3-bit tags, the payload is 29 bits. The highest payload bit is bit `FROTH_CELL_BITS - 4`. This leaves 28 bits for the permanent path (256 MB heap offset range, more than sufficient) or for the transient path (5-bit descriptor index + 23-bit truncated generation).

The truncated generation in the cell is compared against the descriptor's full `uint32_t` generation, masked to the same bit width. If they don't match, the descriptor has been reused and the cell is stale. This catches ABA in most cases, but the truncated generation wraps (see "ABA window" above). On 64-bit cells, the truncated generation is 56 bits and ABA is not a practical concern.

### String lifecycle rules

1. **String literal inside a quotation body** (`: greet "Hello" s.emit ;`): permanent. Heap-allocated by the evaluator during quotation building. The quotation body cell holds a BSTRING with flag bit clear and a heap offset. This is the current behavior, unchanged.

2. **String literal at top level** (`"Hello" s.emit` at REPL or in EVAL source): transient. Allocated in the scratch ring via a descriptor. Flag bit set.

3. **FFI-produced strings** (`froth_push_bstring`): transient. Allocated in the scratch ring via a descriptor.

4. **Future runtime string constructors**: transient. Same path as FFI.

5. **`def` with a transient string**: auto-promotes. `def` checks the flag bit. If transient, resolves the descriptor, copies the bytes to the heap, creates a permanent BSTRING cell, and stores that in the slot. The transient descriptor is **not** freed. It stays live until the ring wraps past it and reclaims it naturally. This is necessary because the transient cell may be aliased (e.g., `"hi" dup 'x def s.emit` — the `dup`'d copy on the stack must remain valid after `def` promotes one copy). Eager release would require reference counting or uniqueness proofs, both of which this ADR explicitly avoids. This is the ownership boundary. Any future word that writes byte data into persistable storage (e.g., a hypothetical `q.pack` that captures values into a quotation) must follow the same rule: reject transient inputs or promote them, but not free the source descriptor.

6. **`s.keep`**: escape hatch. Pops a transient string from the stack, copies to heap, pushes a permanent string. The transient descriptor is not freed (same aliasing rationale as `def`). Needed for cases where a permanent string must exist on the stack without being named. Discouraged for general use.

7. **Stale access**: semantic string operations (`s.emit`, `s.len`, `s@`, `s.=`, `def`, `s.keep`) on a stale transient string throw `FROTH_ERROR_TRANSIENT_EXPIRED` (catchable, error code 22). Diagnostic paths (REPL stack display via `.s`, link layer `format_stack`) must not throw on stale strings. They render `<str:stale>` instead. The distinction: operations that *read the bytes* throw; operations that *display a representation* degrade gracefully.

### Resolver function

All string access goes through one function:

```c
typedef struct {
    const uint8_t *data;
    froth_cell_t len;
} froth_bstring_view_t;

froth_error_t froth_bstring_resolve(froth_vm_t *vm, froth_cell_t cell,
                                    froth_bstring_view_t *view);
```

This replaces the current `pop_bstring` helper pattern. It checks the tag, checks the flag bit, dispatches to heap-direct or descriptor lookup, validates generation for transient strings, and returns a view (pointer + length). String consumers never touch heap offsets or descriptors directly.

### The evaluator's role

The reader tokenizes string literals into `TOKEN_BSTRING` tokens with raw bytes. It does not allocate. The evaluator decides where to store the bytes based on context:

- When building a quotation body (inside `[...]` or `: ;`): allocate on the heap as today. The string becomes part of the quotation body. Permanent BSTRING cell with heap offset.
- At top level (not inside a quotation): allocate in the scratch ring via a descriptor. Transient BSTRING cell with descriptor index + generation.

The evaluator already tracks quotation-building context. The branch point is a check on this context when handling a `TOKEN_BSTRING`. The reader stays dumb; the evaluator decides storage class.

### `def` type-awareness

Currently `def` is type-agnostic: `froth_slot_set_impl(slot_index, cell)` stores whatever cell it receives. For transient strings, `def` must:

1. Check if the cell is a BSTRING with the transient flag set.
2. If so, resolve the descriptor, allocate the bytes on the heap (same layout as ADR-023: length cell + bytes + null terminator), create a new permanent BSTRING cell, and store that.
3. If not (permanent string, number, quotation, slot, pattern), store as-is. Current behavior.

This is the one place where `def` becomes type-aware. The `set` word (defined as `swap def` in stdlib) inherits this behavior.

### Snapshot interaction

The snapshot serializer must handle both encodings:

- Permanent BSTRING (flag bit clear): serialize as today. Follow heap offset, emit bytes.
- Transient BSTRING (flag bit set): must never appear in serializable data. `def` auto-promotes, so slots should only hold permanent strings. The serializer must reject transient strings anywhere it encounters them in overlay-owned data (slot impls, quotation bodies, nested references), not just at the top-level slot check. If encountered, fail with a clear error, same principle as ADR-024's non-persistable NativeAddr values.

The snapshot deserializer creates permanent strings only (flag bit clear, heap offsets). No change needed.

### FFI API additions

Two new public functions in `froth_ffi.h`:

```c
/* Push a string from C into Froth. Allocates in transient scratch ring.
 * The caller's buffer is copied; the caller retains ownership of `data`. */
froth_error_t froth_push_bstring(froth_vm_t *vm, const uint8_t *data, froth_cell_t len);

/* Pop a string from Froth into C. Works on both permanent and transient strings.
 * Returns a view (pointer + length). The pointer is valid until the next
 * transient allocation (for transient strings) or forever (for permanent strings).
 * The caller must not modify the bytes. */
froth_error_t froth_pop_bstring(froth_vm_t *vm, const uint8_t **data, froth_cell_t *len);
```

`froth_pop_bstring` uses `froth_bstring_resolve` internally. The FFI author sees a pointer and a length and doesn't need to know whether the string is permanent or transient. The view is valid until the next transient allocation on the same VM. FFI words that need the bytes to survive across a call path that may allocate transients (including calling back into Froth) must copy the data into their own buffer first.

### Codebase changes

Files that currently assume "BSTRING payload = heap offset" and must be routed through the resolver:

| File | Lines | Current behavior | Change |
|------|-------|-----------------|--------|
| `froth_primitives.c` | 750-770 | REPL display: `heap->data + payload` | Call `froth_bstring_resolve`, use view |
| `froth_primitives.c` | 867-877 | `pop_bstring`: `heap->data + payload` | Replace with `froth_bstring_resolve` |
| `froth_evaluator.c` | 165 | Creates permanent BSTRING from heap offset | Branch on quotation-building context: heap when building a quotation body, transient at top level |
| `froth_snapshot_writer.c` | 138, 221, 391, 452, 496 | Follows heap offsets for serialization | Add transient check: reject if flag bit set |
| `froth_link.c` | 91 | Prints `<str>` (no dereference) | No change needed |
| `froth_executor.c` | 108 | Pushes cell to DS (no dereference) | No change needed |

New files:
- `froth_tbuf.h` / `froth_tbuf.c`: transient buffer management (ring buffer, descriptor table, allocate, resolve, generation checking)

Modified headers:
- `froth_vm.h`: transient buffer struct added to VM
- `froth_ffi.h`: `froth_push_bstring`, `froth_pop_bstring`
- `froth_types.h`: `FROTH_ERROR_TRANSIENT_EXPIRED` (error code 22), `FROTH_ERROR_TRANSIENT_FULL` (error code 23)

### Configurable parameters

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `FROTH_TBUF_SIZE` | 1024 | Scratch ring buffer size in bytes |
| `FROTH_TDESC_MAX` | 32 | Maximum concurrent transient string descriptors |

Both are compile-time CMake variables, same pattern as `FROTH_HEAP_SIZE` and `FROTH_DS_CAPACITY`.

### Memory cost

Descriptor entry: `uint16_t ring_offset` (2) + `uint16_t len` (2) + `uint32_t generation` (4) + `uint8_t kind` (1) = 9 bytes, padded to 12 with typical struct alignment.

On a 32-bit target with defaults: 1024 (ring) + 32 * 12 (descriptors) + 4 (generation counter) + 2 (write cursor) = 1414 bytes. Modest on ESP32 (which has 300KB+ free RAM). On ATtiny or constrained 16-bit targets, the feature can be compiled out or sized down (`FROTH_TBUF_SIZE=256`, `FROTH_TDESC_MAX=8`).

## Consequences

- Runtime-produced strings no longer leak heap. The most common string operations (produce, use, discard) are zero-cost on the heap.
- `def` is the ownership boundary. Named strings live. Unnamed strings are temporary. This is intuitive and easy to teach.
- FFI authors get a simple `froth_push_bstring` / `froth_pop_bstring` API that handles allocation automatically.
- The resolver abstraction (`froth_bstring_resolve`) means all future string storage backends are a change to one function, not a grep through the codebase.
- Stale-access detection (generation checking) turns a class of silent data corruption bugs into catchable errors.
- `mark`/`release` (ADR-032) remains useful for transient quotation and pattern construction but is no longer the primary string hygiene mechanism.
- The descriptor entry's `kind` field leaves a clean path to future storage backends (blob pool, borrowed external, chunked) without changing Froth semantics or string consumer code.
- A program cannot have more than `FROTH_TDESC_MAX` concurrently-live transient strings. This is a hard limit. If exceeded, `froth_push_bstring` returns an error (descriptor table full). In practice, 32 concurrent transient strings is generous. Descriptors are reclaimed only when the ring wraps past their byte span, not when strings are promoted via `def` or `s.keep`. Programs that accumulate many transient strings without consuming them will hit this limit. The mitigation is to use transient strings promptly and let the ring reclaim them naturally.
- The snapshot system gains a safety property: transient strings are non-persistable by construction, same principle as NativeAddr (ADR-024).

## Deferred

- **Full descriptor table for all strings (Option C).** If the hybrid proves limiting (e.g., the resolver branch becomes a measurable hot-path cost, or a uniform model is needed for tooling), all strings can be migrated to descriptors. The resolver abstraction makes this a contained change.
- **Blob pool / chunked storage.** For HTTP response bodies larger than the scratch ring. The descriptor entry format supports this via the `kind` field. Not needed until HTTP server/client work (Phase 3b).
- **Borrowed external buffers.** Zero-copy references to FFI-owned memory. Useful for DMA buffers or memory-mapped data. Risky (lifetime management at the C boundary). Deferred until a concrete use case with clear lifetime guarantees exists.
- **Rewrite-on-reassign optimization.** When `def` overwrites a slot that already holds a permanent string, rewrite in place if the new string fits. Avoids heap growth for repeatedly-updated string slots. Deferred because it adds variable behavior to `def`.
- **Stream-oriented words for large payloads.** Some HTTP responses should be processed as streams, not materialized as strings. This is a different abstraction (iterators/generators) and does not belong in the string subsystem.

## References

- ADR-023: String-Lite heap layout (permanent string encoding, preserved for heap-direct path)
- ADR-019: FFI public API (extended with `froth_push_bstring`, `froth_pop_bstring`)
- ADR-024: Native address profile (precedent for non-persistable values)
- ADR-026: Snapshot persistence (serializer must reject transient strings)
- ADR-032: mark/release (still useful for quotation/pattern construction, no longer primary string hygiene)
- ADR-037: Host-centric deployment (EVAL command loop is a primary source of string leaks)
- Zig allocator model: explicit lifetime strategy, no hidden allocation (design influence)
- Rust ownership / Cow: borrowed-vs-owned as the semantic seam (design influence)
- SQLite bind API: caller specifies static vs transient at the boundary (design influence)
- ANS Forth transient string semantics: `S"` strings valid until next string operation (prior art)
