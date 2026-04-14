# Frothy Surface Syntax Proposal

Status: Draft proposal
Version: vNext
Date: 2026-04-13
Depends on:
- `Frothy_Language_Spec_v0_1.md`
- `docs/adr/101-stable-top-level-slot-model.md`
- `docs/adr/103-non-capturing-code-value-model.md`
- `docs/adr/105-canonical-ir-as-persisted-code-form.md`
- `docs/adr/106-snapshot-format-and-overlay-walk-rules.md`
- `docs/adr/107-interactive-profile-boot-and-interrupt.md`
- `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`

## 1. Purpose

This document proposes the surface shape of the next-stage Frothy draft.

It is intentionally not a semantic rewrite.
The accepted `v0.1` contract remains authoritative for:

- the value model
- stable top-level slot identity
- explicit mutation
- save / restore / dangerous.wipe
- non-capturing `Code`
- canonical IR persistence
- boot and inspection behavior

The frozen baseline for surface work is spoken-ledger syntax tranche 1:

- `name is expr`
- `here name is expr`
- `set place to expr`
- `to` / `fn with`
- bracket blocks plus `;`
- `:` calls and `call expr with ...`
- `repeat`, `when`, `unless`, `and`, and `or`
- prompt verbs `show`, `info`, and `remember`
- prompt-only simple-call sugar

After spoken-ledger syntax tranche 1, records, modules, cond/case,
try/catch, and binding/place designators remain draft.

This proposal keeps that split explicit:
the baseline above is frozen, and the remaining draft surface below is still
design work only.

## 2. Frozen Baseline Surface

### 2.1 Top-level forms

The baseline top-level forms are:

```txt
name is expr
set place to expr
to name [ block ]
to name with a, b [ block ]
```

Meaning:

- `name is expr` creates or rebinds the stable top-level slot `name`
- `set place to expr` mutates an existing top-level place
- `to name [ block ]` binds `name` to zero-arity `Code`
- `to name with a, b [ block ]` binds `name` to `Code` with parameters

`in prefix [ ... ]` is not part of this frozen baseline.
It remains draft-only.

### 2.2 Local binding and mutation

The baseline local and mutation forms are:

```txt
here name is expr
set place to expr
```

Rules:

- `here` is valid only inside a block
- `here` creates a new local in the current block scope
- `set` mutates an existing place
- misspelled or missing places still fail instead of creating new names

### 2.3 Calls, anonymous code, and blocks

The baseline call and block surface is:

```txt
name:
name: arg1, arg2
call expr with arg1, arg2
fn [ block ]
fn with a, b [ block ]
[ item* ]
```

Rules:

- `count` reads a value
- `count:` calls that value as `Code`
- `call expr with ...` is the explicit computed-callee escape hatch
- blocks use brackets
- newlines or `;` separate block items

### 2.4 Already-landed control forms

The following are frozen baseline rather than remaining draft:

```txt
repeat count [ block ]
repeat count as i [ block ]
when condition [ block ]
unless condition [ block ]
```

`repeat ... as ...` introduces a readable counted-loop index.
`when`, `unless`, `and`, and `or` are already part of the proved baseline.

### 2.5 REPL baseline

The accepted callable inspection entry points remain:

- `words`
- `see`
- `core`
- `slotInfo`
- `save`
- `restore`
- `dangerous.wipe`

The prompt additionally accepts baseline sugar such as:

```txt
words
show @name
core @name
info @name
remember
restore
dangerous.wipe
name arg1, arg2
path.name arg1, arg2
```

This remains REPL sugar first, not a commitment to make every shortcut a full
file-language form.

### 2.6 Line and separator rules

A newline ends the current form unless the form is clearly incomplete.

In the frozen baseline, incompleteness includes:

- unclosed `[` or `(`
- an unclosed string literal
- trailing `is`, `to`, `with`, `,`, or a binary operator
- `set place to` waiting for an expression
- `to name` waiting for `[` or `with`
- `to name with ...` waiting for `[`
- `repeat ...` waiting for `[`
- `when condition` waiting for `[`
- `unless condition` waiting for `[`

## 3. Remaining Draft Surface

