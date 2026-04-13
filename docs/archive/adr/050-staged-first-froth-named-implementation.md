# ADR-050: Stage the First FROTH-Named Implementation Around Plain `:` Signatures

**Date**: 2026-04-01
**Status**: Accepted
**Spec sections**: `docs/archive/spec/Froth_Language_Spec_v1_1.md` Section 2 (`: ;` sugar), Section 3.4 (slots), Section 5 (`arity!`), Section 6 (errors), Section 8 (FROTH-Named)
**Related ADRs**: ADR-012 (`perm` reading direction), ADR-016 (stable explicit error codes), ADR-017 (`def` accepts any value), ADR-018 (`: ;` sugar), ADR-041 (strict bare identifiers), ADR-049 (`times.i`)

## Context

The immediate kernel-language milestone after slot-zero, `times.i`, `value` /
`to` / `assign`, and `defer` / `is` is the first shippable FROTH-Named slice.
The goal is not to land the entire optional profile in one jump. The goal is
to land the smallest non-regrettable implementation path that:

1. keeps the kernel small,
2. preserves prompt-never-dies recovery,
3. compiles name references to existing `perm` machinery,
4. does not add locals frames, closures, or hidden allocation,
5. and is realistic for the current repo shape and workshop schedule.

During design review, several concrete tensions surfaced:

- The spec's FROTH-Named binding syntax uses `( x y -- z )`, but the current
  reader still treats `( ... )` as nested comments and discards them.
- Colon definitions are evaluator sugar, not a richer parser, by accepted
  design (ADR-018).
- The slot table has no numeric arity metadata today.
- `froth_ffi_entry_t` carries textual `stack_effect` strings, but the compiler
  cannot safely depend on parsing those strings.
- The spec's coarse `ERR.SIG = 8` numbering collides with the implementation's
  stable explicit error table, where code 8 is already
  `FROTH_ERROR_PATTERN_TOO_LARGE` (ADR-016).
- Parameter shadowing must coexist with ADR-041 strict bare identifiers.

An initial proposal introduced a staged `:n` surface to avoid colliding with
existing documentary stack comments on ordinary `:` definitions. After review,
that extra keyword proved unnecessary.

The key repo fact is that existing signed `:` definitions in `src/lib/core.froth`
and board libraries already use `( ... -- ... )` comments, but they do not
reference those parameter names in their bodies. That means plain `:` can
ingest signatures semantically without changing existing behavior, provided the
implementation distinguishes:

- **signature ingestion / arity recording**, and
- **named-body lowering**.

Those are related, but they do not need to land together.

## Decision

### 1. Keep one definition surface: plain `:`

The first implementation of FROTH-Named stays on the spec's existing `:` / `;`
surface. We do **not** introduce `:n`.

If a colon definition contains an immediately following stack-effect header of
the form:

```froth
: name ( in1 in2 -- out1 out2 ) ... ;
```

that header is semantic input to the compiler, not mere documentation.

This keeps the user-facing surface aligned with Section 8 and avoids a second
definition form that would need later migration and deprecation.

### 2. Stage the work into tranche 1a and tranche 1b

The first FROTH-Named implementation is split into two consecutive tranches.

#### Tranche 1a: arity metadata path only

Tranche 1a lands:

- semantic ingestion of `( ... -- ... )` on plain `:` definitions,
- slot storage for numeric `in_arity` / `out_arity`,
- the `arity!` primitive,
- explicit numeric arity seeding for kernel primitives,
- optional numeric arity fields on `froth_ffi_entry_t`,
- and `see` / tooling visibility for the new metadata.

In tranche 1a, colon definition bodies still compile as ordinary quotations.
No named lowering happens yet.

This means existing signed `:` definitions continue to behave exactly as they
do today, while immediately producing useful stack-effect metadata for tooling
and later named compilation.

#### Tranche 1b: named lowering only when the body uses bound names

Tranche 1b adds named-frame compilation on top of tranche 1a metadata.

Only definitions whose **outer body** actually references one of the bound
input names enter named compilation mode. Signed definitions that never use a
bound input name remain ordinary compiled quotations with semantic arity
metadata.

This is the staging rule that keeps plain `:` viable without breaking existing
library code.

