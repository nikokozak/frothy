# Frothy Language Specification vNext

Status: Draft proposal
Version: vNext
Date: 2026-04-13
Depends on:
- `Frothy_Language_Spec_v0_1.md`
- `Frothy_Surface_Syntax_Proposal_vNext.md`
- `docs/adr/101-stable-top-level-slot-model.md`
- `docs/adr/103-non-capturing-code-value-model.md`
- `docs/adr/105-canonical-ir-as-persisted-code-form.md`
- `docs/adr/106-snapshot-format-and-overlay-walk-rules.md`
- `docs/adr/107-interactive-profile-boot-and-interrupt.md`
- `docs/adr/112-next-stage-language-growth-and-recovery-boundary.md`

## 1. Purpose

This document proposes the next semantic step after the accepted Frothy `v0.1`
core.

It does not replace the accepted `v0.1` contract.
That contract remains the authority for current Frothy behavior.

This proposal exists because the accepted core is now proven small and live,
but still too narrow for ordinary library-scale work.
The next stage should make Frothy feel:

- workable for real programs,
- strong enough for reusable libraries,
- and more obviously distinct from both C-like batch languages and inherited
  Froth stack-visible programming.

The current draft surface direction for that next stage is a spoken and
ledger-like hybrid optimized for heavy line-by-line REPL use:

- `name is expr` for binding
- `to name ... [ ... ]` for named code
- `set place to expr` for mutation
- bracketed blocks plus `;` for one-line separation
- `:` as the primary call marker in files
- and narrower bare-command sugar at the prompt

The first implemented slice in tree is spoken-ledger syntax tranche 1:

- `name is expr`
- `here name is expr`
- `set place to expr`
- `to name ... [ ... ]` and `fn with ... [ ... ]`
- bracket blocks plus `;`
- `:` calls and `call expr with ...`
- `repeat`, `when`, `unless`, `and`, and `or`
- prompt verbs `show`, `info`, and `remember`
- prompt-only simple-call sugar

That slice lowers onto existing canonical IR and evaluator machinery.
After spoken-ledger syntax tranche 1, records, modules, cond/case,
try/catch, and binding/place values remain draft.

## 2. What Must Stay True

The next stage must keep the parts of Frothy that are already correct.

### 2.1 Stable top-level slot identity stays central

The top level remains one namespace of stable named slots.
Modules, records, iteration helpers, and any later abstraction layer must not
break this.

### 2.2 Persistence stays image-oriented

Frothy still persists the overlay image, not running execution.
Local scopes, active evaluation, and host-machine control state remain
non-persisted.

### 2.3 `Code` remains an ordinary value

The next stage should not reopen the one-namespace rule.
Callable names are still ordinary slots whose current value is `Code`.

### 2.4 Canonical IR stays the semantic truth

Surface sugar may grow.
Canonical IR remains the persisted code truth.

### 2.5 No hidden closure machinery by accident

The accepted non-capturing `Code` model remains the base.
If Frothy later grows richer local abstraction, it should do so through
explicit environment objects or another equally visible mechanism, not by
silently implying hidden closure capture.

## 3. Next-Stage Goal

Frothy should still be explainable through a small number of composable rules,
but the language now needs a larger expressive center.

The missing pressure points are:

- counted iteration,
- multi-branch selection,
- named data layout,
- library encapsulation,
- structured recovery inside ordinary code,
- stronger binding and place inspection,
- and a clearer statement of what recovery means without stack-visible
  `catch`/`throw`.

## 4. Required Additions

This section describes the features that should define the next-stage language
effort.

### 4.1 Indexed counted iteration

Frothy should add a first-class counted loop rather than forcing routine code
to spell every loop as manual local initialization plus `while`.

The semantic requirements are:

- zero or negative counts execute zero times,
- an indexed form is available,
- the iteration index is explicit in user code,
- nested counted loops remain readable,
- and the construct lowers to ordinary lexical and control-flow machinery
  rather than to hidden loop-variable state.

The preferred user-facing shape is:

```txt
repeat count [ block ]
repeat count as i [ block ]
```

Illustrative examples:

```txt
repeat 5 as i [
  gpio.write: i, true
]
```

