# ADR-047: Unified String Length Limit

**Date**: 2026-03-21
**Status**: Accepted
**Spec sections**: FROTH-String-Lite (Section 8), ADR-023 (String-Lite heap layout), ADR-043 (transient string buffer)

## Context

The string subsystem currently has fragmented length limits:

- `FROTH_BSTRING_LEN_MAX` (128): hardcoded in `froth_reader.h`, caps string literals at the tokenizer level. Named like a global string cap but only enforced in the reader.
- `FROTH_TBUF_SIZE` (1024): the transient ring buffer capacity. A single transient string could theoretically be up to ~1020 bytes (ring size minus overhead).
- Heap-allocated strings (permanent, via `def`/`s.keep`): no explicit length cap beyond heap exhaustion.

This creates a confusing user experience. A 200-byte string produced by `s.concat` would succeed in the transient buffer but might surprise the user when promoted to the heap. A string literal can't exceed 128 bytes, but an `n>bins` result on a 64-bit cell is 63 bytes and a concat of two such results would be 126. The limits are storage-class-dependent and implementation-leaky.

### User model goal

From the user's perspective, there is one string type. A string either fits or it doesn't. Where it lives (transient ring, heap, snapshot) is the system's problem. One cap, one error, predictable everywhere.

### Transient lifetime

Unnamed runtime strings are temporary and may expire after further string operations. This is a language rule, not a bug. If you need a string to persist, bind it with `def`. `s.keep` exists as an advanced escape hatch. This is documented, not hidden.

## Options Considered

### Option A: Status quo (multiple implicit limits)

Keep `FROTH_BSTRING_LEN_MAX` for the reader, let the transient buffer enforce its own size, no heap cap. Each creation path has its own behavior.

Pros: no work. Cons: confusing, limits leak storage internals, `s.concat` would need to invent its own cap.

### Option B: Single unified cap, CMake-configurable

One compile-time constant `FROTH_STRING_MAX_LEN` enforced at every string creation point. All internal knobs (`FROTH_TBUF_SIZE`, `FROTH_TDESC_MAX`) remain tunable but are implementation details, not language semantics.

Pros: one rule, predictable everywhere. A string that's valid in one context is valid in all contexts (transient, heap, snapshot, promotion). Cons: the cap applies to permanent strings too, so you can't heap-allocate a 2KB string even if the heap has room. (The blob pool, later on the roadmap, is the answer for large payloads.)

## Decision

**Option B.** Introduce `FROTH_STRING_MAX_LEN` with a default of 256.

### Changes

1. **New CMake parameter**: `FROTH_STRING_MAX_LEN` (default 256), added to both POSIX and ESP-IDF CMakeLists.

2. **Retire `FROTH_BSTRING_LEN_MAX`**: the reader's token buffer and length check switch to `FROTH_STRING_MAX_LEN`. The old name is removed.

3. **Enforce at all creation points**:
   - Reader: string literal tokenization (already enforced, just rename the cap)
   - `froth_push_bstring` (FFI): check `len > FROTH_STRING_MAX_LEN` before allocating
   - `froth_tbuf_alloc`: check before ring allocation (defense in depth)
   - `s.concat`: check combined length before allocating
   - Promotion (`froth_bstring_promote`): no separate check needed (source was already validated at creation)

4. **Error code**: reuse `FROTH_ERROR_BSTRING_TOO_LONG` (existing). The error means "this string exceeds the configured maximum length" regardless of where it was created.

5. **Reader token buffer**: `bstring_bytes` in `froth_token_t` grows from `[128]` to `[FROTH_STRING_MAX_LEN]`. On 32-bit ESP32 this adds 128 bytes to the token struct. Acceptable.

6. **Compile-time validation**: `FROTH_STRING_MAX_LEN` must be <= `FROTH_TBUF_SIZE` (a single string can't exceed the entire transient ring). Static assert in `froth_tbuf.h`.

### Why 256

- 128 is too tight for HTTP response building (the primary downstream use case).
- 256 covers practical concatenation of sensor readings, status messages, and simple HTML fragments.
- 512+ starts competing with the default 1024 transient ring (only 1-2 live strings at a time).
- The blob pool (future) handles large payloads.

## Consequences

- Users have one mental model: strings up to `FROTH_STRING_MAX_LEN` bytes, period.
- `FROTH_TBUF_SIZE` and `FROTH_TDESC_MAX` remain internal knobs for power users tuning memory budgets. They do not affect the language-level string size limit.
- A savvy developer can set `-DFROTH_STRING_MAX_LEN=512 -DFROTH_TBUF_SIZE=2048` for a target with more RAM. The static assert prevents misconfiguration.
- The blob pool (roadmap item) will be the escape hatch for payloads that exceed this cap.

## References

- ADR-043: transient string buffer
- ADR-023: String-Lite heap layout
- ADR-046: number-to-string primitives
