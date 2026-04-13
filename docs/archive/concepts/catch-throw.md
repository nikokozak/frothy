# catch/throw — Error Handling in Froth

## Overview

Froth uses `catch` and `throw` for deterministic error recovery. Any error that occurs during execution — whether an explicit `throw` by the user or an internal runtime error like stack underflow — can be intercepted by `catch`. This is the mechanism that keeps the REPL alive: errors don't crash the system, they unwind to the nearest handler.

## The primitives

### `throw ( e -- )`

Takes an error code (a number) from the data stack.

- If `e` is `0`: does nothing. This is a no-op by design, so you can conditionally throw without branching: `some-check throw`.
- If `e` is nonzero: stores `e` in `vm->thrown` and returns the internal sentinel `FROTH_ERROR_THROW` through the C call chain.

The error code `e` is an integer. Our error codes are stable, explicitly numbered values (see ADR-016). For example, `2` is stack underflow, `3` is type mismatch. Users can also throw arbitrary numbers — `42 throw` is valid; `catch` will push `42`.

### `catch ( q -- ... 0 | e )`

Takes a quotation from the data stack and executes it with error protection.

**Step by step:**

1. Pop `q` from DS. Must be a QuoteRef or catch itself throws a type error.
2. Snapshot the current depths: `ds_depth = vm->ds.pointer`, same for RS and CS.
3. Execute `q` via `froth_execute_quote`.
4. Inspect the result:
   - **Success (`FROTH_OK`):** Push `0` onto DS. The quotation's stack effects are preserved — whatever it pushed stays.
   - **Any error:** Restore DS, RS, and CS to their saved depths (truncate the stacks back). Then push the error code as a number.

For explicit throws (`FROTH_ERROR_THROW`), the error code comes from `vm->thrown` (what the user passed to `throw`). For runtime errors (stack underflow, type mismatch, etc.), the C error code *is* the user-visible code — the `froth_error_t` enum value is pushed directly.

## How unwinding works

The executor uses C recursion: `froth_execute_quote` calls `froth_execute_slot` which may call `froth_execute_quote` again. Errors propagate upward via `FROTH_TRY`, which is a macro that returns early on any non-OK result:

```c
#define FROTH_TRY(expr) do { froth_error_t _err = (expr); if (_err != FROTH_OK) return _err; } while(0)
```

When `throw` fires deep inside nested quotation calls:

```
catch
  └─ froth_execute_quote (body)
       └─ froth_execute_slot (some word)
            └─ froth_execute_quote (that word's impl)
                 └─ throw  →  returns FROTH_ERROR_THROW
            └─ FROTH_TRY propagates FROTH_ERROR_THROW
       └─ FROTH_TRY propagates FROTH_ERROR_THROW
  └─ catch sees err != FROTH_OK, restores depths, pushes error code
```

Each intermediate frame doesn't know about catch/throw — it just sees a non-OK return and propagates it upward via `FROTH_TRY`. The `catch` primitive is the only place that *inspects* the error and decides to handle it instead of propagating further.

This means `catch` intercepts **all** errors, not just explicit throws. A stack underflow, a type mismatch, a division by zero — all of these return non-OK error codes through `FROTH_TRY`, and all are caught.

## Depth restoration

When `catch` intercepts an error, it restores the stack pointers:

```c
froth_vm->ds.pointer = ds_depth;
froth_vm->rs.pointer = rs_depth;
froth_vm->cs.pointer = cs_depth;
```

This is a truncation, not a clear. Items that were on the stack *before* `catch` was called survive. Only the items added (or consumed and re-added) during the failed quotation's execution are discarded.

Example:
```
10 20 [ 1 2 throw ] catch
```
- DS before catch pops its argument: `[10 20 Q:...]`
- catch pops quotation, snapshots depth = 2 (10 and 20)
- quotation pushes 1 and 2, then `throw` fires with code 2
- DS restored to depth 2 → `[10 20]`
- error code pushed → `[10 20 2]`

## REPL integration

The REPL implements "prompt never dies" using the same depth-snapshot pattern, but without `catch`:

```c
froth_cell_u_t ds_snapshot = vm->ds.pointer;
froth_cell_u_t rs_snapshot = vm->rs.pointer;

err = froth_evaluate_input(repl_buffer, vm);
if (err != FROTH_OK) {
    froth_cell_t code = (err == FROTH_ERROR_THROW) ? vm->thrown : (froth_cell_t)err;
    // print error(N): description
    vm->ds.pointer = ds_snapshot;
    vm->rs.pointer = rs_snapshot;
    continue;
}
```

On success, the stack changes persist (the user expects `1 2 +` to leave `3`). On error, the stack is restored to what it was before the failed line, and the REPL prints the error code and name.

An uncaught `throw` at the top level (no `catch` around it) propagates `FROTH_ERROR_THROW` all the way to the REPL, which handles it just like any other error — print, restore, continue.

## Error codes

Error codes are stable, explicitly numbered values that serve as both the internal C enum and the user-facing API (ADR-016):

| Code | Name | Meaning |
|------|------|---------|
| 0 | OK | No error |
| 1 | STACK_OVERFLOW | Data or return stack full |
| 2 | STACK_UNDERFLOW | Pop from empty stack |
| 3 | TYPE_MISMATCH | Wrong value type for operation |
| 4 | UNDEFINED_WORD | Slot has no implementation |
| 5 | DIVISION_BY_ZERO | `/mod` with zero divisor |
| 6 | HEAP_OUT_OF_MEMORY | Heap or slot table exhausted |
| 7 | PATTERN_INVALID | Bad pattern construction |
| 8 | PATTERN_TOO_LARGE | Pattern exceeds FROTH_MAX_PERM_SIZE |
| 9 | IO | Platform I/O failure |
| 10 | NOCATCH | throw with no active catch (informational) |
| 11 | WHILE_STACK | while loop stack discipline violation |
| 12 | VALUE_OVERFLOW | Number doesn't fit in payload bits |
| 13 | BOUNDS | Index out of bounds |

Users can throw any integer. Unknown codes display as "unknown error" but are otherwise handled normally.

## Future: trampoline refactor

The current approach (C-return propagation) works because quotation nesting is shallow in practice. A future trampoline/CS-driven executor (deferred to FROTH-Perf) would replace C recursion with an explicit loop reading continuation frames from CS. In that design, `catch` becomes a CS frame type and `throw` walks CS to find the nearest handler. The `vm->thrown` field and the primitive interfaces stay the same — only the unwinding mechanism changes. See ADR-015 for the full rationale.