```txt
repeat height as y [
  repeat width as x [
    drawPixel: x, y
  ]
]
```

Why this matters:

- it covers the practical role that `times.i` covered in Froth,
- it fits Frothy's lexical user model better than a stack-visible combinator,
- and it removes one of the biggest sources of avoidable boilerplate in small
  hardware and library code.

### 4.2 Short-circuit boolean and multi-way selection

Frothy should grow beyond raw `if` and `while`.

The minimum next-stage control additions should be:

- short-circuit `and`
- short-circuit `or`
- `when condition [ block ]`
- `unless condition [ block ]`
- `cond` for ordered multi-branch selection
- `case` for value-based dispatch

The point is not surface novelty.
The point is to stop making ordinary control flow expand into nested `if`
shapes that hide the intent.

### 4.3 Fixed-layout records

Frothy should add fixed-layout records as the first named aggregate value.

Records are not dynamic objects and not hash maps.
They are declared fixed-shape values with named fields and stable field order.

The semantic requirements are:

- the field set is declared up front,
- field layout is fixed for the lifetime of the record definition,
- construction, field read, and explicit field update are supported,
- persistence stays straightforward,
- and implementation can lower record storage to fixed offsets rather than
  dictionary lookup.

The exact field-access spelling is still open.
What matters first is the semantic shape:

- named fields instead of magic indices,
- predictable storage,
- inspectable layout,
- and clear compatibility with the overlay snapshot walk.

Records are the natural next data step after `Cells`.
They should make ordinary stateful code legible without reopening a dynamic
object system.

### 4.4 Module images built from slots

Frothy should add a true module surface for library organization.

Modules are not a second runtime namespace.
They are a way to organize stable slots into explicit library-shaped groups.

The preferred first-step module model is:

- a module expands to stable top-level slots with a prefix,
- the stable slot identity model remains intact,
- module load/inspection can operate in terms of those grouped slots,
- and a module can serve as the natural unit for checked-in library code.

Illustrative shape:

```txt
in led [
  pin is LED_BUILTIN

  to on [ gpio.write: pin, true ]
  to off [ gpio.write: pin, false ]
]
```

The key property is that this remains compatible with the accepted slot model.
A module is not a separate hidden object graph that bypasses slots.
It is a structured way to define and reason about groups of prefixed stable
slots.

This makes modules the right main mechanism for libraries:

- clearer encapsulation,
- explicit grouping,
- easier inspection,
- and a path to loadable slot bundles or module images later.

### 4.5 Binding and place values

Frothy's inspection story should grow from command-only `@name` sugar into a
real place-aware language feature.

The next stage should support binding-oriented operations in ordinary code,
not only at the REPL.

The semantic goal is:

- a program can refer to a named binding itself,
- inspection and controlled rebinding can talk about stable slot identity
  directly,
- and modules remain built out of that same slot identity rather than
  bypassing it.

This feature should stay narrow.
It exists to make Frothy's live image model more explicit, not to introduce a
general reflection free-for-all.

### 4.6 Frothy-native `try/catch`

Frothy should add a structured in-language recovery form.

This should be a Frothy-native construct, not a return to Froth's old
stack-visible global catch model.

The preferred first-step shape is:

```txt
try [ block ] catch err [ block ]
```

Illustrative example:

```txt
to sample [
  try [
    readSensor:
  ] catch err [
    logError: err;
    nil
  ]
]
```

The semantic requirements are:

- the `try` block is evaluated normally
- if it succeeds, the whole expression yields that value
- if a catchable runtime evaluation error occurs, the `catch` block runs
- the catch binding receives a language-visible error value
- the whole expression yields the `catch` block result on failure
- there is no implicit rollback of already-completed side effects
- slot writes, cells writes, and foreign side effects that completed before the
  error remain visible
- parse errors, startup restore failures, and module-load failures remain
  top-level boundary errors rather than expression-level catch cases in the
  first step
- interrupt and reset should remain boundary-control events in the first step,
  not ordinary catchable errors

For Frothy, this feature should be paired with a narrow user-visible failure
signal such as `raise(err)` or `fail(err)`.
Without that, `try/catch` would only handle builtin and foreign failures and
would be weaker than it needs to be for library code.

