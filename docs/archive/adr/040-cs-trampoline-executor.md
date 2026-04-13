# ADR-040: CS Trampoline Executor

**Date**: 2026-03-17
**Status**: Accepted
**Spec sections**: Froth_Core (quotation execution), ADR-031 (call depth guard)

## Context

The executor (`froth_executor.c`) uses C recursion for nested word calls. When a quotation body contains a `FROTH_CALL` tag, `froth_execute_quote` calls `froth_execute_slot`, which may call `froth_execute_quote` again. Each nesting level consumes a C stack frame (~40-80 bytes depending on platform and compiler).

ADR-031 added a `call_depth` guard (default 64) to prevent C stack overflow. This converts a segfault into a catchable error, but the underlying problem remains: the maximum call depth is determined by the C stack size, which varies across platforms and is invisible to the user.

On ESP32 (8KB default task stack), 64 levels is survivable. On RP2040 or smaller targets, it may not be. The C stack budget is shared with primitives, FFI callbacks, and platform code. Deep nesting competes with all of them.

The CS (call stack) exists on the VM but is currently unused. It was reserved for exactly this purpose.

## Options Considered

### Option A: Keep C recursion, tune the limit per target

Each target sets `FROTH_CALL_DEPTH_MAX` based on its known stack size. ESP32 gets 64, RP2040 gets 32, small targets get 16.

Trade-offs:
- Pro: zero implementation work.
- Con: the limit is a guess. It depends on compiler optimizations, primitive stack usage, and FFI callback depth. A safe limit on one build may crash on another with different flags.
- Con: the failure mode is still opaque. Users don't control C stack size.

### Option B: Trampoline loop with CS frames

Replace C recursion with an explicit loop. When the executor encounters a `FROTH_CALL`, it pushes a continuation frame onto the CS (saving where to resume in the current quotation) and starts executing the callee. When a quotation body ends, the executor pops a CS frame and resumes the caller. The top-level loop runs until the CS is empty.

Trade-offs:
- Pro: C stack depth is O(1) regardless of Froth call depth.
- Pro: maximum call depth is `FROTH_CS_CAPACITY`, which is CMake-configurable, explicit, and platform-independent.
- Pro: failure mode is a clean `FROTH_ERROR_CALL_DEPTH` from a stack bounds check, not a segfault.
- Con: implementation work. The executor loop changes from a simple `for` over body cells to a state machine.
- Con: slight overhead per call (CS push/pop instead of C call/return). Negligible for an interpreter.

### Option C: Tail-call optimization only

Keep C recursion but detect tail calls (last cell in a quotation body is a `FROTH_CALL`) and reuse the current frame. Reduces C stack usage for tail-recursive patterns.

Trade-offs:
- Pro: helps the common recursive case (e.g., `while`-like patterns defined in Froth).
- Con: does not help non-tail calls. Deep mutual recursion still blows the C stack.
- Con: adds complexity without solving the fundamental problem.

## Decision

**Option B.** The CS trampoline replaces C recursion entirely. The executor becomes a single loop that manages its own call stack.

### CS frame format

Each frame is two `froth_cell_u_t` values:

1. **`quote_offset`**: heap byte offset of the quotation.
2. **`ip`**: next cell index to execute (1-based, since cell 0 is the length).

The length is read from `heap[quote_offset]` on each iteration. One extra memory access per resume, but it keeps frames small and avoids converting between byte offsets and cell indices.

Purpose-built `froth_cs_frame_t` struct and `froth_cs_t` stack type, separate from the generic `froth_stack_t` (which handles DS and RS).

### Executor loop

The trampoline peeks at the top CS frame, advances its `ip`, and dispatches the cell. No pop-and-repush needed for the parent frame.

```
push initial frame {quote_offset, ip=1}

while CS not empty above cs_base:
    frame = peek top
    if frame.ip > length: pop, continue
    check interrupt
    cell = base[frame.ip++]
    switch tag:
        literal -> push to DS
        CALL -> look up slot
            if prim -> call C function (may re-enter trampoline)
            if quote -> push new frame {callee_offset, 1}
            if value -> push to DS
    // body exhausted: pop frame, loop resumes parent
```

When a CALL resolves to a quotation, the callee frame is pushed on top. The parent frame's `ip` was already advanced past the CALL cell, so when the callee finishes and is popped, the parent resumes at the right place.

### Re-entrancy (cs_base partitioning)

Primitives like `while`, `catch`, and `call` invoke `froth_execute_quote` from C. Each invocation snapshots `cs_base = vm->cs.pointer` and only processes frames above that base. This makes the trampoline re-entrant: nested invocations use their own partition of the shared CS array. The CS capacity bound applies globally.

### RS balance check

Checked once at trampoline exit: RS depth must match the snapshot taken at entry. Per-frame RS checking was considered and rejected. The trampoline exit check catches imbalances, and the REPL/catch rolls back RS on error. No safety issue from delayed detection: the RS is a user-facing stack, not used by the trampoline itself, and leaked values cannot corrupt execution flow.

### Depth limits

Two separate limits:

1. **`FROTH_CS_CAPACITY`** (default 256): total Froth call nesting depth. Bounds the CS frame array. Intra-trampoline CALL dispatches push frames here at zero C stack cost.

2. **`FROTH_REENTRY_DEPTH_MAX`** (default 64): C-level re-entries into `froth_execute_quote`. Each re-entry adds one C stack frame (~40-60 bytes on ARM). Tracked by `trampoline_depth` counter on the VM. On ESP32 (8KB task stack), 64 re-entries is ~3-4KB, leaving room for prims, FFI callbacks, and platform code.

`call_depth` is removed. `FROTH_ERROR_CALL_DEPTH` (code 18) is reused for both CS overflow and re-entry overflow.

### `while` and other loop primitives

`while` calls `froth_execute_quote` for condition and body. Each call is one C re-entry (counted by `trampoline_depth`), runs the trampoline to completion, returns. No change to `while`'s structure. Same for `catch`, `call`, and any future prim that evaluates quotations.

## Consequences

- Intra-trampoline Froth execution is O(1) C stack regardless of call depth.
- C stack usage from prim-driven re-entries is bounded by `FROTH_REENTRY_DEPTH_MAX` (64).
- Total Froth nesting depth is bounded by `FROTH_CS_CAPACITY` (256).
- The CS element type changes from `froth_cell_t` to `froth_cs_frame_t` (two-cell struct).
- `froth_stack.h` gains `froth_cs_frame_t` and `froth_cs_t` types alongside the generic `froth_stack_t`.
- `call_depth` removed from VM struct. Replaced by `trampoline_depth`.
- All existing tests pass unchanged. The trampoline is an implementation change, not a semantic change.
- Portability improves immediately. Smaller targets get predictable, configurable depth limits without guessing C stack budgets.

## Implementation priority

After `reset` primitive (ADR-037/039). Before RP2040 porting work.

## References

- ADR-031: Call depth guard (the problem this solves permanently)
- ADR-022: RS quotation balance check (must be preserved in trampoline)
- `src/froth_executor.c`: current recursive implementation
- `src/froth_stack.h`: generic stack (may need adaptation for wider CS frames)