Everything in this section is still draft-only in this branch.

### 3.1 Module grouping with `in prefix`

The remaining draft module surface is:

```txt
in led [
  pin is LED_BUILTIN
  to on [ gpio.write: pin, true ]
]
```

Meaning:

- `in prefix [ ... ]` is source-time grouping over prefixed top-level slots
- it is not a second namespace or a module object
- nested `in` forms concatenate dotted prefixes
- unqualified top-level references inside the body resolve through the active
  prefix first, then fall back to ordinary top-level lookup
- locals and parameters still obey ordinary lexical rules

This is why `in prefix [ ... ]` is draft-only: it needs a precise slot-level
story before it becomes baseline syntax.

### 3.2 Fixed-layout records

The remaining draft record surface is:

```txt
record Point [ x, y ]
Point: 10, 20
point->x
set point->x to 11
```

Meaning:

- `record Name [ field, ... ]` declares fixed field order
- `Name: ...` is the constructor slot
- `value->field` reads a declared field
- `set value->field to expr` updates that field place
- records are not dynamic objects or map-like bags

`->` is chosen here because `.` already belongs to ordinary Frothy slot names
and module-style prefixes.

### 3.3 `cond` and `case`

The remaining draft multi-way control surface is:

```txt
cond [
  when ready [ start: ]
  when fallback [ recover: ]
  else [ nil ]
]

case mode [
  "off" [ led.off: ]
  "on" [ led.on: ]
  else [ fail: Error: 1, "mode", "boot", "unknown" ]
]
```

Meaning:

- `cond` is ordered boolean clause selection
- `case` evaluates its scrutinee once and dispatches on scalar literals only
- both forms may have optional `else`
- neither form has fallthrough

### 3.4 `try/catch` and `fail`

The remaining draft recovery surface is:

```txt
try [
  readSensor:
] catch err [
  logError: err;
  nil
]

fail: err
```

Meaning:

- `try/catch` handles runtime evaluation failures plus explicit `fail`
- it is explicitly non-transactional
- completed side effects remain visible
- parse, restore, startup, interrupt, and reset remain boundary-control events
- this is narrower than Froth's old global catch model

If records land in the same tranche, the preferred caught value is a fixed
`Error` record with fields `code`, `kind`, `origin`, and `detail`.

### 3.5 Restricted ordinary-code `@name`

The remaining draft binding/place extension is:

```txt
@name
@module.name
```

Meaning:

- these designate stable top-level slots directly
- they extend the existing REPL inspection idea into ordinary code only in this
  narrow top-level form
- they are not valid for locals, parameters, computed names, cells elements, or
  record fields
- they are not a new persisted runtime value kind

REPL `show @name`, `core @name`, and `info @name` remain part of the frozen
baseline regardless of whether ordinary-code `@name` lands later.

## 4. Resolved Surface Rules

The previous draft left several collisions open.
This proposal resolves them explicitly.

- `in prefix [ ... ]` is draft-only, not a baseline top-level form
- `repeat`, `when`, `unless`, `and`, and `or` are already baseline
- ordinary-code `@name` is restricted to stable top-level slot designators
- `.` remains slot and prefix spelling, not record-field access
- record fields use `->`
- modules in this tranche imply no loader, registry, package surface, or module
  value

## 5. Implementation Boundary

The baseline parser, evaluator, snapshot, shell, and control-session behavior
are already proved for spoken-ledger tranche 1.

The remaining surfaces in this document stay draft-only until later work
chooses to widen runtime semantics deliberately.

This means:

- no current parser or evaluator change is implied here
- no snapshot-format change is implied here
- no control-session change is implied here
- no module-loader or package work is implied here

## 6. Summary

The surface story is now split cleanly:

- spoken-ledger syntax tranche 1 is frozen baseline
- `in prefix` is draft-only module grouping over prefixed slots
- records are fixed-layout and use `->`
- `cond` and `case` are the remaining multi-way control forms
- `try/catch` is paired with `fail` and stays non-transactional
- ordinary-code `@name` is restricted to stable top-level slot designators

That keeps the current Frothy surface explicit and keeps the next widening step
small enough to review.
