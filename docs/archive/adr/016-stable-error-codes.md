# ADR-016: Stable explicit error codes

**Date**: 2026-03-04
**Status**: Accepted
**Spec sections**: Section 6.1 (Error codes)

## Context

The spec defines 10 coarse error codes (ERR.STACK = 1, ERR.TYPE = 4, etc.) and permits implementations to define additional codes. Our internal `froth_error_t` enum had many more fine-grained error variants (stack overflow vs underflow, slot not found vs slot empty vs slot impl not found, etc.) but used auto-incremented values that could shift whenever a new variant was added.

With `catch`/`throw` landing (ADR-015), error codes become part of the user-facing API — Froth programs inspect them after `catch` and branch on their values. Auto-incremented enum values are unsuitable: adding a new error between existing ones would silently renumber everything, breaking user code.

The spec's coarse codes (e.g. a single ERR.TYPE = 4 for all type errors) are too ambiguous for debugging on embedded targets where a user at a REPL needs to know *why* something failed, not just the category.

## Options Considered

### Option A: Map to spec error codes

Collapse all internal errors to the spec's 10 codes. `FROTH_ERROR_STACK_OVERFLOW` and `FROTH_ERROR_STACK_UNDERFLOW` both become `1`. `FROTH_ERROR_TYPE_MISMATCH` and `FROTH_ERROR_DIVISION_BY_ZERO` both become `4`.

Trade-offs:
- (+) Spec-compliant numbering.
- (-) Loses diagnostic precision. "type mismatch" and "division by zero" are indistinguishable to the program and to error messages.
- (-) Would need a separate mechanism to preserve fine-grained info for REPL error messages.

### Option B: Two number spaces with mapping

Keep internal auto-incremented enum for C code. Maintain a separate mapping function that produces stable user-visible codes. `catch` calls the mapping before pushing.

Trade-offs:
- (+) Clean separation of concerns.
- (-) Two tables to maintain. Every new error needs an entry in both.
- (-) C code and Froth programs use different numbers for the same error, complicating debugging.

### Option C: Explicit enum values as the API

Assign stable, explicit integer values to every `froth_error_t` variant. These values are the user-facing API — the same numbers that `catch` pushes and that `throw` accepts. New errors are appended with the next available number; existing values never change.

Trade-offs:
- (+) One number space for C and Froth. No mapping function, no dual maintenance.
- (+) Fine-grained errors preserved: programs can distinguish stack overflow from underflow, type mismatch from division by zero.
- (+) Spec-compatible: the spec says "Implementations MAY define additional codes."
- (-) Our error numbers don't match the spec's numbering. A Froth program written assuming ERR.TYPE = 4 would need to use our number (3) instead.

## Decision

**Option C: Explicit enum values.**

Deciding factors:
1. **Debuggability.** On embedded targets, knowing "stack underflow" vs "stack overflow" vs "type mismatch" vs "division by zero" matters. Collapsing them loses real information.
2. **Simplicity.** One number, one meaning, everywhere. No mapping layer.
3. **Stability.** Explicit values can't accidentally shift. The enum is the contract.

Error code organization:
- **Runtime errors (1–99):** Errors that can occur during quotation execution and be caught by `catch`. Stable API.
- **Reader/evaluator errors (100+):** Errors from parsing/compilation. Programs won't typically catch these, but they use the same number space for consistency.
- **Internal sentinel (-1):** `FROTH_ERROR_THROW` is not a user-visible code; it's the C-level signal that unwinding is in progress.

## Consequences

- Error codes are a stable API. Adding new errors means appending, never reordering.
- Our numbers diverge from the spec's suggested numbering. If we later expose named constants (`ERR.STACK`, `ERR.TYPE`, etc.) as Froth words, they'll use our values.
- The REPL's `error_name` table and any future documentation must reflect the actual numeric values.
- `throw` accepts any number the user provides. If a user does `3 throw`, that's `FROTH_ERROR_TYPE_MISMATCH` in our numbering. This is intentional — the numbers are the API.

## References

- Spec v1.1, Section 6.1: "Implementations MAY define additional codes."
- ADR-015: catch/throw via C-return propagation
