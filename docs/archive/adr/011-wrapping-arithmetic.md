# ADR-011: Wrapping Arithmetic via Unsigned Cast and Payload Masking

**Date**: 2026-03-02
**Status**: Accepted
**Spec sections**: Section 5.1 (arithmetic operates with wraparound semantics, two's complement modulo 2^w)

## Context

The spec mandates two's-complement wrapping for all arithmetic (`+`, `-`, `*`). In C, signed integer overflow is undefined behavior — the compiler may optimize away overflow checks, produce garbage, or exhibit any other behavior. We need a technique that produces correct wrapping results portably across all cell widths (8/16/32/64-bit).

Additionally, `lshift` and `rshift` are specified as unsigned (logical) shifts. Left-shifting a negative signed value is UB in C; right-shifting a negative signed value is implementation-defined (arithmetic vs logical). Both need defined behavior.

Payload width is `FROTH_CELL_SIZE_BITS - 3` due to 3-bit LSB tagging (ADR-004).

## Options Considered

### Option A: Signed arithmetic with overflow check

Detect overflow before it happens (e.g., `if (a > 0 && b > MAX - a)`). Reject or clamp.

Trade-offs: Contradicts the spec (wrapping is correct, not an error). Complex branch logic for every op.

### Option B: Unsigned cast, operate, mask, sign-extend

1. Cast signed payload values to `froth_cell_u_t` (unsigned arithmetic is defined to wrap modulo 2^N in C).
2. Perform the operation in unsigned space.
3. Mask result to payload width (`pbits = FROTH_CELL_SIZE_BITS - 3`) to enforce modulo 2^pbits wrapping.
4. Sign-extend from the payload sign bit so the value is correctly interpreted as signed when stored back.

Trade-offs: Correct per spec. Portable. Small constant overhead per op. Requires a helper function.

### Option C: Use compiler builtins (__builtin_add_overflow)

GCC/Clang provide overflow-detecting builtins.

Trade-offs: Not portable (MSVC, embedded compilers). Still need the masking step for payload width. Overkill — we don't need to detect overflow, we need to define it.

## Decision

Option B. Implemented as `froth_wrap_payload()` in `froth_types.h`. Arithmetic primitives (`+`, `-`, `*`) cast operands to unsigned, operate, and wrap. `invert` and `lshift` also use `froth_wrap_payload` since their results can exceed payload width. `rshift` additionally masks the input to payload width before shifting to ensure logical (zero-fill) behavior on sign-extended values.

Bitwise `and`, `or`, `xor` do not need wrapping — bitwise ops on in-range values produce in-range results.

## Consequences

- All arithmetic is well-defined C with no UB, matching spec semantics.
- `froth_make_cell` range check never triggers for wrapped arithmetic results (the wrap guarantees fit).
- The helper is cell-width-agnostic — works for 8/16/32/64-bit builds without changes.
- `divmod` does not use wrapping (division/modulo of in-range values produces in-range results), but the INT_MIN / -1 edge case remains a future concern.

## References

- Spec Section 5.1: "All arithmetic operates on Numbers with wraparound semantics (two's complement modulo 2^w), where w is the cell width."
- C11 §6.2.5/9: unsigned arithmetic wraps modulo 2^N.
- C11 §6.5.7: left shift of negative value is UB; right shift of negative value is implementation-defined.
- ADR-004 (value tagging), ADR-001 (cell width).
