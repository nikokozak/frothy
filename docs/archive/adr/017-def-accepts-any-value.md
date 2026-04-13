# ADR-017: `def` accepts any value, not just callables

**Date**: 2026-03-05
**Status**: Accepted
**Spec sections**: 3.4 (slots), 4.2 (invoking a word), 5.2 (`def`), 5.1 (`call`)

## Context

Froth has no dedicated variable or mutable storage mechanism. The spec requires `def`'s `impl` argument to be a callable (QuoteRef or Primitive). This means the only way to store a computed value in a slot is to wrap it in a quotation via `q.pack` — which allocates on the linear heap every time. In a no-GC system, a counter that updates in a loop would leak heap until OOM.

Slots already have the right properties for mutable named storage: stable identity, coherent visibility, in-place update via `def`. The only barrier is the type restriction on `impl`.

## Options Considered

### Option A: Dedicated variable mechanism (`var` / `@` / `!`)

Introduce new words for mutable cells. Either raw memory access (`@`/`!` on addresses) or slot-local storage (`'x var` allocates a cell, `'x @` reads, `42 'x !` writes).

Trade-offs:
- Pro: Separates "word definition" from "value storage" — clear intent.
- Con: New mechanism, new API surface, new concepts to learn.
- Con: Raw `@`/`!` is unsafe and belongs in the platform/FFI layer, not FROTH-Core.
- Con: Slot-local `var` would require new slot table fields and new primitives.

### Option B: Relax `def` to accept any value

Remove the type restriction. `def` stores any value in `slot.impl`. The executor checks: if impl is callable, execute it; if it's a plain value, push it. `get` returns whatever is stored.

Trade-offs:
- Pro: Zero new mechanism. Slots become general-purpose named storage for free.
- Pro: Zero heap cost on update — slot table entry is overwritten in place.
- Pro: Coherent visibility across all references, same as word redefinition.
- Pro: Works naturally with all value types (numbers, StringRefs, SlotRefs, etc.).
- Pro: `set` is trivially defined as `swap def` for imperative-style assignment.
- Con: A slot holding a number is silently "callable" (invoking it pushes the value). This blurs the word/variable distinction.
- Con: FROTH-Checked contract enforcement needs a policy for non-callable impls (see Consequences).

### Option C: `q.pack`-based variables (status quo)

Keep `def` restricted. Store values as `'x [42] def` (literal) or `val 1 q.pack 'x swap def` (computed). Read with `x` (calls the quotation, pushes the value).

Trade-offs:
- Pro: No spec change.
- Con: Every "write" of a computed value allocates a new quotation on the linear heap. Fatal for long-running firmware.
- Con: Requires `q.pack` (not yet implemented) for computed values.
- Con: Stale quotations are never reclaimed without FROTH-Region.

## Decision

**Option B: relax `def` to accept any value.**

The deciding factor is heap cost. An embedded language with no GC cannot afford heap allocation on every variable write. Slots already provide stable identity and in-place mutation — the type restriction is the only thing preventing their use as general-purpose storage.

The "blur" between words and variables is acceptable because Froth's concatenative semantics already treat values as programs: a number is a program that pushes itself. Executing a slot whose impl is a number is semantically consistent — it produces the value.

Raw memory access (`@`/`!`) serves a different purpose (hardware registers, FFI buffers) and should be provided via FFI primitives, not FROTH-Core.

## Consequences

- **`def` change:** remove the callable type check. `def ( slot value -- )` accepts any value.
- **Executor change:** `froth_execute_slot` gains one case: if `slot.impl` is not a QuoteRef, push it onto DS instead of trying to execute it.
- **`call` change:** `call` on a SlotRef whose impl is a non-callable value pushes that value. This is consistent with "executing a literal pushes it."
- **`get` unchanged:** returns whatever `slot.impl` holds.
- **`set` definable as library word:** `'set [swap def] def`. Usage: `42 'x set`.
- **FROTH-Checked note:** contract enforcement on non-callable slots should either skip the contract check (the slot is a value, not a function) or check that the stored value matches an expected kind. This detail is deferred to FROTH-Checked implementation.
- **FROTH-Perf:** promotion (DTC/native) only applies to callable impls. Value-holding slots are not candidates for promotion. `slot.version` still increments on `def`, caches still invalidate correctly.
- **Snapshot persistence:** plain tagged values are self-describing and simpler to serialize than quotations.

## References

- Spec v1.1, Section 3.4 (slots), Section 5.2 (`def`)
- Ergonomics review: `docs/ERGONOMICS_REVIEW.md`
- Forth VALUE/TO pattern (ANS Forth 94)
