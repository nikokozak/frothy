# ADR-025: Multi-line input

**Date**: 2026-03-08
**Status**: Accepted
**Spec sections**: Froth Interactive Development v0.5 — "Expression completeness and multi-line input"

## Context

The spec requires Direct Mode to accumulate input until the reader reports a complete expression. Incomplete constructs (unclosed `[`, `p[`, `(`, `"`) must trigger a continuation prompt rather than an immediate error. The REPL currently reads a single line and feeds it to the evaluator, which fails on any construct that spans a line boundary.

Key constraints:
- The reader and evaluator operate on a complete `const char*` buffer — they have no concept of "more input coming."
- `: ;` sugar implicitly opens a bracket (the evaluator calls `handle_open_bracket` after reading the name), so `:` must be counted as +1 depth.
- `\` line comments consume to `\n`, so the separator between accumulated lines must be `\n`, not a space. A space separator would cause a `\` comment on line N to consume all subsequent lines.

## Options Considered

### Option A: Depth scanner in the REPL (pre-scan)

A lightweight character-level state machine in `froth_repl.c` scans each line for depth changes before the evaluator ever sees the input. Three pieces of state carry across lines: bracket depth, paren-comment depth, and in-string flag. The REPL accumulates lines into its existing buffer (separated by `\n`) until all three are zero. No reader or evaluator changes required.

Trade-offs: fast, simple, no coupling to tokenizer internals. Risk of scanner diverging from reader rules, but the scanner only needs to correctly skip strings/comments — it doesn't need to produce tokens.

### Option B: Tentative evaluation with retry

Feed each line to the evaluator. If it returns `FROTH_ERROR_UNTERMINATED_QUOTE` / `FROTH_ERROR_UNTERMINATED_STRING` / `FROTH_ERROR_UNTERMINATED_COMMENT`, append the next line and retry from scratch. No new scanner needed.

Trade-offs: simpler code, but re-evaluates from the start on every continuation line (O(n^2) in line count). Also has side effects: the evaluator creates slots eagerly on first reference, so retried evaluation would attempt duplicate slot creation. Would require transactional rollback of heap and slot table state.

### Option C: Reader-level "needs more input" signal

Add a `FROTH_ERROR_INCOMPLETE` return from the reader that the evaluator propagates. The REPL catches it, appends a line, and re-initializes the reader on the extended buffer.

Trade-offs: semantically clean, but the reader currently has no way to distinguish "unterminated because the user isn't done" from "unterminated because the user made an error." The distinction is context-dependent (REPL vs. file evaluation), so pushing it into the reader couples it to the interaction model.

## Decision

Option A: depth scanner in the REPL.

The scanner is ~60 lines of self-contained code with no dependencies on reader internals. It handles:
- `[` / `p[` → bracket depth +1 (the `p` is consumed as a word; the `[` increments naturally)
- `]` / `;` → bracket depth -1
- `:` as a standalone word → bracket depth +1 (implicit open bracket from colon sugar)
- `"..."` → skips string body respecting `\` escapes; sets `in_string` if unclosed at end of text
- `( ... )` → tracks paren-comment depth with nesting; carries across lines if unclosed
- `\` → skips to `\n` or `\0` (line comment)
- `'word` → skips tick and following word (no depth effect)

Separator between lines is `\n`. This is critical: `\` line comments consume to `\n`, so using `\n` ensures they stop at the right boundary. The reader already treats `\n` as whitespace, so non-string code tokenizes identically.

Ctrl-C during continuation clears `vm->interrupted`, discards the accumulated buffer, and returns to the `froth> ` prompt. The guard is the depth state itself: if still nonzero after breaking out of the accumulation loop, the REPL skips evaluation via `continue`.

Continuation prompt: `.. ` (spec recommends `..`).

## Consequences

- Multi-line definitions (`: foo ... ;`), quotations (`[ ... ]`), patterns (`p[ ... ]`), comments (`( ... )`), and strings (`"..."`) all work across line boundaries with no reader or evaluator changes.
- Strings spanning lines capture a literal `\n` at the join point. Users who want a space can restructure; users who want `\n` get it naturally.
- The scanner and reader could theoretically disagree on what constitutes a delimiter or comment. In practice the scanner is conservative: if it misjudges, the worst case is the evaluator sees an incomplete expression and returns an error, which the REPL displays normally.
- Buffer overflow during accumulation (exceeding `FROTH_LINE_BUFFER_SIZE`) breaks out of the continuation loop and lets the evaluator handle the truncated input.

## References

- Froth Interactive Development v0.5, "Expression completeness and multi-line input"
- ADR-018 (colon-semicolon sugar — `;` produces `CLOSE_BRACKET`, `:` opens implicit bracket)
- ADR-020 (interrupt flag — Ctrl-C sets `vm->interrupted`)