### 3. Do not change the reader

The reader remains responsible for comments, tokens, and existing `:` / `;`
sugar only.

The FROTH-Named header is parsed inside the evaluator's colon-definition path,
using the already accepted evaluator-centric architecture from ADR-018.

The implementation should inspect raw source immediately after the definition
name and ingest a leading `( ... -- ... )` header before ordinary comment
skipping discards it.

This keeps the reader simple and avoids broader parser machinery.

### 4. Store arity metadata on slots as compact numeric fields

Slot metadata is the compiler authority for arities.

The slot table gains:

- `in_arity`
- `out_arity`

Both are stored as single bytes with `0xFF` meaning "unknown".

This choice is deliberate:

- no extra validity bit is required,
- the per-slot RAM cost stays at 2 bytes,
- and the representation is sufficient for the current practical arity range.

Binding names do **not** live on slots in this first implementation. Named
input labels remain evaluator-local compile metadata.

### 5. `froth_ffi_entry_t.stack_effect` remains presentation text

The textual `stack_effect` field is for humans and REPL/tool display. It is not
the compiler authority.

`froth_ffi_entry_t` gains optional numeric arity fields. Registration copies
those numeric arities onto the created slots when present. If the numeric
fields are absent or left unknown, the slot arity remains unknown.

Tranche 1a requires explicit numeric arity seeding for kernel primitives.
Board FFI tables may remain unknown initially and can be filled in later.

### 6. Named lowering compiles to `perm` and keeps the entry frame explicit

When tranche 1b named mode is active:

- input names are read-only aliases for entry-stack positions,
- name references lower to generated `perm` sequences,
- values never leave DS,
- no locals frame or closure environment is introduced,
- and quotations do not capture bindings.

The compiler tracks net stack delta above the preserved entry frame.

Ordinary calls in named mode may consume only from the working stack above the
entry frame. If a call's required input arity would underflow into bound inputs,
the definition is rejected at compile time.

### 7. Unknown-arity operations reject named-mode definitions

Tranche 1b rejects named-mode definitions when static arity metadata is not
available for a required operation.

This is an explicit first-pass limitation, not an accident.

Named mode should be described to users as a **narrower static ergonomic
subset**, not as a universal stack-analysis mode that all signed definitions are
expected to satisfy. The purpose of the feature is still ergonomic stack
reference in ordinary code, but the chosen implementation strategy deliberately
limits that affordance to the subset the compiler can lower cleanly to `perm`
without adding locals storage or hidden runtime state.

In particular, named-mode bodies reject use of operations whose stack effects
are intentionally dynamic or not tracked in slot metadata, including the
current first-pass treatment of:

- `call`
- `catch`
- raw user-authored `perm`

These operations remain legal in ordinary definitions. The restriction applies
only when the compiler has entered named mode for that definition.

This ADR explicitly rejects a hybrid "dynamic tail escape" for tranche 1b. Once
the compiler can no longer track the preserved entry frame, the named-frame
model ceases to be a coherent user contract. Rather than silently degrading into
an ambiguous half-named state, tranche 1b keeps the rule simple: either the
definition stays on the ordinary path, or it enters a fully static named-mode
subset and must remain within it.

### 8. `'name call` is an explicit named-mode escape

Section 8 requires bound names to shadow slots inside the body, with `'name call`
used to force slot invocation.

In this implementation, that is not a generic peephole optimization. It is a
specific named-compiler escape hatch:

- `'name` still denotes the SlotRef,
- but in named mode the compiler recognizes `'name call` as a request to emit a
  direct slot call using that slot's recorded arity metadata.

If the target slot lacks known arity metadata, the definition is rejected as a
signature/metadata failure.

### 9. Raise the default `FROTH_MAX_PERM_SIZE` when named lowering lands

The current default of `8` is too tight for a useful first pass at named
lowering. Deep input references plus intermediate delta growth exhaust that
window quickly even for ordinary 3-4 input words.

When tranche 1b lands, the default `FROTH_MAX_PERM_SIZE` should increase to
`16`, while remaining build-time configurable.

If a named reference or final cleanup still exceeds the configured window, the
definition is rejected at compile time.

### 10. Append new stable compile-time error codes

