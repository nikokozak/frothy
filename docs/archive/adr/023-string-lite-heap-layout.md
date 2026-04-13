# ADR-023: String-Lite Heap Layout

**Date**: 2026-03-07
**Status**: Accepted
**Spec sections**: FROTH-String-Lite (Section "FROTH-String-Lite"), Reader (Section 2.1 string literals)

## Context

FROTH-String-Lite adds immutable byte strings as first-class values. We need to decide how StringRef objects are stored on the linear heap, how the reader produces them, and what escape sequences are supported.

Key constraints:
- No GC. Strings are immutable and allocated once (at read/definition time).
- `s@` MUST throw `ERR.BOUNDS` on out-of-range index (spec requirement), so bounds checking must be efficient.
- `s.emit` should be cheap — ideally a pointer pass to existing `emit_string`.
- The heap already supports both cell-granular (`froth_heap_allocate_cells`) and byte-granular (`froth_heap_allocate_bytes`) allocation. Patterns use the byte-granular path.

## Options Considered

### Option A: Null-terminated only (no length prefix)

Store raw bytes + `\0` on the heap. Like C strings.

- Pro: minimal overhead, `s.emit` is trivial, `s.=` can use `strcmp`.
- Con: `s.len` is O(n) — must walk bytes every call. `s@` bounds checking requires O(n) length computation. Embedded `\0` impossible.

### Option B: Length-prefixed only (no null terminator)

One cell for byte count, then raw bytes.

- Pro: O(1) `s.len` and `s@` bounds checking. Embedded `\0` possible.
- Con: `s.emit` can't pass pointer directly to C string functions — needs length-aware output. More plumbing.

### Option C: Length-prefixed + null-terminated

One cell for byte count, then raw bytes, then a trailing `\0`.

- Pro: O(1) `s.len` and `s@` bounds checking. `s.emit` can pass pointer to `emit_string` (null-terminated). `s.=` can use `memcmp` with known lengths (fast reject on length mismatch). Embedded `\0` possible in the future since correctness uses the length, not the terminator. Matches quotation layout pattern (cell-sized prefix + body).
- Con: one extra byte per string (the `\0`). Marginal.

## Decision

Option C: length-prefixed + null-terminated. The length cell gives O(1) `s.len` and `s@` bounds checking. The null terminator gives free C interop for `s.emit`. Cost is one cell + one byte overhead per string — negligible.

### Heap layout

```
[byte_count (1 cell)] [byte0] [byte1] ... [byteN-1] [\0]
```

- `byte_count` is the number of content bytes (excludes the null terminator).
- Allocated via `froth_heap_allocate_bytes(sizeof(froth_cell_t) + byte_count + 1)`.
- The StringRef value on the data stack is a tagged cell: `(heap_offset << 3) | FROTH_STRING`.

### Tag

`FROTH_STRING = 4` — already allocated in `froth_types.h`.

### Reader integration

- New token type: `FROTH_TOKEN_STRING`.
- Reader scans from opening `"` to closing `"`, processing escape sequences.
- At the top level, the evaluator allocates the string on the heap and pushes a StringRef to DS.
- Inside quotations, the string literal is allocated at build time and stored as a literal cell in the quotation body. Executing the quotation pushes the same StringRef (no re-allocation).

### Escape sequences

| Escape | Byte  | Description |
|--------|-------|-------------|
| `\"`   | 0x22  | Literal quote |
| `\\`   | 0x5C  | Literal backslash |
| `\n`   | 0x0A  | Newline (LF) |
| `\t`   | 0x09  | Tab |
| `\r`   | 0x0D  | Carriage return |

`\0` is deferred. The length-prefix design supports it, but adding it now would require care around C interop paths that assume null-termination. Can be added later without layout changes.

Unknown escape sequences (e.g., `\q`) MUST raise a reader error.

### Primitives

| Word | Stack effect | Implementation |
|------|-------------|----------------|
| `s.emit` | `( s -- )` | Type-check STRING tag, get heap pointer, pass `&bytes[0]` to `emit_string`. |
| `s.len` | `( s -- n )` | Type-check, read length cell from heap, push as number. |
| `s@` | `( s i -- byte )` | Type-check both, bounds-check `i` against length, read byte, push as number. |
| `s.=` | `( s1 s2 -- flag )` | Type-check both, compare lengths (fast reject), then `memcmp`. Push `FROTH_TRUE` or `FROTH_FALSE`. |

### Stack display

Strings display as `"contents"` in the REPL stack output. Non-printable bytes display as `\n`, `\t`, `\r`, or `\xHH` for other non-printables.

## Consequences

- StringRef is a first-class tagged value. The executor's quotation walker must handle `FROTH_STRING` in the switch (push to DS, same as numbers and quotations).
- No new heap allocation strategy needed — uses existing `froth_heap_allocate_bytes`.
- The reader gains string state (inside-string flag, escape processing). This is new complexity but contained.
- Future `\0` support requires no layout changes.
- Future `s.pack` (FROTH-String) can use the same layout — allocate length cell + copied bytes + null terminator.

## References

- Spec v1.1: FROTH-String-Lite section
- Spec v1.1: Reader section 2.1 (string literals)
- ADR-013: PatternRef byte encoding (precedent for byte-granular heap allocation)
- ADR-010: Contiguous quotation layout (precedent for length-prefixed heap objects)
