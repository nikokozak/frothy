# FROTH-Named 1b Implementation Checklist

Date: 2026-04-01
Status: planning note
Authority: subordinate to `docs/spec/` and `docs/archive/adr/050-staged-first-froth-named-implementation.md`

This note turns ADR-050 tranche 1b into an implementation checklist. It is not a
new language decision. It assumes the accepted staging:

- plain `:` remains the only definition surface
- signed definitions always provide metadata when countable
- named lowering activates only when the outer body actually uses a bound input
  name
- named mode is a deliberately static subset
- no dynamic-tail escape is provided in tranche 1b

## Goal

Implement the first true named-lowering pass without changing the runtime model.

The tranche should:

- compile bound-name references to generated `perm`
- preserve the entry frame on DS until explicit cleanup
- reject unsupported dynamic operations in named mode
- keep quotations non-capturing
- preserve prompt-never-dies recovery

The tranche should not:

- add locals storage
- add closure capture
- add a general parser
- infer through unknown-arity calls
- make ordinary dynamic Froth illegal

## User-Facing Contract

The user-facing rule for tranche 1b should be:

1. A signed definition always records arity metadata when countable.
2. A signed definition only enters named mode if its outer body uses a bindable
   input name.
3. Named mode is static. If the body relies on dynamic stack behavior, the user
   should write the body in ordinary Froth rather than with named references.

Examples:

```froth
: sumsq ( x y -- r ) x x * y y * + ;
```

This enters named mode.

```froth
: bi ( x f g -- ... ) -rot keep rot call ;
```

This remains an ordinary signed definition with semantic arity metadata, because
the outer body does not use bare `x`, `f`, or `g`.

## Scope Gates

Before writing code, lock these gates:

1. No dynamic-tail escape.
2. No capture in nested quotations.
3. No raw `call`, `catch`, or user-authored `perm` in named mode.
4. No slot auto-creation for bare identifiers during named-mode compilation.
5. Keep executor/runtime semantics unchanged where possible.

## Step 1: Add the Remaining Stable Error Surface

Files:

- `src/froth_types.h`
- `src/froth_repl.c`

Tasks:

1. Add `FROTH_ERROR_NAMED_FRAME = 109`.
2. Add human-readable REPL text for 109.
3. Audit current 108 usage and keep the split clean:
   - 108 = metadata/signature/untracked-operation failure
   - 109 = named-frame/static-stack/named-scope failure

Definition of done:

- `error(108)` and `error(109)` are both surfaced with distinct text.

## Step 2: Raise the Default `perm` Window

Files:

- `src/froth_primitives.h`
- tests that assume the old constant, if any

Tasks:

1. Raise `FROTH_MAX_PERM_SIZE` default from 8 to 16.
2. Keep it build-time configurable.
3. Do not change `perm` runtime semantics.

Rationale:

- named-reference lowering grows windows as `entry_depth + delta + 1`
- cleanup also consumes `N + M`
- 8 is too small for a useful first named pass

Definition of done:

- POSIX build still passes
- no unrelated `perm` regressions

## Step 3: Preserve Bindable Input Metadata From the Signature

Files:

- `src/froth_evaluator.c`

Tasks:

1. Extend the current signature parser so it returns two things:
   - countable arity metadata
   - bindable input metadata for the left side
2. Keep the current permissive counting behavior for `1a` compatibility.
3. Define a bindable input name as a left-side signature token that can appear
   as a normal bare identifier token in a body.
4. Reject duplicate bindable input names.
5. Do not require every counted input token to be bindable.

Examples:

- `x`, `value`, `ledc.timer` may be bindable if the reader would produce them as
  identifiers.
- `...`, `'name`, `f(x)`, `|a|` count for signature ingestion but should not be
  treated as bindable names.

Definition of done:

- the evaluator can map bindable names to entry depths without changing `1a`
  behavior for current stdlib signatures

## Step 4: Add an Outer-Body Probe Pass

Files:

- `src/froth_evaluator.c`

Tasks:

1. Add a dedicated probe pass for colon-definition bodies.
2. The probe must read the outer body until `;` using the same token stream shape
   as the ordinary builder.
3. It must answer:
   - does the outer body use a bound input name?
   - does any nested quotation attempt capture?
   - does the outer body contain a named-mode-forbidden operation?