ADR-016 forbids renumbering the implementation's stable explicit error table.
We therefore do **not** reuse the spec's coarse `ERR.SIG = 8` numbering.

Instead, the first implementation appends new compile-time errors in the
reader/evaluator range:

- `108`: signature / arity metadata error
- `109`: named-frame compile-time error

Recommended meanings:

- `108` covers malformed or inconsistent signatures, duplicate input names,
  missing required arity metadata, and unsupported unknown-arity callees in
  named mode.
- `109` covers static underflow into the bound entry frame, output-count
  mismatch, out-of-scope named references, and named-lowering window overflow.

Tranche 1b should not ship on numeric error codes alone. The REPL and host-side
surface must make the failure class legible enough that users can tell whether a
definition failed because:

- a callee lacks arity metadata,
- a dynamic operation is unsupported in named mode,
- a nested quotation attempted capture,
- or the inferred stack frame discipline failed.

Where feasible, diagnostics should include the definition name being compiled
and the offending callee or construct.

### 11. Snapshot persistence of arity metadata is deferred

Tranche 1a and 1b do not require snapshot persistence of slot arity metadata.

Consequence: a restored user-defined word may execute correctly if already
compiled, yet still lack arity metadata for future named-mode compilation that
references it. That later compilation should fail cleanly as a signature/arity
metadata error until snapshot support for arities is added.

This limitation is acceptable for the first implementation and must be
documented plainly.

## Consequences

### What lands earlier

- tooling gets useful user-word arity metadata before named lowering is fully
  implemented,
- `words` / `see` / host inspection work can consume real numeric metadata,
- and the compiler groundwork becomes testable in a smaller first step.

### What remains constrained in the first named pass

- no quotation capture,
- no locals frame backend,
- no broad syntax machinery beyond the colon-definition handler,
- no arity inference through dynamic calls,
- and no promise that every valid Section 8 program lands in tranche 1b.

The first pass optimizes for correctness, recoverability, and repo fit.

It also keeps the conceptual boundary clear:

- ordinary Froth remains the place for fully dynamic stack behavior,
- signed definitions remain useful for metadata even when they never enter named
  mode,
- and named references are an optional static affordance rather than a new
  mandatory discipline for all code.

### RAM cost

The slot-table cost of this feature is intentionally bounded:

- 2 bytes per slot for `in_arity` / `out_arity`
- `0xFF` sentinel for unknown

At `FROTH_SLOT_TABLE_SIZE = 128`, that is 256 bytes of additional slot storage.
That is trivial on ESP32-class targets and still materially smaller than a
3-byte or 4-byte metadata scheme on tiny targets.

### Migration effect on existing code

Existing signed `:` definitions that do not reference their bound input names
continue to compile and run as before. They simply gain semantic arity
metadata once tranche 1a lands.

This is the core reason plain `:` remains viable.

## Alternatives Rejected

### Introduce a separate `:n` surface

Rejected because it creates a second definition form, pushes migration into the
future, and does not actually solve the harder compiler problems.

### Make every signed `:` definition full named mode immediately

Rejected because signature ingestion and named lowering have different risk
profiles. The repo needs the metadata earlier than it needs full body
rewriting, and existing definitions should not be forced through tranche 1b's
unknown-arity restrictions.

### Parse numeric arities from `stack_effect` prose

Rejected because `stack_effect` is presentation text and already includes forms
that are not compiler-stable (`...`, return-stack notes, descriptive wording).

### Defer metadata until full named lowering exists

Rejected because it blocks useful tooling and makes the eventual compiler
harder to validate incrementally.

## Implementation Notes

Recommended order:

1. slot arity storage + getters/setters
2. `arity!`
3. kernel primitive numeric arity seeding
4. semantic signature ingestion on plain `:`
5. `see` / tooling display updates
6. named-mode compiler with explicit `'name call` escape

Tests should be split the same way:

- tranche 1a: metadata correctness and propagation
- tranche 1b: named lowering, no-capture, rejection cases, cleanup behavior

## References

- `PROGRESS.md`
- `TIMELINE.md`
- `docs/archive/spec/Froth_Language_Spec_v1_1.md`
- Design review and follow-up synthesis on 2026-04-01
