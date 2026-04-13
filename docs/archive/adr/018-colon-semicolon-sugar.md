# ADR-018: Colon-Semicolon Definition Sugar

**Date**: 2026-03-06
**Status**: Accepted
**Spec sections**: Interactive spec (`: ;` sugar, optional)

## Context

Defining words with `'foo [ 1 + ] def` is verbose. The spec allows optional `: foo 1 + ;` sugar. We need to decide how to implement it without complicating the reader/evaluator pipeline.

## Options Considered

### Option A: Distinct `;` token with parameterized terminator

Add `FROTH_TOKEN_SEMICOLON`. Pass a `terminator` parameter to `handle_open_bracket` and `count_quote_body` so the `:` handler terminates on `;` while `[` terminates on `]`.

Trade-offs: clean separation, but requires changing every call site of `handle_open_bracket`.

### Option B: Reader treats `;` as `FROTH_TOKEN_CLOSE_BRACKET`

The reader emits `FROTH_TOKEN_CLOSE_BRACKET` for both `]` and `;`. The evaluator special-cases `:` as an identifier: reads the name, pushes a slot ref, calls `handle_open_bracket` (which terminates on the `;`-as-`]`), pushes the quotation, and calls `def`.

Trade-offs: minimal code change, reuses all existing quotation machinery. Nesting works correctly because inner `]` always matches inner `[` first. `;` becomes reserved (cannot be a word name).

### Option C: Synthetic token injection / input rewriting

The `:` handler rewrites the token stream, injecting `[` and replacing `;` with `] def`.

Trade-offs: complex, fragile, no clear benefit.

## Decision

Option B. `;` is treated as `FROTH_TOKEN_CLOSE_BRACKET` in the reader, and `;` is added to the delimiter set. The evaluator handles `:` by checking for a single-character `:` identifier before normal slot lookup.

## Consequences

- `: foo 1 + ;` works with zero changes to quotation building or counting.
- `;` is reserved and cannot appear as a word name or inside identifiers.
- Nested brackets inside colon definitions work naturally: `: foo [ 1 2 ] call ;`.
- A bare `;` at top level acts like a stray `]` — currently silently ignored by the evaluator. Acceptable for now.
