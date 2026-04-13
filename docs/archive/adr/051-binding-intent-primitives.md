# ADR-051: Split Raw `def` from Intent-Specific Binding Primitives

**Date**: 2026-04-01
**Status**: Accepted
**Spec sections**: `docs/archive/spec/Froth_Language_Spec_v1_1.md` Section 3.4 (slots), Section 5 (`def`, `get`, `arity!`), Section 8 (FROTH-Named)
**Related ADRs**: ADR-017 (`def` accepts any value), ADR-050 (first staged FROTH-Named implementation)

## Context

ADR-017 deliberately made `def` permissive: any tagged Froth value can become a
slot implementation. That decision is still correct. It keeps the kernel small,
allows slot-backed mutable storage with zero heap churn, and matches the
executor's existing rule:

- QuoteRef impl: execute it
- non-QuoteRef impl: push it as data

The problem discovered during the ADR-050 / TM1629 pass is not that `def` is too
weak. The problem is that higher-level spellings built directly on `def` are too
honest about the VM and not honest enough about programmer intent.

Today:

- `value`, `to`, `assign`, and `set` are just `swap def`
- `is` is also just `swap def`
- raw `def` preserves whatever slot arity metadata happened to already exist

That creates three concrete issues:

1. `value`-style bindings do not automatically provide the `(0 -- 1)` metadata
   that named-mode authoring wants for scalar/data slots.
2. `is` accepts non-callable values and bare SlotRefs, which reads like
   callable rebinding but actually stores data.
3. Rebinding a slot through raw `def` can leave stale arity metadata attached to
   a now-different implementation, and the named compiler may later trust that
   stale metadata.

## Decision

### 1. Keep `def` as the raw binding primitive

`def` remains the low-level, fully permissive primitive:

- stack effect: `( slot value -- )`
- accepts any Froth value as the new impl
- preserves ADR-017 executor semantics

This preserves the language's honest escape hatch. Not every binding form
should promise intent.

### 2. Add a strict data-binding primitive family

`value`, `to`, `assign`, and `set` become real kernel primitives with identical
behavior:

- stack effect: `( value slot -- )`
- `slot` must be SlotRef
- `value` must **not** be QuoteRef
- binding stores the value in the slot
- binding stamps slot arity metadata to `(0 -- 1)`

The key rule is that these are **data binders**, not generic aliases for `def`.
Rejecting QuoteRef is what makes the `(0 -- 1)` stamp truthful.

Non-QuoteRef values, including Number, StringRef, PatternRef, and SlotRef, are
still valid because invoking the slot pushes them as data.

### 3. Add a strict callable-binding primitive

`is` becomes a real kernel primitive:

- stack effect: `( quote slot -- )`
- `slot` must be SlotRef
- source must be QuoteRef

This is intentionally narrow. It does **not** accept bare SlotRefs and does not
perform hidden wrapping or allocation.

The correct callable rebinding idiom is therefore explicit:

```froth
[ foo ] 'hook is
```

not:

```froth
'foo 'hook is
```

The latter stores a SlotRef as data, which is exactly what raw `def` is for.

### 4. Raw `def` clears slot arity metadata on rebind

Any successful `def` rebind clears `slot.in_arity` / `slot.out_arity` back to
unknown.

Reason: raw `def` does not know whether the new impl is callable, data, or a
callable with a different stack effect. Clearing stale metadata is the safest
and most honest policy for a permissive primitive.

This does **not** break signed `:` definitions, because the colon-definition
path already binds first and then explicitly reapplies the recorded metadata.

### 5. `is` also clears arity metadata

Binding a QuoteRef through `is` does not infer stack effect. The implementation
therefore clears arity metadata, leaving any later effect declaration to `arity!`
or to a signed `:` path.

## Consequences

- The language keeps one honest low-level binding primitive (`def`) and gains
  intent-specific surfaces for data and callable rebinding.
- Slot-backed scalar/data state becomes much less noisy in named-mode code,
  because `value`-family bindings now carry truthful `(0 -- 1)` metadata.
- Bare `def` becomes safer in the presence of FROTH-Named by refusing to leave
  stale arity claims attached to a changed implementation.
- `is` becomes more predictable: its surface now matches callable intent instead
  of being a thin spelling over raw rebinding.
- Programs that truly want to bind arbitrary values still can; they should use
  `def`.

## Non-goals

- No automatic effect inference for quotations bound via `is`
- No hidden SlotRef-to-QuoteRef wrapping
- No executor change for "follow stored SlotRef and invoke target"
- No broader effect system beyond trusted arity metadata

## References

- ADR-017: `def` accepts any value
- ADR-050: staged first FROTH-Named implementation
