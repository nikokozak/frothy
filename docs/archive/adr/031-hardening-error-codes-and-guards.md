# ADR-031: Hardening — new error codes, call depth guard, prim redefinition ban

**Date**: 2026-03-13
**Status**: Accepted
**Spec sections**: Froth_Interactive_Development_v0_5.md (boot sequence, interrupt semantics)

## Context

Smoke testing on Mar 13 revealed five issues ranging from a segfault to misleading error messages. All share a theme: the kernel lacked guards at boundaries that users will hit during normal exploration.

## Decisions

### 1. Call depth guard (prevents segfault on infinite recursion)

The executor uses C recursion (`froth_execute_quote` → `froth_execute_slot` → `froth_execute_quote`). Unbounded recursion blows the C stack before any Froth-level check triggers.

Fix: `call_depth` counter on the VM struct. `froth_execute_quote` increments on entry, decrements on exit (including all error paths). Exceeding `FROTH_CALL_DEPTH_MAX` (default 64) returns `FROTH_ERROR_CALL_DEPTH` (code 18). The error is catchable.

The executor loop was restructured from `FROTH_TRY` early-returns to `err` variable + `break`, ensuring `call_depth--` always executes.

64 is conservative. At ~3-4 C frames per Froth call level, 64 levels uses roughly 15-25KB of C stack. Safe on both POSIX (8MB default) and ESP32 (configurable, typically 4-8KB for main task). Configurable via `FROTH_CALL_DEPTH_MAX` define.

This is a stopgap. The proper fix is a trampoline/iterative executor (evaluator refactor), which eliminates C recursion entirely.

### 2. Primitive redefinition forbidden

`def` now checks whether the target slot has a C primitive bound. If so, it returns `FROTH_ERROR_REDEF_PRIMITIVE` (code 17) instead of silently setting an impl that can never be reached (prim-first executor dispatch).

This also fixed a pre-existing bug in the colon-sugar handler: it was calling `froth_slot_create` (unconditional new slot) instead of `resolve_or_create_slot` (find-or-create). This meant `: foo ... ;` always created a duplicate slot entry instead of updating the existing one.

### 3. Bare `]` at top level is an error

Previously, a bare `]` outside any quotation context fell through to `default: break` in the evaluator and silently produced an empty quotation. Now returns `FROTH_ERROR_UNEXPECTED_BRACKET` (code 107).

### 4. Reader error propagation in quotation builder

`count_quote_body` (pass 1 of the two-pass quotation builder) now returns `froth_error_t` and propagates reader errors to callers. Previously, a reader error during pass 1 (e.g., string too long) was silently swallowed, causing the builder to miscount tokens and report "unterminated quotation" instead of the real error.

### 5. Slot table full error

`froth_slot_create` now returns `FROTH_ERROR_SLOT_TABLE_FULL` (code 16) when the table is at capacity, instead of the misleading `FROTH_ERROR_HEAP_OUT_OF_MEMORY`.

## New error codes

| Code | Name | Category |
|------|------|----------|
| 16 | `FROTH_ERROR_SLOT_TABLE_FULL` | Runtime |
| 17 | `FROTH_ERROR_REDEF_PRIMITIVE` | Runtime |
| 18 | `FROTH_ERROR_CALL_DEPTH` | Runtime |
| 107 | `FROTH_ERROR_UNEXPECTED_BRACKET` | Reader |

## Consequences

- Infinite recursion no longer crashes the process. Workshop participants can safely write recursive definitions and get a catchable error.
- Primitive redefinition is explicitly forbidden. No silent dead definitions.
- Colon-sugar redefinitions now correctly update the existing slot instead of creating duplicates. This fixes a slot table leak.
- Error messages are accurate: the user sees the real problem, not a misleading side effect.
- `FROTH_CALL_DEPTH_MAX` is a new CMake-configurable knob (default 64).

## References

- ADR-030: platform_check_interrupt and safe boot window (same session)
- Mar 13 smoke test session