The caught error should not be a raw anonymous integer if records are available
in the same next-stage tranche.
The preferred direction is a small named error value with fields such as:

- `code`
- `kind`
- `origin`
- `detail`

## 5. Libraries And Encapsulation

Modules and records serve different needs and should both exist.

### 5.1 Modules organize programs

Modules answer:

- which definitions belong together,
- what a library unit contains,
- how a user inspects or loads that unit,
- and where encapsulation boundaries live.

### 5.2 Records organize data

Records answer:

- what fields a value has,
- how state is named,
- how a value is inspected,
- and how code stops depending on raw integer field positions.

### 5.3 Neither replaces the other

A module may define record constructors and functions.
A record value may be stored in a slot owned by a module.
These are orthogonal mechanisms.

## 6. Error Model And Recovery Boundary

The absence of a Froth-style language-visible `catch` in Frothy `v0.1` was not
an accident.
It followed from the current boundary of the language.

### 6.1 Current recovery story

Today Frothy recovers at the top-level evaluation boundary:

- the shell parses and evaluates one complete top-level form at a time,
- parse and evaluation failures are reported and the prompt stays usable,
- startup rebuilds the base image first,
- restore and boot run through an explicit startup report path,
- failed restore leaves the system in a usable base state,
- and safe boot remains the recovery path for bad persisted startup code.

This means Frothy already has recoverability without a language-visible global
`catch`.

### 6.2 Why `try/catch` was deferred in `v0.1`

Adding an in-language `try/catch` is not just a parser exercise.
It forces several semantic decisions:

- which failures are catchable,
- whether parse and load failures are catchable or stay boundary errors,
- whether interrupt is catchable,
- what happens to partial side effects before the error,
- how caught errors interact with `save`, `restore`, `wipe`, and `boot`,
- and how foreign failures appear to user code.

Frothy `v0.1` deliberately deferred this because the live image and recovery
story could already be made correct at the shell, control-session, and boot
boundaries.

### 6.3 Next-stage direction

The next stage should now include a Frothy-native `try/catch`.
The preferred direction is an expression- or block-oriented construct, not a
return of Froth's stack-visible global catch model.

That construct should obey these rules:

- it catches runtime evaluation failures only unless explicitly widened,
- it does not imply transactional rollback of side effects,
- completed slot writes and cells writes remain visible unless a construct
  explicitly says otherwise,
- parser, restore, and module-load failures remain top-level boundary failures
  unless later specified otherwise,
- interrupt and reset remain boundary-control events in the first step,
- and the feature should compose with the one-namespace slot model rather than
  reintroducing hidden control objects.

## 7. Staging

This next-stage work should land in a disciplined order.

### 7.1 First implemented slice

The first implemented slice after the helper/control hardening work is now
spoken-ledger syntax tranche 1:

1. spoken-ledger binding and mutation forms
2. bracket blocks and `;`
3. `to` / `fn with` / `:` calls
4. counted iteration
5. `when`, `unless`, `and`, and `or`
6. prompt verbs plus prompt-only simple-call sugar

### 7.2 Remaining design order

The remaining next-stage design work should now stay narrower:

1. records
2. modules
3. `cond` and `case`
4. Frothy-native `try/catch` with named error values
5. binding/place values after the above surfaces are explicit

### 7.3 Explicit non-goals for this tranche

This proposal does not yet require:

- hidden closures,
- a dynamic object system,
- general hash maps,
- effect handlers,
- green threads,
- hygienic macros,
- or transactional rollback semantics.

Those may become valuable later.
They are not required to make Frothy feel workable and powerful in the next
real step.

## 8. Summary

The next Frothy language step should make five things true:

- loops should read like ordinary loops, not manual `while` scaffolding
- state should have named structure, not only indexed `Cells`
- libraries should have explicit module boundaries
- ordinary code should be able to recover with a Frothy-native `try/catch`
  instead of depending only on the shell boundary
- stable slot identity should become more inspectable and composable
- recoverability should stay explicit without restoring Froth's old
  stack-visible global `catch` model

That is the smallest next language step that seems likely to make Frothy feel
both usable and distinctly its own.