4. If no bindable name is used, fall back to the existing ordinary builder.

Important rule:

- named mode should activate on actual use, not just on the presence of a
  signature

Definition of done:

- signed definitions that do not use bound names continue down the ordinary path
- a nested quote that uses a bound name is rejected before emission

## Step 5: Define the Named-Mode Delta Model Precisely

Files:

- `src/froth_evaluator.c`

Tasks:

1. Introduce a compile-time context for named mode:
   - `input_count`
   - `output_count`
   - bindable-name table with entry depths
   - `delta`
   - emitted-cell count
2. Use the existing Section 8 depth convention:
   - entry TOS has depth 0
   - deepest input has depth `N - 1`
3. Interpret `delta` as net DS growth above the preserved entry frame.

Allowed `delta` updates:

- number/string/quote/pattern literal: `+1`
- bound name reference: `+1`
- ordinary direct call: `out_arity - in_arity`
- ticked slot literal not followed by `call`: `+1`

Static frame rule:

- if a direct call would need `in_arity > delta`, reject with 109

Definition of done:

- the compiler can explain the current named-frame depth at any point in the
  outer body

## Step 6: Ban Side-Effectful Slot Creation in Named Mode

Files:

- `src/froth_evaluator.c`

Tasks:

1. For named-mode bare identifiers, use `froth_slot_find_name`, not
   `resolve_or_create_slot`.
2. Continue using ordinary slot creation behavior in the non-named path.
3. Keep ticked names as the explicit way to create/reference slots where that is
   already allowed by existing semantics.

Rationale:

- named-mode failures should not leave ghost slots behind
- missing arity should be a clean compile-time failure, not a side-effect plus a
  failure

Definition of done:

- failed named compilation does not mutate the slot table with stray new slots

## Step 7: Implement the Named Count Pass

Files:

- `src/froth_evaluator.c`

Tasks:

1. Add a pass that counts emitted cells for named mode before allocation.
2. Do not reuse `count_quote_body`; one source token may emit multiple cells.
3. Count the emitted shape of each supported construct:
   - literal: 1 cell
   - direct call: 1 cell (`FROTH_CALL`)
   - bound name reference: 3 cells (`n`, pattern ref, `perm` call)
   - `'name call`: 1 cell (`FROTH_CALL`)
   - final cleanup: 3 cells (`n`, pattern ref, `perm` call), unless `M == 0`
4. Reject unsupported tokens or constructs during this pass, not halfway through
   emission.

Definition of done:

- the compiler can allocate the final quotation block exactly once

## Step 8: Implement Name-Reference Lowering

Files:

- `src/froth_evaluator.c`

Tasks:

1. For a bound name with entry depth `d0`, compute `d = d0 + delta`.
2. Compute `n = d + 1`.
3. Reject with 109 if `n > FROTH_MAX_PERM_SIZE`.
4. Generate the duplicate pattern corresponding to:
   - preserve the window
   - duplicate the value currently at depth `d` to TOS
5. Emit:
   - `n` as a number cell
   - generated pattern ref
   - direct `FROTH_CALL` to `perm`
6. Increment `delta` by 1.

The generated pattern should match the current `perm` semantics in
`src/froth_primitives.c`.

Definition of done:

- repeated and deep input references compile into valid quotation bodies without
  runtime support changes

## Step 9: Implement Ordinary Direct Calls in Named Mode

Files:

- `src/froth_evaluator.c`

Tasks:

1. Resolve bare non-bound identifiers to slots using `froth_slot_find_name`.
2. Fetch numeric arity metadata from the slot.
3. If arity is unknown, reject with 108.
4. If `in_arity > delta`, reject with 109.
5. Emit a direct `FROTH_CALL` cell to that slot.
6. Update `delta += out_arity - in_arity`.

Explicitly unsupported in tranche 1b:

- bare `call`
- bare `catch`
- bare `perm`

These should be rejected with 108 when named mode is active.

Definition of done:

- direct fixed-arity user and primitive calls integrate into the static frame
  discipline

## Step 10: Implement `'name call` as a Named-Mode Escape

Files:

- `src/froth_evaluator.c`

Tasks:

1. In named mode, treat the sequence `'name` + `call` as a special compile-time
   construct.
