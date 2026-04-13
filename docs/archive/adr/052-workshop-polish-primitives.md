# ADR-052: Workshop Polish Tranche (`millis`, Formatted `words`, Tiny Stdlib Helpers)

**Date**: 2026-04-02
**Status**: Accepted
**Spec sections**: `docs/archive/spec/Froth_Language_Spec_v1_1.md` Section 7 (FROTH-Base), Section 10 (FROTH-Stdlib)
**Related ADRs**: ADR-028 (board/platform architecture), ADR-050 (staged first FROTH-Named implementation), ADR-051 (binding-intent primitives)

## Context

The current workshop-facing kernel is functionally strong enough, but two small
rough edges remain visible in ordinary interactive use:

- `words` prints every slot name in one long stream, which becomes hard to scan
  once the stdlib and board FFI are loaded.
- there is no built-in way to read monotonic uptime, even though the platform
  layer already exposes `platform_uptime_ms()` on both POSIX and ESP-IDF.

At the same time, the stdlib still lacks a couple of low-cost helpers that show
up quickly in non-trivial Froth code:

- `2over`
- `flag>n`

This tranche is intentionally small. It improves the day-to-day authoring
surface without opening a larger design project.

## Decision

### 1. Add `millis ( -- n )` as a kernel primitive

`millis` pushes a monotonic millisecond counter supplied by the platform layer.

- source: `platform_uptime_ms()`
- result type: Froth Number
- overflow policy: wrap through the existing tagged-number payload rules

This makes timing reads available everywhere the existing platform support
exists, without forcing projects to depend on board-local FFI for a basic
introspection capability.

`millis` is observational only. It does not replace the existing `ms ( n -- )`
delay word supplied by board/FFI profiles.

### 2. Format `words` output in fixed-width columns

`words` remains an introspection word with stack effect `( -- )`, but its output
format is improved:

- names are emitted in left-aligned columns
- column width is based on the longest visible name
- wrapping uses a fixed target width of 80 characters
- no terminal-width probing is performed

This is an implementation/UI choice, not a new semantic contract. The point is
to make the listing readable in REPL and serial-console environments without
adding platform-specific complexity.

### 3. Add two tiny stdlib helpers

The shipped stdlib now includes:

- `2over ( a b c d -- a b c d a b )`
- `flag>n ( flag -- n )`

Both are definable on the existing kernel surface and do not require new VM
behavior.

## Consequences

- Interactive inspection becomes easier immediately, especially once the board
  FFI and stdlib are loaded.
- Time-aware Froth code can measure elapsed time portably on host and ESP32
  without custom FFI glue.
- Common straight-line stack code gets a little less noisy.

## Non-goals

- No terminal capability probing for `words`
- No new string-helper tranche in this ADR
- No changes to board-local `ms ( n -- )`
- No deeper diagnostic/source-map work
