# ADR-021: Hex and Binary Number Literals

**Date**: 2026-03-06
**Status**: Accepted
**Spec sections**: 3.1 (Values — Cell/Number), 4.1 (Tokens)

## Context

Froth targets embedded hardware where users frequently work with register addresses, bitmasks, and pin numbers expressed in hexadecimal or binary. Decimal-only input forces mental conversion (e.g., `255` instead of `0xFF`, `10` instead of `0b1010`), which is error-prone and slows down REPL exploration of hardware peripherals.

The reader already parses decimal integers (with optional leading `-`) in `try_parse_number`. We need to extend it to recognize hex and binary prefixes.

## Options Considered

### Option A: New token types (TOKEN_HEX, TOKEN_BINARY)

Reader emits distinct token types; evaluator converts to numeric value.

Trade-offs: Adds complexity to both reader and evaluator. The evaluator would need separate handling for tokens that are semantically identical (they're all just numbers). No downstream consumer needs to know *how* a number was written.

### Option B: Extend `try_parse_number` in the reader

Detect `0x` / `0b` prefixes inside the existing number parser. Produce the same `TOKEN_NUMBER` with the converted value. No changes to token types, evaluator, or anything downstream.

Trade-offs: Simplest change. All number-literal knowledge stays in one place. Downstream code never sees the difference between `255` and `0xFF`.

## Decision

**Option B.** Hex (`0x`) and binary (`0b`) parsing added to `try_parse_number` in `froth_reader.c`.

### Syntax

- **Hex**: `0x` followed by one or more hex digits (`0-9`, `a-f`, `A-F`). Prefix is lowercase only; digits are case-insensitive.
- **Binary**: `0b` followed by one or more binary digits (`0` or `1`). Prefix is lowercase only.
- **Negative**: leading `-` is supported: `-0xFF`, `-0b1010`.
- **Bare prefix**: `0x` or `0b` with no digits is rejected (falls through as an identifier).
- **Invalid digits**: `0xGG`, `0b2` are rejected (fall through as identifiers).

### Tag system interaction

The parsed literal value is the *payload* that gets packed into a tagged cell via `froth_make_cell` (ADR-004). The 3-bit LSB tag consumes 3 bits of the cell, leaving `FROTH_CELL_SIZE_BITS - 3` bits for the signed payload:

| Cell width | Payload bits | Payload range |
|------------|-------------|---------------|
| 64-bit     | 61          | -1,152,921,504,606,846,976 to 1,152,921,504,606,846,975 |
| 32-bit     | 29          | -268,435,456 to 268,435,455 |
| 16-bit     | 13          | -4,096 to 4,095 |

This means the maximum hex literal is *not* the full cell width. On a 32-bit cell, `0x0FFFFFFF` (268,435,455) is the largest positive value; `0x10000000` overflows. Users writing full-width bitmasks (e.g., `0xFFFFFFFF`) will get `FROTH_ERROR_VALUE_OVERFLOW` from `froth_make_cell`. This is the same constraint that applies to decimal literals — it is a consequence of tagging, not of this ADR.

### Display

`.` and `.s` still display numbers in decimal. Hex display (e.g., `.hex`) is not part of this change.

## Consequences

- Users can write hardware register values naturally: `0xFF`, `0b00001111`, `-0x1A`.
- No new token types, no evaluator changes, no changes to stack display.
- The payload-width constraint may surprise users expecting full-width hex masks. This should be documented in the workshop cheat sheet.
- Future: a `.hex` display word could complement this by showing stack values in hex.

## References

- ADR-004: 3-bit LSB value tagging (defines payload width)
- ADR-011: wrapping arithmetic (payload masking and sign extension)
- `froth_reader.c`: `try_parse_number` — sole location of the change
