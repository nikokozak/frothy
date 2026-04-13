# ADR-049: Indexed Counted Iteration

**Date**: 2026-03-31
**Status**: Accepted
**Spec sections**: `docs/archive/spec/Froth_Language_Spec_v1_1.md` (core control-flow words, `call`, stdlib `times`), `PROGRESS.md`, `TIMELINE.md`

## Context

Froth currently has one counted-iteration surface: `times`, defined in
`src/lib/core.froth` as a stdlib word on top of `while` and return-stack
juggling.

That existing surface is intentionally narrow:

- `times` is stack-neutral `( n q -- )`
- the body quotation receives no iteration index
- the body discipline is effectively `( -- )`

This is fine for repeated side effects, but it is awkward for ordinary
embedded/library code that wants a count and an index at the same time:

- scanning rows/columns
- repeated hardware setup across numbered channels
- generating or consuming small structured sequences
- nested loops where both indices matter

The current milestone order in `PROGRESS.md` and `TIMELINE.md` explicitly puts
indexed counted iteration ahead of `value` / `to`, `defer` / `is`, and the
first FROTH-Named slice. The point is to improve ordinary code without pulling
in larger naming or syntax machinery too early.

Constraints:

1. **No major architectural rework.** This is a helper-tranche language
   improvement, not a new execution model.
2. **Keep the stack explicit.** Froth should not grow hidden locals frames or
   special loop-variable stacks for this feature.
3. **Preserve recoverability.** Interrupts and body-discipline errors must
   remain catchable and understandable.
4. **Do not pre-commit to FROTH-Named.** Indexed iteration should land before
   named bindings and remain usable on systems that never implement them.
5. **Avoid new reader syntax unless it buys something essential.**

## Options Considered

### Option A: `do ... loop` syntax with implicit `i`

Add new syntax, Forth-style loop words, and an implicit current-index binding
(`i`, and likely eventually `j`, `k`, loop bounds, and related rules).

Trade-offs:

- Pro: familiar to Forth users.
- Pro: compact loop syntax.
- Con: reader/evaluator work, not just a word addition.
- Con: introduces special name-resolution behavior before FROTH-Named.
- Con: creates pressure for a whole loop-variable family rather than one small
  feature.
- Con: harder to keep the design obviously orthogonal and explicit.

### Option B: Indexed iteration as a stdlib word

Add an indexed variant of `times` in `src/lib/core.froth`, built out of
existing words such as `while`, `call`, `>r`, `r>`, and `r@`.

Trade-offs:

- Pro: keeps the kernel primitive surface unchanged.
- Pro: attractive if the definition is simple and readable.
- Con: with the current return-stack surface, the implementation is awkward and
  fragile.
- Con: runtime stack-discipline enforcement becomes indirect or brittle.
- Con: likely pushes the definition toward dense stack acrobatics, which is
  exactly what this feature is meant to relieve.
- Con: a difficult stdlib definition is the wrong teaching surface for a
  high-frequency tool.

### Option C: New primitive `times.i` with explicit DS index

Add a new primitive combinator:

- `times.i ( n q -- )`
- executes `q` exactly `n` times
- pushes the zero-based iteration index before each body execution
- body discipline is `( i -- )`

The index is an ordinary Froth Number on DS. There is no implicit binding and
no hidden loop-variable storage.

Trade-offs:

- Pro: minimal surface addition with clear semantics.
- Pro: no syntax changes.
- Pro: preserves explicit stack semantics.
- Pro: easy to explain, including nested loops.
- Pro: runtime body-discipline checks can be enforced directly, just like
  `while`.
- Con: adds a new primitive rather than a pure library word.
- Con: creates an intentional asymmetry with `while` if `times.i` accepts
  SlotRefs as well as QuoteRefs.

## Decision

**Option C: add a new primitive `times.i` with explicit index passing on DS.**

### Surface

`times.i ( n q -- )`

