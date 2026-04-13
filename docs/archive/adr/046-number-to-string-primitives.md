# ADR-046: Number-to-String Primitives

**Date**: 2026-03-21
**Status**: Accepted
**Spec sections**: FROTH-String-Lite (Section 8), ADR-043 (transient string buffer), ADR-023 (String-Lite heap layout), ADR-019 (FFI public API)

## Context

Froth has no way to produce a string at runtime. String literals exist (`"hello"`), and the transient string buffer (ADR-043) provides infrastructure for runtime-produced strings, but no kernel word exercises that path yet.

The immediate need: `s.concat` (next on the roadmap) and downstream WiFi/HTTP demos require number-to-string conversion for building dynamic output. Sensor readings, register dumps, and HTTP response bodies all need to format numbers as text.

Three numeric literal formats exist in the reader: decimal, hex (`0x`), binary (`0b`). The corresponding string constructors should cover all three.

### Design constraints

- Output goes into the transient ring buffer via `froth_push_bstring`. No heap allocation.
- Maximum output length is bounded by cell width. For 32-bit cells (29 payload bits): decimal needs at most 10 characters (`-536870912`), hex needs at most 10 (`0x` + 8 nibbles), binary needs at most 31 (`0b` + 29 digits). For 64-bit cells (61 payload bits): binary worst case is 63 characters (`0b` + 61 digits).
- The conversion loop produces digits least-significant-first, but the string must be stored most-significant-first. A small fixed scratch buffer on the C stack bridges the two.

## Options Considered

### Option A: Single word with format argument

`10 n>s` for decimal, `16 n>s` for hex, etc.

Pros: one word to learn. Cons: the format argument is a bare number with no mnemonic value, hex/binary prefix behavior is ambiguous, and sign handling differs between radixes (signed decimal vs unsigned hex/binary), making a single word do too many things.

### Option B: Three dedicated words

`n>s` (decimal), `n>hexs` (hex string), `n>bins` (binary string).

Pros: each word has exactly one behavior. The `n>` prefix groups them as a family. The trailing `s` in `n>hexs` and `n>bins` makes clear the output is a string, not a numeric conversion. No ambiguity about sign handling or prefix inclusion.

Cons: three primitives instead of one. Marginal cost given the implementation is a shared conversion routine with a radix/prefix parameter.

### Option C: `s.` prefix family

`s.n>s`, `s.n>hex`, `s.n>bin`. Groups them with existing string words for discovery and compile-time gating.

Pros: consistent prefix. Cons: the `s.` prefix on existing words (`s.emit`, `s.len`, `s@`) means "operate on a string." These words operate on numbers to produce strings. The prefix is semantically wrong. Gating can be achieved at the source file level without encoding it in the name.

## Decision

**Option B: three dedicated words.**

| Word | Stack effect | Behavior |
|------|-------------|----------|
| `n>s` | `( n -- s )` | Signed decimal. `42` -> `"42"`, `-7` -> `"-7"`, `0` -> `"0"` |
| `n>hexs` | `( n -- s )` | Unsigned hex with `0x` prefix, uppercase, minimal digits. `255` -> `"0xFF"`, `0` -> `"0x0"`, `-1` -> `"0x1FFFFFFF"` (raw 29-bit pattern on 32-bit cells) |
| `n>bins` | `( n -- s )` | Unsigned binary with `0b` prefix, minimal digits. `10` -> `"0b1010"`, `0` -> `"0b0"`, `-1` -> `"0b11111111111111111111111111111"` (29 ones on 32-bit cells) |

**Format details:**

- **Decimal**: signed, no prefix. Matches `.` output convention.
- **Hex**: unsigned raw bit pattern. Uppercase hex digits (`A-F`). `0x` prefix always present. Minimal digits (no leading zeros). This matches hardware conventions: register dumps and datasheets use raw unsigned hex.
- **Binary**: unsigned raw bit pattern. `0b` prefix always present. Minimal digits. Same rationale as hex.
- **Zero**: always at least one digit: `"0"`, `"0x0"`, `"0b0"`.

**Why unsigned for hex and binary:** these are display/debugging words. When you print a register value in hex, you want the bit pattern, not a signed interpretation. Decimal is the math representation (signed). Hex and binary are the machine representation (unsigned). This matches the reader: `0xFF` parses as 255, not as a signed value.

**Why minimal digits:** fixed-width output would require padding to cell width. With 29 payload bits, hex would be 7.25 nibbles and binary would be 29 digits, neither of which is a natural width. Minimal digits are readable and sidestep the awkward cell-width alignment.

### Implementation

All three words share a single C conversion routine parameterized by radix (10/16/2), prefix string, and sign behavior.

```
static int number_to_string(froth_cell_t payload, int radix,
                            const char *prefix, bool is_signed,
                            char *buf, int buf_size);
```

Pure formatting function (no stack interaction). Each primitive wrapper pops, type-checks, calls this, then `froth_push_bstring`.

Steps:
1. Extract payload via `FROTH_CELL_STRIP_TAG`.
2. For signed (decimal): if negative, note sign, negate via unsigned cast (avoids C signed-overflow UB). For unsigned (hex/binary): cast payload to `froth_cell_u_t` and mask to `FROTH_CELL_SIZE_BITS - 3` bits.
3. Divide loop into scratch buffer, filling right-to-left. Hex uses `"0123456789ABCDEF"` lookup.
4. Prepend prefix (`"0x"` or `"0b"`) and sign (`"-"`) if needed.
5. Wrapper calls `froth_push_bstring(vm, result, len)` to allocate in transient ring and push tagged cell.

Scratch buffer size is `FROTH_CELL_SIZE_BITS` bytes (`N2S_BUF_SIZE` macro), which covers all radixes at all supported cell widths. Binary worst case on 64-bit cells: `0b` + 61 digits = 63 chars. No heap allocation.

## Consequences

- First runtime string constructors in the kernel. Validates that the transient buffer (ADR-043) works end-to-end for string production.
- Enables `s.concat` to build meaningful dynamic strings (the next roadmap item).
- Three new FFI entries in the kernel primitive table.
- The unsigned-hex convention means `n>hexs` output does not round-trip through the reader for negative numbers. `-7 n>hexs` produces `"0x1FFFFFF9"`, and `0x1FFFFFF9` parses back as `536870905`, not `-7`. This is acceptable: these are display words, not serialization words.
- Scratch buffer size is derived from `FROTH_CELL_SIZE_BITS` via `N2S_BUF_SIZE`, so it scales automatically across all supported cell widths (8, 16, 32, 64).

## References

- ADR-043: transient string buffer
- ADR-023: String-Lite heap layout
- ADR-019: FFI public API
- Spec Section 8: FROTH-String-Lite
- Spec: FROTH-String PAD-style guidance (informative, Section "FROTH-String")
