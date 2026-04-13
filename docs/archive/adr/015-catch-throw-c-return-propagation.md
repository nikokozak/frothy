# ADR-015: catch/throw via C-return propagation

**Date**: 2026-03-04
**Status**: Accepted
**Spec sections**: Section 6 (Error Handling — catch/throw), Section 6.1 (Error codes)

## Context

The spec requires `catch` and `throw` as primitives for deterministic error recovery:

- `catch ( q -- ... 0 | e )` installs a handler frame, executes `q`. Normal → push 0. Throw → restore DS/RS/CS depths, push error code.
- `throw ( e -- )` unwinds to the nearest `catch`. No handler → ERR.NOCATCH abort, system stays alive.

The executor currently uses C recursion: `froth_execute_quote` → `froth_execute_slot` → `froth_execute_quote`. Errors propagate upward via `FROTH_TRY` (early return on non-OK). The control stack (CS) exists in the VM struct but is unused.

The question: how does `throw` unwind to the correct `catch`, and what state does a handler frame need to capture?

## Options Considered

### Option A: CS-driven trampoline (VM loop)

Refactor the executor into a flat loop. Quotation calls push continuation frames onto CS instead of recursing in C. `catch` pushes a handler frame onto CS. `throw` walks CS to find the handler.

Trade-offs:
- (+) Eliminates C stack depth dependency — ideal for tiny targets (ATTiny with ~256B stack).
- (+) CS becomes the single source of truth for execution state.
- (+) Enables future introspection of the handler chain.
- (-) Requires rewriting the entire executor. Every `froth_execute_quote` call becomes a CS push + return-to-loop.
- (-) High risk of introducing regressions in all existing control flow.
- (-) Significant complexity increase for a system that currently works.

### Option B: setjmp/longjmp

`catch` calls `setjmp` to snapshot C execution state. `throw` calls `longjmp` to restore it.

Trade-offs:
- (+) Minimal changes to executor structure.
- (-) `jmp_buf` is large (>200 bytes on some platforms). Nested `catch` requires stacking them.
- (-) Not available or reliable on all embedded toolchains (some bare-metal targets lack it).
- (-) Hides control flow — harder to reason about resource cleanup.
- (-) C stack state between `setjmp` and `longjmp` is silently discarded, which is correct here but fragile.

### Option C: FROTH_ERROR_THROW sentinel with vm->thrown field

`throw` stores the error code in a new `vm->thrown` field and returns `FROTH_ERROR_THROW`. This propagates upward through the existing `FROTH_TRY` chain like any other error. `catch` calls `froth_execute_quote`, then inspects the return:

- `FROTH_OK` → push 0.
- `FROTH_ERROR_THROW` → read `vm->thrown`, restore saved depths, push the error code.
- Any other error → propagate (catch itself had a structural failure).

Trade-offs:
- (+) Zero changes to the executor loop. `FROTH_TRY` already does the unwinding.
- (+) One new field on `froth_vm_t` — trivial memory cost.
- (+) No platform dependencies (no setjmp, no special toolchain support).
- (+) Composes correctly with nesting: inner `catch` intercepts, outer never sees it.
- (+) Migration path to Option A is clean: `vm->thrown` stays, only the unwinding mechanism changes.
- (-) C stack frames are unwound one-by-one via returns, not jumped over. Slightly slower than longjmp for deeply nested throws. Irrelevant in practice — quotation nesting is shallow.
- (-) CS remains unused. Handler frames are implicit in C's call stack, not inspectable.

## Decision

**Option C: FROTH_ERROR_THROW sentinel.**

Deciding factors:
1. **Minimal complexity.** One new error code, one new VM field, two new primitives. No executor refactor.
2. **No platform risk.** Works on ATTiny, ESP32, POSIX, anything with a C compiler.
3. **Clean migration.** When/if we move to a trampoline executor (Option A), the primitive interface and VM field don't change — only the internal unwinding mechanism does.
4. **Correctness.** The spec's semantics are fully satisfiable: depth snapshots are taken at `catch` entry, restored on throw, and the error code is delivered.

The CS-driven trampoline (Option A) is the right long-term architecture but is out of scope for the current milestone. It should be revisited during the FROTH-Perf / DTC work.

## Consequences

- `froth_vm_t` gains a `froth_cell_t thrown` field.
- `froth_error_t` gains `FROTH_ERROR_THROW` and `FROTH_ERROR_NOCATCH` variants.
- `catch` must snapshot DS/RS depths before executing `q` and restore them on `FROTH_ERROR_THROW`.
- `throw` with no active `catch` propagates `FROTH_ERROR_THROW` to the top level. The REPL (or any top-level caller) must handle this as ERR.NOCATCH.
- The REPL should wrap each evaluation in `catch` to satisfy "prompt never dies."
- CS remains unused. A future ADR will address the trampoline refactor.
- `catch` restoring DS depth means truncating (not clearing) — items below the snapshot depth survive.

## References

- Spec v1.1, Section 6: Error Handling
- Spec v1.1, Section 6.1: Error codes (ERR.NOCATCH = 7)
- Spec v1.1, Appendix: "Safe REPL tooling with catch"
- ANS Forth CATCH/THROW (basis for Froth's design)
