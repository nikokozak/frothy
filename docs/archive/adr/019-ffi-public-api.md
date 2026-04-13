# ADR-019: FFI Public C API

**Date**: 2026-03-06
**Status**: Accepted
**Spec sections**: §A "A minimal hardware blink sketch", §A "Defining a callback hook"

## Context

Froth needs a public C API so that FFI authors can write hardware bindings (GPIO, timers, peripherals) without knowing the internal tagging scheme, stack struct layout, or error propagation machinery. Today, built-in primitives directly use `froth_stack_pop`, `FROTH_CELL_STRIP_TAG`, `FROTH_CELL_IS_NUMBER`, etc. Exposing those internals to external binding authors creates coupling that breaks when internals change and forces FFI authors to learn machinery they don't need.

The API must cover the three things every FFI binding does:
1. Pop input values from the data stack.
2. Push result values onto the data stack.
3. Signal errors back to Froth.

A secondary concern is supporting rare FFI functions that need to receive non-number types (e.g., a callback registration word that accepts a quotation).

## Options Considered

### Option A: Single pop/push, always tagged

```c
froth_error_t froth_pop(froth_vm_t *vm, froth_cell_t *cell);  // returns tagged cell
froth_error_t froth_push(froth_vm_t *vm, froth_cell_t cell);   // expects tagged cell
```

FFI authors must understand tagging, use `FROTH_CELL_STRIP_TAG`, check types manually. Uniform, but burdensome for the 90%+ case where you just want a number.

### Option B: Split pop (number vs. tagged), single push (number only)

```c
// Common path: pop a number, get raw payload. Type-checked internally.
froth_error_t froth_pop(froth_vm_t *vm, froth_cell_t *value);

// Power path: pop any cell, get decomposed payload + tag. No type assumption.
froth_error_t froth_pop_tagged(froth_vm_t *vm, froth_cell_t *payload, froth_cell_tag_t *tag);

// Push a number onto DS. Tags internally.
froth_error_t froth_push(froth_vm_t *vm, froth_cell_t value);

// Signal an error. Sets vm->thrown, returns FROTH_ERROR_THROW for propagation.
froth_error_t froth_throw(froth_vm_t *vm, froth_cell_t error_code);
```

Common case is one function call with no tagging knowledge. Power users get structured type info without knowing the bit layout. Push is number-only because FFI authors produce numbers; constructing quotations/slots from C is a runtime-extension concern, not an FFI concern. `froth_push_tagged` can be added later if a real use case appears.

### Option C: Typed pop variants per type

```c
froth_error_t froth_pop_number(froth_vm_t *vm, froth_cell_t *value);
froth_error_t froth_pop_quote(froth_vm_t *vm, froth_cell_t *quote_ref);
froth_error_t froth_pop_slot(froth_vm_t *vm, froth_cell_t *slot_ref);
// ... one per tag
```

Explicit, but proliferates the API surface. Most of these would never be called. `froth_pop_tagged` covers the same ground with one function.

## Decision

**Option B.** Four functions:

| Function | Purpose |
|---|---|
| `froth_pop(froth_vm, &value)` | Pop number (type-checked). Returns stripped payload. |
| `froth_pop_tagged(froth_vm, &payload, &tag)` | Pop any cell. Returns stripped payload + tag enum. |
| `froth_push(froth_vm, value)` | Push a number. Tags internally. |
| `froth_throw(froth_vm, code)` | Set `froth_vm->thrown`, return `FROTH_ERROR_THROW`. |

Deciding factors:
- **Minimal surface.** Four functions cover all FFI needs. `froth_push_tagged` is deferred until a concrete use case exists.
- **Safe by default.** `froth_pop` rejects non-numbers with `FROTH_ERROR_TYPE_MISMATCH`, same error as built-in primitives. Silent type confusion at the hardware boundary is eliminated.
- **Tagging is an implementation detail.** FFI authors never see bit layouts. If the tag encoding changes, FFI code recompiles without modification.
- **Consistent error convention.** All four return `froth_error_t`, matching the `FROTH_TRY` pattern used throughout the codebase. A typical FFI binding looks like:

```c
static froth_error_t gpio_write(froth_vm_t *froth_vm) {
    froth_cell_t value, pin;
    FROTH_TRY(froth_pop(froth_vm, &value));
    FROTH_TRY(froth_pop(froth_vm, &pin));
    hal_gpio_write((int)pin, (int)value);
    return FROTH_OK;
}
```

## Convenience Macros

### `FROTH_FFI` — function definition + metadata

Declares an FFI function with the correct signature and a companion static metadata struct:

```c
FROTH_FFI(gpio_write, "gpio.write", "( pin value -- )", "Write digital output") {
    FROTH_POP(value);
    FROTH_POP(pin);
    hal_gpio_write((int)pin, (int)value);
    return FROTH_OK;
}
```

Expands to:
1. A forward declaration: `static froth_error_t gpio_write(froth_vm_t *froth_vm);`
2. A `static const froth_ffi_entry_t gpio_write_entry` struct containing `{name, word, stack_effect, help}`.
3. The function definition: `static froth_error_t gpio_write(froth_vm_t *froth_vm)`

The VM pointer is always named `froth_vm`, which the other convenience macros depend on.

### `FROTH_POP(name)` / `FROTH_PUSH(value)`

Stack sugar that eliminates per-argument boilerplate:

```c
#define FROTH_POP(name)    froth_cell_t name; FROTH_TRY(froth_pop(froth_vm, &name))
#define FROTH_PUSH(value)  FROTH_TRY(froth_push(froth_vm, (value)))
```

These rely on `froth_vm` being in scope (guaranteed by `FROTH_FFI`).

### `FROTH_BIND(name)` — table entry reference

References the metadata struct created by `FROTH_FFI` for use in a binding table.

### `FROTH_BOARD_BEGIN` / `FROTH_BOARD_END` / `FROTH_BOARD_DECLARE` — board binding tables

Board packages declare their binding tables using these macros:

```c
/* ffi_posix.h */
FROTH_BOARD_DECLARE(froth_board_bindings);

/* ffi_posix.c */
FROTH_BOARD_BEGIN(froth_board_bindings)
  FROTH_BIND(prim_gpio_mode),
  FROTH_BIND(prim_gpio_write),
  FROTH_BIND(prim_ms),
FROTH_BOARD_END
```

`FROTH_BOARD_BEGIN` expands to `const froth_ffi_entry_t name[] = {`, `FROTH_BOARD_END` appends the null sentinel and closing brace, and `FROTH_BOARD_DECLARE` produces the `extern` declaration for the header.

### Registration struct

```c
typedef struct {
    const char *name;
    froth_native_word_t word;
    const char *stack_effect;
    const char *help;
} froth_ffi_entry_t;
```

Built-in primitives have been migrated to use this same struct (with stack effects and help text populated), unifying metadata across kernel and FFI words. This enables `words`, `see`, `info`, and future `help` to work uniformly.

### Board package structure

Board-specific FFI code lives in `boards/<board>/`, separate from the kernel in `src/`:

```
src/                    -- kernel (never touched by board porters)
src/lib/                -- Froth stdlib
boards/posix/           -- POSIX stub board (reference implementation)
boards/esp32/           -- (future)
```

`main.c` registers both kernel primitives and board bindings via `froth_ffi_register`. The build system selects which board directory to compile.

## Error Code Ranges

The throw/catch mechanism operates on plain numbers. To avoid collisions between kernel errors and FFI-defined errors, error code ranges are reserved by convention:

| Range | Owner |
|---|---|
| 1–299 | Froth kernel (runtime errors 1–13, reader errors 100–103, remainder reserved for future kernel use) |
| 300+ | FFI libraries (each library documents its own codes in its own header) |

FFI authors `#define` their error codes in their own headers and pass them to `froth_throw`. No modification to `froth_types.h` is needed. Collision avoidance between independent FFI libraries is by convention at this scale; a structured error identity system is deferred until the namespace/module story is designed.

## Consequences

- FFI bindings are decoupled from tagging internals. Tag bit width or encoding can change without breaking bindings.
- `froth_pop` enforces type safety at the boundary — wrong-type errors are caught consistently, not silently misinterpreted as hardware values.
- `froth_push` only handles numbers. The value must fit in the payload range (29-bit signed on 32-bit cells); `FROTH_ERROR_VALUE_OVERFLOW` is returned otherwise. If a future use case requires pushing tagged values from C (callback registration, metaprogramming), a `froth_push_tagged` must be added. This is intentionally deferred to keep the FFI boundary narrow.
- `froth_throw` lets FFI authors define domain-specific error codes (e.g., `ERR.GPIO`, `ERR.TIMEOUT`) using codes 300+ and the same throw/catch mechanism as Froth-level code.
- These four functions (plus `FROTH_TRY`) are the complete public API an FFI author needs to learn. Everything else is internal.

## References

- Spec §A: "A minimal hardware blink sketch" — `gpio.mode`, `gpio.write`, `ms`
- Spec §A: "Defining a callback hook" — quotation-receiving FFI use case
- ADR-004: Value tagging (internal encoding these functions abstract over)
- ADR-015: catch/throw C-return propagation (`froth_throw` follows this pattern)
