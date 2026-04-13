# Evaluator Refactor Plan

**When**: immediately after persistence stage 1 lands (target: Mar 10-11)
**Why**: `froth_evaluator.c` mixes structural parsing, sugar lowering, slot policy, heap allocation, and top-level execution in one file. This makes it hard to reason about any one concern.

## Current state

```
input string
  -> froth_reader.c    (lexer: bytes -> tokens)
  -> froth_evaluator.c (everything else: structure, sugar, slots, heap, top-level exec)
  -> froth_executor.c  (runs already-built quotations)
```

`froth_evaluator.c` is 304 lines doing 5 jobs:

1. **Structural parsing**: `count_quote_body`, `count_and_typecheck_pattern_body` (two-pass counting with reader save/restore)
2. **Sugar lowering**: `:` detection and desugaring to `'name [...] def` (lines 253-271)
3. **Heap object construction**: quote building (`handle_open_bracket`), pattern building (`handle_open_pat`), string allocation (`handle_bstring`)
4. **Slot resolution policy**: `resolve_or_create_slot` — the implicit create-on-miss behavior
5. **Top-level dispatch**: `froth_evaluate_input` loop — decides push-vs-execute for each token type

## Target state

Split into three files. No new types, no intermediate representation, no transient allocation. The reader and executor are unchanged.

```
input string
  -> froth_reader.c    (unchanged: bytes -> tokens)
  -> froth_toplevel.c  (top-level loop: dispatch, sugar lowering, slot policy)
  -> froth_builder.c   (heap construction: quotes, patterns, strings)
  -> froth_executor.c  (unchanged: runs quotations)
```

### froth_builder.h / froth_builder.c — heap object construction

Owns: heap layout knowledge, two-pass counting, allocation.

Public functions:
```c
// Build a quotation from tokens. Called after '[' consumed.
// Uses two-pass strategy internally (count, allocate, fill).
// Calls resolve_slot_fn for identifiers inside quote bodies.
froth_error_t froth_build_quote(froth_reader_t *reader, froth_vm_t *vm, froth_cell_t *out);

// Build a pattern from tokens. Called after 'p[' consumed.
// Validates pattern body (single letters a-z, numbers 0-255).
froth_error_t froth_build_pattern(froth_reader_t *reader, froth_vm_t *vm, froth_cell_t *out);

// Allocate a string on the heap from token data.
froth_error_t froth_build_string(froth_token_t token, froth_vm_t *vm, froth_cell_t *out);
```

Key detail: `froth_build_quote` still needs to resolve identifiers to slot indices for CALL cells. It calls the slot resolution function (which lives in toplevel) through a passed-in function pointer or by calling the slot table directly. The simplest version just calls `froth_slot_resolve` (see below).

The counting helpers (`count_quote_body`, `count_and_typecheck_pattern_body`) become static functions inside this file. They're implementation details of the two-pass strategy.

### froth_toplevel.h / froth_toplevel.c — top-level dispatch + policy

Owns: the input loop, `:` sugar, slot creation policy, push-vs-execute decisions.

Public functions:
```c
// Main entry point. Replaces froth_evaluate_input.
froth_error_t froth_toplevel_eval(const char *input, froth_vm_t *vm);

// Slot resolution — centralized policy.
// Currently: create-on-miss. Future: strict mode (error on undefined).
froth_error_t froth_slot_resolve(const char *name, froth_heap_t *heap, froth_cell_u_t *slot_index);
```

This file contains:
- The main `for(;;)` token loop
- `:` sugar detection and lowering (read name, build quote, call def)
- Top-level identifier handling (resolve + execute)
- Top-level tick-identifier handling (resolve + push SLOT cell)
- Top-level push logic for numbers, quotes, patterns, strings

It delegates to `froth_build_*` for compound forms. It never touches heap layout directly.

`froth_slot_resolve` is the **single place** where create-on-miss policy lives. When the strict-identifiers ADR lands, only this function changes. The builder calls it for identifiers inside quotations; toplevel calls it for bare identifiers at top level.

### What moves where

| Current location | Function/block | Destination |
|---|---|---|
| `resolve_or_create_slot` | slot policy | `froth_toplevel.c` as `froth_slot_resolve` |
| `froth_evaluator_handle_number` | push number | inline in `froth_toplevel.c` (3 lines) |
| `froth_evaluator_handle_identifier` | resolve + execute | inline in `froth_toplevel.c` |
| `froth_evaluator_handle_tick_identifier` | resolve + push SLOT | `froth_toplevel.c` (used at top-level and by builder) |
| `count_quote_body` | counting pass | `froth_builder.c` (static) |
| `count_and_typecheck_pattern_body` | counting + validation | `froth_builder.c` (static) |
| `froth_evaluator_handle_open_bracket` | quote construction | `froth_builder.c` as `froth_build_quote` |
| `froth_evaluator_handle_open_pat` | pattern construction | `froth_builder.c` as `froth_build_pattern` |
| `froth_evaluator_handle_bstring` | string allocation | `froth_builder.c` as `froth_build_string` |
| `froth_evaluate_input` | main loop + `:` sugar | `froth_toplevel.c` as `froth_toplevel_eval` |

### The tick-identifier wrinkle

`handle_tick_identifier` is called from two places:
1. Top-level: push a SLOT ref onto DS
2. Inside `handle_open_bracket`: write a SLOT cell into a quote body

Both need slot resolution but differ in output (push to DS vs write to cell). The cleanest solution: make it a small helper in toplevel that just does resolve + make_cell, called by both toplevel dispatch and `froth_build_quote`.

Or: `froth_build_quote` calls `froth_slot_resolve` directly for CALL cells, and handles tick-identifiers by calling resolve + `froth_make_cell(index, FROTH_SLOT, &cell)` inline. Tick resolution is 2 lines — it doesn't need its own function.

### Boundary rules

- **Builder** depends on: reader (token stream), heap (allocation), slot table (resolve names). Does NOT depend on: stack, executor. Never pushes to DS/RS.
- **Toplevel** depends on: reader (init + token loop), builder (compound forms), stack (push), executor (slot invocation), slot table (resolve + create). Owns the `:` sugar.
- **Executor** depends on: slot table, stack, heap. Unchanged.

### Migration steps

1. Create `froth_builder.h` / `froth_builder.c` with `froth_build_quote`, `froth_build_pattern`, `froth_build_string`. Move counting helpers as static functions.
2. Create `froth_toplevel.h` / `froth_toplevel.c` with `froth_toplevel_eval` and `froth_slot_resolve`.
3. Update all callers of `froth_evaluate_input` to call `froth_toplevel_eval` (grep: `main.c`, `froth_repl.c`).
4. Update `CMakeLists.txt`: replace `froth_evaluator.c` with `froth_builder.c` + `froth_toplevel.c`.
5. Delete `froth_evaluator.c` and `froth_evaluator.h`.
6. Verify build and test.

### What this enables

- **Strict bare identifiers**: change `froth_slot_resolve` only. One function, one policy.
- **Persistence hooks**: builder's heap construction is isolated — snapshot traversal can reuse the same heap layout knowledge.
- **Future compilation**: builder is already "compile token stream to heap objects." A DTC pass would replace or wrap it.
- **Explainability**: "Reader tokenizes. Toplevel dispatches and lowers sugar. Builder makes heap objects. Executor runs them."