- If `n <= 0`, execute zero times.
- Otherwise execute `q` for indices `0 .. n-1`.
- On each iteration, push the current index as a Number, then execute `q`.
- `q` must consume that index and leave the surrounding stack depth unchanged.

Examples:

```froth
5 [ . ] times.i
\ prints: 0 1 2 3 4

4 [ dup * . ] times.i
\ prints: 0 1 4 9
```

### Callable policy

`times.i` accepts the same callable classes as `call`:

- QuoteRef
- SlotRef

This is intentionally broader than `while`, which currently only accepts
QuoteRefs. The asymmetry is accepted in this ADR rather than hidden. `while`
may be revisited later if callable parity becomes worthwhile, but that is not
part of this change.

### Error / discipline policy

The body discipline is enforced at runtime:

- snapshot DS depth before the loop starts
- after each body execution, DS depth must match that snapshot

To keep the stable user-facing error number unchanged, the existing runtime
error code `11` is retained but generalized:

- rename the symbol from `FROTH_ERROR_WHILE_STACK` to
  `FROTH_ERROR_LOOP_STACK`
- update the user-facing message to describe loop stack-discipline failure
  generically rather than specifically naming `while`

This keeps the stable numeric API while removing misleading naming once
`times.i` exists.

### Spec placement

`times.i` should be documented in the kernel primitive/control-flow section near
`while`, not folded into the stdlib `times` section.

`times` remains where it is: a stdlib word with stack-neutral `( -- )` body
discipline.

The pair then reads clearly:

- `while`: primitive structured loop
- `times.i`: primitive indexed counted loop
- `times`: simple stdlib helper for the no-index counted case

## Consequences

- Froth gains indexed counted iteration without new syntax or hidden loop
  machinery.
- Nested loops remain explicit and compositional because indices are ordinary
  DS values, not implicit bindings.
- The feature lands cleanly before `value` / `to` and FROTH-Named.
- Future naming-oriented surfaces can lower to `times.i` instead of inventing a
  second indexed-loop backend.
- `while` and `times.i` will temporarily differ in callable acceptance. This is
  a conscious design asymmetry, not an accident.
- The error-name cleanup (`WHILE_STACK` -> `LOOP_STACK`) slightly broadens the
  meaning of error 11 while preserving the stable numeric surface.

### Implementation notes

Recommended primitive algorithm:

1. Pop `q`, then `n`.
2. Require `n` to be a Number.
3. Require `q` to be QuoteRef or SlotRef.
4. If `n <= 0`, return `FROTH_OK`.
5. Record `base_depth = ds.pointer`.
6. For `i = 0; i < n; i++`:
   - check/poll interrupt at the top of the iteration, matching `while`
   - push tagged Number `i`
   - dispatch inline:
     - QuoteRef -> `froth_execute_quote`
     - SlotRef -> `froth_execute_slot`
   - require `ds.pointer == base_depth`, else `ERR.LOOP_STACK`
7. Return `FROTH_OK`.

The dispatch should be written inline in `times.i`. No shared helper is
introduced by this ADR; the duplication is too small to justify it yet.

### Expected tests

- `5 [ . ] times.i`
- `0 [ 99 throw ] times.i`
- negative count executes zero times
- SlotRef body works if stored in a slot and passed as a callable
- bad body such as `3 [ 1 ] times.i` fails with error 11
- interrupt during a long-running `times.i`
- nested explicit-index case:

```froth
3 [ 2 [ over + . ] times.i drop ] times.i
```

This last case is important because it proves the design benefit of explicit DS
indices over implicit `i` bindings: the outer index remains visible as ordinary
stack data, and the inner loop simply pushes another index on top.

## References

- `PROGRESS.md`: immediate next kernel-language tranche
- `TIMELINE.md`: indexed counted iteration milestone entry
- `docs/archive/spec/Froth_Language_Spec_v1_1.md`: `call`, `while`, stdlib `times`
- `src/lib/core.froth`: current `times` definition
