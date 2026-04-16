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
- `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`

## 1. Purpose

This document proposes the next semantic step after the accepted Frothy `v0.1`
core.

It does not replace the accepted `v0.1` contract.
That contract remains the authority for current Frothy behavior.

The frozen baseline for this next-stage work is spoken-ledger syntax tranche 1:

- `name is expr`
- `here name is expr`
- `set place to expr`
- `to name ... [ ... ]` and `fn with ... [ ... ]`
- bracket blocks plus `;`
- `:` calls and `call expr with ...`
- `repeat`, `when`, `unless`, `and`, and `or`
- prompt verbs `show`, `info`, and `remember`
- prompt-only simple-call sugar

That slice is already in tree.
It lowers onto the existing canonical IR, evaluator, and snapshot machinery.

After spoken-ledger syntax tranche 1, records, modules, cond/case,
try/catch, and binding/place designators remain draft.

This document narrows those remaining draft surfaces so later implementation
work can widen runtime semantics deliberately rather than by drift.

## 2. What Must Stay True

The next stage must keep the parts of Frothy that are already correct.

### 2.1 Stable top-level slot identity stays central

The top level remains one namespace of stable named slots.
Any record, module, or recovery surface must preserve that model.

### 2.2 Persistence stays image-oriented

Frothy still persists the overlay image, not running execution.
Local scopes, active evaluation, and host-machine control state remain
non-persisted.

### 2.3 `Code` remains an ordinary value

Callable names are still ordinary slots whose current value is `Code`.
The next stage does not reopen the one-namespace rule.

### 2.4 Canonical IR stays the semantic truth

Surface sugar may grow.
Canonical IR remains the persisted code truth.

### 2.5 No hidden closure machinery by accident

The accepted non-capturing `Code` model remains the base.
Nothing in this draft implies hidden closure capture or invisible environments.

### 2.6 No hidden runtime broadening from this doc alone

This draft is still a design artifact.
It does not itself widen the parser, evaluator, snapshot, or control-session
contracts.

## 3. Frozen Baseline

The following next-stage surfaces are already part of the frozen baseline:

- counted iteration through `repeat count [ ... ]` and
  `repeat count as i [ ... ]`
- short boolean control through `and` and `or`
- single-branch control through `when` and `unless`
- REPL inspection sugar through `show @name`, `core @name`, and `info @name`

They are no longer open design questions in this document.

## 4. Remaining Draft Surfaces

This section narrows the parts of the next-stage draft that are still open.

### 4.1 Multi-way selection with `cond` and `case`

The remaining control additions are:

```txt
cond [
  when condition [ block ]
  when condition [ block ]
  else [ block ]
]

case expr [
  literal [ block ]
  literal [ block ]
  else [ block ]
]
```

`cond` is ordered boolean clause selection.

Rules:

- clauses are considered from top to bottom
- each `when` condition is evaluated left to right
- the first `when` whose condition yields `true` runs its block
- later clauses are skipped once one branch is chosen
- `else` is optional
- if no clause matches and there is no `else`, the whole form yields `nil`

`case` is value-based dispatch.

Rules:

- the scrutinee expression is evaluated exactly once
- clauses are matched from top to bottom
- each clause literal must be one scalar literal from the current core:
  `Int`, `Bool`, `Nil`, or `Text`
- the first equal literal wins
- later clauses are skipped once one branch is chosen
- `else` is optional
- if no clause matches and there is no `else`, the whole form yields `nil`
- there is no fallthrough

These forms are still draft-only in this branch.

### 4.2 Fixed-layout records

Records are the first named aggregate value in the remaining draft.

The chosen draft surface is:

```txt
record Name [ field, field ]
Name: expr, expr
value->field
set value->field to expr
```

Rules:

- `record Name [ field, ... ]` declares a fixed field set and stable field order
- duplicate field names in one record declaration are an error
- field layout does not change for the lifetime of that record definition
- the declaration creates a top-level record definition plus a constructor slot
  `Name`
- `Name: ...` is the constructor call and has exact arity equal to the field
  count
- `value->field` reads a declared field
- `set value->field to expr` mutates an existing record field place
- missing or undeclared fields are errors
- records are not dynamic objects, hash maps, or open property bags

`->` is the chosen field-access spelling because `.` already belongs to the
accepted top-level name model from `v0.1`.

This feature remains draft-only in this branch.

### 4.3 Module grouping through prefixed slots

Modules remain source-time grouping over stable top-level slots.

The chosen draft surface is:

```txt
in led [
  pin is LED_BUILTIN

  to on [ gpio.write: pin, true ]
  to off [ gpio.write: pin, false ]
]
```