2. Resolve the ticked slot name even if it collides with a bound input name.
3. Require known slot arity; unknown means 108.
4. Apply the same `delta` underflow rule as any direct call.
5. Emit a direct `FROTH_CALL` cell, not a SlotRef literal followed by a runtime
   `call`.

Important:

- if `'name` is not immediately followed by `call`, it remains a literal SlotRef
  and contributes `+1`

Definition of done:

- shadow escape exists without turning runtime `call` into a special global case

## Step 11: Enforce No-Capture in Nested Quotations

Files:

- `src/froth_evaluator.c`

Tasks:

1. During the named probe/count path, recursively scan nested quotations.
2. A nested quotation must reject any bare identifier that matches a bound input
   name from the enclosing named definition.
3. Ticked names in nested quotations are allowed.
4. Nested quotations otherwise continue to compile through the ordinary quote
   builder.

Definition of done:

- `[ x 1 + ]` inside a named definition is rejected if `x` is a bound input
- `x [ 1 + ] call` remains the explicit non-capturing pattern

## Step 12: Emit Final Cleanup

Files:

- `src/froth_evaluator.c`

Tasks:

1. At end of named emission, require `delta == output_count`.
2. If not, reject with 109.
3. If `output_count == 0`, emit cleanup that drops the preserved inputs.
4. If `output_count > 0`, emit cleanup `perm` that keeps the top `M` outputs and
   drops the `N` preserved inputs below them.
5. Reject with 109 if `N + M > FROTH_MAX_PERM_SIZE`.

Definition of done:

- named definitions leave only the declared outputs on DS

## Step 13: Keep the Runtime Untouched Unless Forced

Files likely untouched:

- `src/froth_executor.c`
- `src/froth_reader.c`
- `src/froth_slot_table.c`

Rule:

- if a proposed `1b` change requires runtime frame state, locals storage, or new
  cell tags, stop and revisit the design before landing it

Definition of done:

- named bodies execute correctly as ordinary lowered quotations

## Step 14: Improve Diagnostics Before Shipping

Files:

- `src/froth_repl.c`
- possibly `src/froth_evaluator.c` if lightweight context plumbing is added

Tasks:

1. Keep numeric errors stable.
2. Improve the text presented for 108 and 109 so the user can distinguish:
   - missing arity metadata
   - unsupported dynamic operation in named mode
   - frame underflow
   - output-count mismatch
   - nested capture
   - `perm` window overflow
3. If cheap, include:
   - definition name being compiled
   - offending callee name

Definition of done:

- a failed named definition does not feel arbitrary at the REPL

## Step 15: Test Plan

Add a dedicated named-mode shell test, likely:

- `tests/kernel/test_named.sh`

Required green-path cases:

1. `: sumsq ( x y -- r ) x x * y y * + ;`
2. repeated deep reference to the same input
3. signed definition with no named references still works unchanged
4. `'name call` escapes a collision correctly
5. nested quote with no capture is fine

Required rejection cases:

1. unknown-arity callee in named mode -> 108
2. raw `call` in named mode -> 108
3. raw `catch` in named mode -> 108
4. raw `perm` in named mode -> 108
5. nested quotation capture -> 109
6. frame underflow into preserved inputs -> 109
7. output-count mismatch -> 109
8. lowering window overflow -> 109
9. cleanup window overflow -> 109

Regression cases:

1. `core.froth` still boots and loads
2. signed-but-non-named words still behave as before
3. prompt survives failed named compilation
4. no ghost slots appear after failed named compilation

## Recommended Implementation Order

1. Add 109 and diagnostic text.
2. Raise `FROTH_MAX_PERM_SIZE` default to 16.
3. Extend signature parsing to preserve bindable input metadata.
4. Add outer-body probe pass and no-capture detection.
5. Add named count pass.
6. Add named emit pass for literals, bound references, and direct fixed-arity
   calls.
7. Add `'name call` escape.
8. Add cleanup emission.
9. Add focused named tests.
10. Re-run full kernel shell suite.

## Exit Criteria

Tranche 1b is ready to land when:

- named references compile to lowered quotations without runtime support changes
- the unsupported subset is rejected deterministically
- diagnostics are understandable enough for workshop use
- ordinary Froth remains unchanged outside named mode
- the full kernel shell suite still passes