Rules:

- `in prefix [ item* ]` is source-time grouping only
- each top-level definition in the body lowers to a prefixed top-level slot
- nested `in` forms concatenate dotted prefixes
- locals and parameters keep their ordinary lexical precedence
- unqualified top-level references inside the body resolve through the active
  prefix first, then through ordinary top-level lookup
- existing shipped helper families such as `math.*`, `random.*`, `gpio.*`,
  and `adc.*` already use this prefixed-slot style today; `in prefix` would
  formalize source-time grouping over that same model rather than introduce a
  second grouping mechanism
- the result is still one flat stable-slot image at runtime
- there is no module object, second namespace, loader, registry, or package
  system in this tranche

Module inspection in this draft means reasoning about grouped prefixed slots,
not loading or persisting a separate module value.

This feature remains draft-only in this branch.

### 4.4 Binding and place designators

Binding/place work is narrowed to stable top-level slot designators only.

The chosen draft surface is:

```txt
@name
@module.name
```

Rules:

- these designate stable top-level slots directly
- they are the narrow ordinary-code extension of the existing REPL inspection
  sugar
- they may be used where code needs slot identity explicitly, especially
  inspection and `set`
- they are not valid for locals, parameters, computed names, cells elements,
  or record fields
- they are designators, not a new persisted runtime value class

One practical consequence is that `set @count to expr` can name the top-level
slot directly even if a local `count` is in scope.

This feature remains draft-only in this branch.

### 4.5 Frothy-native `try/catch`

The chosen recovery surface is:

```txt
try [ block ] catch err [ block ]
fail: err
```

Illustrative shape:

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

Rules:

- the `try` block evaluates normally
- if it succeeds, the whole expression yields that result
- the `catch` block runs for explicit `fail: err` and for catchable runtime
  evaluation failures
- the catch binding is local to the `catch` block
- the whole expression yields the `catch` block result on failure
- there is no implicit rollback of already-completed side effects
- completed slot writes, cells writes, and foreign side effects remain visible
- parse, restore, startup, interrupt, and reset remain boundary-control
  failures in this first step
- because modules are source-time grouping only in this tranche, there are no
  module-loader failures to catch here

If records land in the same tranche, the preferred caught error value is a
small fixed-layout `Error` record with fields:

- `code`
- `kind`
- `origin`
- `detail`

This feature remains draft-only in this branch.

## 5. Libraries And Data Shape

Modules and records solve different problems and should both exist.

- modules organize groups of stable top-level slots
- records organize named data layout
- a module may define record constructors and functions
- a record value may live in a module-owned slot

Neither replaces the other.

## 6. Error Model And Recovery Boundary

The absence of a Froth-style language-visible global `catch` in Frothy `v0.1`
was intentional.
Current recovery still happens at the top-level shell, control-session, boot,
and restore boundaries.

The narrowed next-stage direction is:

- `try/catch` handles runtime evaluation failures only in its first step
- `fail: err` is the ordinary user-visible failure signal
- `try/catch` is explicitly non-transactional
- parser, restore, startup, interrupt, and reset stay boundary-control events
- nothing here reintroduces Froth's old stack-visible global catch model

## 7. Staging

This next-stage work should land in a disciplined order.

### 7.1 Frozen baseline

Spoken-ledger syntax tranche 1 is already landed and frozen:

1. spoken-ledger binding and mutation forms
2. bracket blocks and `;`
3. `to` / `fn with` / `:` calls
4. counted iteration
5. `when`, `unless`, `and`, and `or`
6. prompt verbs plus prompt-only simple-call sugar

### 7.2 Remaining draft order

The remaining design work stays narrower:

1. records
2. modules
3. `cond` and `case`
4. Frothy-native `try/catch` with `fail` and named error values
5. binding/place designators after the above surfaces are explicit

### 7.3 Explicit non-goals for this tranche

This proposal still does not require:

- hidden closures
- a dynamic object system
- general hash maps
- effect handlers
- green threads
- hygienic macros
- transactional rollback semantics
- a module loader, registry, or package surface

## 8. Summary

The remaining next-stage draft is now intentionally smaller:

- counted iteration and short boolean control are frozen baseline
- records are fixed-layout values with `->` field access
- modules are source-time prefix grouping over stable slots only
- `cond` and `case` are the only remaining multi-way control additions
- `try/catch` is paired with `fail` and remains runtime-only plus
  non-transactional
- binding/place designators are restricted to stable top-level slots

That is the smallest coherent next draft that preserves Frothy's live-image
center without widening runtime semantics in this branch.
