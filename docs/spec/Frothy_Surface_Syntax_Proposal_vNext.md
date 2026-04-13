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

## 1. Purpose

This document proposes a cleaner next-stage surface syntax for Frothy.

It is intentionally not a semantic rewrite.
The accepted `v0.1` contract remains the authority for:

- the value model
- stable top-level slot identity
- explicit mutation
- save / restore / wipe
- non-capturing `Code`
- canonical IR persistence
- boot and inspection behavior

This proposal is about how the same model should look and feel to a human.

The target outcome is simple:
Frothy should read like a language of named live image definitions, not like a
thin lexical wrapper around anonymous `fn` values.

The first implemented slice in tree is spoken-ledger syntax tranche 1:

- `name is expr`
- `here name is expr`
- `set place to expr`
- bracket blocks plus `;`
- `to` / `fn with`
- `:` calls and `call expr with ...`
- `repeat`, `when`, `unless`, `and`, and `or`
- prompt verbs `show`, `info`, and `remember`
- prompt-only simple-call sugar

After spoken-ledger syntax tranche 1, records, modules, cond/case,
try/catch, and binding/place values remain draft.

## 2. Scope And Non-Goals

This proposal is narrow on purpose.

It should improve:

- readability at the prompt
- readability in checked-in Frothy source files
- the distinction between durable image state and local computation
- the feeling that executable definitions are still ordinary named slots

It should not require:

- closure capture
- a second namespace
- a new persistence format
- new persisted runtime object kinds
- a new execution model
- or a large parser or evaluator rewrite

It also does not attempt to solve:

- types
- module loading semantics
- richer data structures
- bytecode
- or a new FFI ABI

Those are later design topics.
This document is only about the next practical surface step.

## 3. Design Constraints From The Accepted Core

The accepted spec and ADRs already constrain what a sensible surface can be.

### 3.1 Stable top-level identity must stay visible

Per ADR-101, the top level is one namespace of stable named slots.
Rebinding changes the value in a slot, not the identity of the slot.

That means the surface should reinforce:

- top-level names are durable image places
- `Code` lives in the same namespace as other values
- redefining a callable name is still just rebinding a slot

### 3.2 Closures are not available in `v0.1`

Per ADR-103, `fn(...) { ... }` may use:

- parameters
- locals it binds itself
- top-level names

It may not capture outer locals.

So the surface should not imply hidden closure state or invisible environments.

### 3.3 Canonical IR remains the semantic truth

Per ADR-105, persisted code truth is canonical IR, not preserved surface text.

That means:

- surface sugar is acceptable
- pretty-printing may recover normalized source-like forms
- but the proposal should not depend on exact-source persistence

### 3.4 Persistence remains overlay-based

Per ADR-106, the snapshot persists overlay top-level slots and persistable
objects reachable from them.

That means:

- top-level definitions matter more than local formatting
- locals remain non-persisted evaluation state
- the proposal should keep the top-level image boundary obvious

### 3.5 The REPL is part of the product

Per ADR-107, multiline input, recovery, interruptibility, boot, and inspection
are part of the language contract.

That means the surface must be judged partly by terminal feel, not only by file
syntax.

## 4. Design Principles

The next surface should follow six principles.

### 4.1 The top level already is the image

Top-level declarations do not need extra ceremony just to say "this is part of
the live image."

At top level, plain named definitions should be enough.

### 4.2 Locals should say that they are local

Top-level names are durable image state.
Locals are temporary computation state.

The local form should make that distinction obvious to a new reader.

### 4.3 Mutation should stay explicit

Creating a name and changing an existing place are different acts.

Keeping that distinction visible is important in a live system because silent
accidental name creation is a real failure mode.

### 4.4 Executable definitions should not look magical

A named executable thing is still a slot that currently holds `Code`.

The surface should make that feel natural.

### 4.5 REPL conveniences should stay cheap

If a terminal convenience can live in the shell surface instead of the core
language grammar, prefer the shell surface first.

That keeps the language smaller and the implementation cheaper.

### 4.6 Novelty is not a goal

This proposal should not borrow syntax merely to look modern.

Every new form should earn its place by improving:

- memorability
- scope clarity
- live patching
- or inspection ergonomics

## 5. Proposed Surface

This draft now prefers a spoken and ledger-like hybrid.

The core idea is:

- the top level should read like editing the live image
- executable definitions should read like telling the machine what to do
- multiline entry must stay REPL-friendly
- every multiline construct must also have a compact one-line form

### 5.1 Top-level binding and named code

At top level, the primary forms should be:

```txt
name is expr
to name [ block ]
to name with a, b [ block ]
in prefix [ item* ]
```

Meaning:

- `name is expr`
  creates or rebinds the stable top-level slot `name`
- `to name [ block ]`
  binds the ordinary top-level slot `name` to zero-arity `Code`
- `to name with a, b [ block ]`
  binds the ordinary top-level slot `name` to `Code` with parameters
- `in prefix [ item* ]`
  groups top-level definitions under a dotted prefix while preserving stable
  slot identity

Examples:

```txt
threshold is 42
frame is cells(3)

to count [ frame[0] ]

to writeFrame [
  set frame[0] to 7;
  set frame[1] to false;
  set frame[2] to "ready"
]

to boot [
  gpio.mode: LED_BUILTIN, true
]
```

Why this shape is right:

- `is` reads better than symbolic assignment in a live notebook language
- `to` makes named code feel authored without pretending it is a second
  namespace
- `to boot [ ... ]` stays honest about `boot` as an ordinary well-known slot
- `in prefix [ ... ]` provides a real grouping surface without a hidden module
  object

### 5.2 Local forms

Inside a block, the primary local binding form should be:

```txt
here name is expr
```

Example:

```txt
to sumTo with limit [
  here total is 0;
  here i is 0;

  while i < limit [
    set total to total + i;
    set i to i + 1
  ];

  total
]
```

`here` still earns its place.
It says the important thing plainly:
this name belongs to this local region, not to the durable image.

Rules:

- `here` is valid only inside a block
- `here` creates a new local in the current block scope
- using `here` for an already-bound name in the same block is an error
- `here` does not create a top-level slot
- `here` does not persist

`here` should not be allowed at top level.
Allowing top-level `here` would either:

- secretly create a slot, which is misleading
- or create a REPL-only ephemeral binding class, which weakens the image model

Both are worse than keeping the rule simple.

### 5.3 Mutation

Mutation should be:

```txt
set place to expr
```

Examples:

```txt
set threshold to 43
set frame[0] to 9
```

Why this shape is right:

- `set` keeps mutation explicit
- `to` reads naturally without the cargo feel of `put ... into ...`
- misspelled places still fail instead of silently creating names

Concrete safety win:

```txt
threshold is 42
set threshold to 43
```

is valid, but:

```txt
set threshhold to 43
```

should fail if `threshhold` does not already exist.

### 5.4 Calls and anonymous code

The draft should stop making ordinary calls look like C.

The preferred core call surface is:

```txt
name:
name: arg1, arg2
gpio.write: pin, true
call expr with arg1, arg2
```

Meaning:

- `count`
  means read the value in `count`
- `count:`
  means call that value as zero-arity `Code`
- `blink: LED_BUILTIN, 3`
  means call the named slot `blink`
- `call expr with ...`
  is the explicit escape hatch for computed callees and higher-order code

This keeps the important one-namespace distinction sharp without dragging the
surface back to parenthesized calls everywhere.

Anonymous code remains ordinary `Code`, but should align with the spoken
surface:

```txt
fn [ block ]
fn with a, b [ block ]
```

Examples:

```txt
factory is fn [ 1 ]

to apply with action [
  action:
]
```

This proposal does not change the current non-capturing rule.
Nested `fn` values remain legal only when they do not capture outer locals.

### 5.5 Blocks and separators

Blocks should be written with brackets:

```txt
[ item* ]
```

This is the right shape for Frothy because:

- it keeps the REPL free of indentation rules
- it gives every multiline construct a compact one-line form
- it nods back toward Froth and Forth without restoring the old user model

Inside a block:

- a newline may separate items
- `;` may separate items on one line
- the last expression still yields the block value

One-line examples:

```txt
to pulse [ gpio.write: LED_BUILTIN, true; wait: 120; gpio.write: LED_BUILTIN, false ]
to inc with n [ n + 1 ]
```

No significant indentation should be introduced in the language itself.
Files may still be indented for readability, but indentation should never carry
semantic weight.

### 5.6 Counted iteration and clause forms

The surface should extend naturally to the queued next-stage control forms:

```txt
repeat count [ block ]
repeat count as i [ block ]
when condition [ block ]
unless condition [ block ]
try [ block ] catch err [ block ]
```

Examples:

```txt
repeat 5 as i [
  gpio.write: i, true
]
```

```txt
when ready [
  blink: LED_BUILTIN, 1
]
```

`repeat ... as ...` is preferable to `repeat ... with ...` because the index is
not an argument being passed.
It is a name being introduced for the loop body.

## 6. Inspection And REPL Surface

The inspection story should improve, but it also has to feel good one line at
a time.

### 6.1 Keep the accepted language entry points

The accepted `v0.1` entry points remain:

- `words()`
- `see("name")`
- `core("name")`
- `slotInfo("name")`
- `save()`
- `restore()`
- `wipe()`

This proposal does not remove them.

### 6.2 Preferred REPL command surface

At the prompt, the shell should additionally accept:

```txt
words
show @name
core @name
info @name
remember
restore
wipe
name arg1, arg2
path.name arg1, arg2
```

Examples:

```txt
words
show @count
info @threshold
remember
blink LED_BUILTIN, 3
led.on
```

This should be implemented as REPL sugar first, not as a new full expression
grammar.

That keeps the feature cheap:

- no new IR kinds
- no new persisted value kind
- no requirement that the file language accept fully bare calls

The rule should stay narrow:
prompt sugar applies only to simple leading name or dotted-name calls.
Inside files and nested expressions, `:` remains the primary call marker.

### 6.3 `@name` means binding, not value

`@name` denotes the named binding itself rather than the value produced by
reading that name.

Given:

```txt
to count [ frame[0] ]
```

then:

- `count`
  means the current value in the slot `count`
- `count:`
  means call that value
- `@count`
  means the slot binding named `count` as an inspectable image object

This is why plain `show count` is not enough for binding inspection.
`count` is an expression that evaluates to a value.
Binding inspection needs a way to talk about the slot itself.

For the immediate next stage, `@name` should be supported only in REPL
inspection commands.
Making `@name` a full expression-level construct can be deferred.

### 6.4 `show`, `info`, and `remember`

The preferred prompt verbs are:

- `show @name` as shell sugar for `see("name")`
- `info @name` as shell sugar for `slotInfo("name")`
- `remember` as shell sugar for `save()`

They read better in an interactive line-by-line environment than the more
library-like callable names, while still lowering to the accepted runtime
entry points.

## 7. Line And Separator Rules

Because Frothy will be used heavily in the REPL, line handling is part of the
language feel.

### 7.1 Default rule

A newline ends the current form unless the form is clearly incomplete.

### 7.2 A form is incomplete when

Any of these are true:

- `[` is unclosed
- `(` is unclosed
- a string literal is unclosed
- the line ends with `is`
- the line ends with `to`
- the line ends with `with`
- the parser has seen `set place to` and is still waiting for an expression
- the line ends with `,`
- the line ends with a binary operator
- the parser has seen `to name` and is still waiting for `[` or `with`
- the parser has seen `to name with ...` and is still waiting for `[`
- the parser has seen `repeat ...` and is still waiting for `[`
- the parser has seen `when condition` and is still waiting for `[`
- the parser has seen `unless condition` and is still waiting for `[`
- the parser has seen `in prefix` and is still waiting for `[`

### 7.3 One-line and multiline should both feel natural

Anything that can be written across several lines should also be writable on
one line with `;`.

Examples:

```txt
to average with a, b, c [ (a + b + c) / 3 ]
to pulse [ gpio.write: LED_BUILTIN, true; wait: 120; gpio.write: LED_BUILTIN, false ]
```

This matters because REPL editing is line-oriented.
The language should not assume the user can comfortably reshape indentation
interactively.

### 7.4 Prompt modes

The REPL should visibly distinguish complete-entry mode from continuation mode:

```txt
frothy> to blink with period [
...>   led.on:;
...>   wait: period;
...>   led.off:;
...>   wait: period
...> ]
```

The continuation prompt should appear only while the parser still expects the
rest of the form.

## 8. Worked Examples

### 8.1 Small stateful sketch

```txt
frame is cells(3)

to count [ frame[0] ]
to enabled [ frame[1] ]
to label [ frame[2] ]

to writeFrame [
  set frame[0] to 7;
  set frame[1] to false;
  set frame[2] to "ready"
]
```

### 8.2 Loop with local state

```txt
to sumTo with limit [
  here total is 0;
  here i is 0;

  while i < limit [
    set total to total + i;
    set i to i + 1
  ];

  total
]
```

### 8.3 Hardware-facing style

```txt
in led [
  pin is LED_BUILTIN;

  to on [ gpio.write: pin, true ]
  to off [ gpio.write: pin, false ]
]

to blink with period [
  led.on:;
  wait: period;
  led.off:;
  wait: period
]

to boot [
  gpio.mode: led.pin, true
]
```

### 8.4 REPL session shape

```txt
frothy> threshold is 42
frothy> frame is cells(1)
frothy> to count [ frame[0] ]
frothy> set frame[0] to 9
frothy> count:
9
frothy> show @count
to count [ frame[0] ]
frothy> blink LED_BUILTIN, 1
frothy> remember
```

## 9. Desugaring To The Accepted `v0.1` Core

This proposal is designed to lower onto the current semantic core with minimal
runtime change.

### 9.1 Top-level value definition

```txt
threshold is 42
```

desugars directly to the current top-level slot binding:

```txt
threshold = 42
```

### 9.2 Named executable slot

```txt
to count [ frame[0] ]
```

desugars to:

```txt
count = fn() { frame[0] }
```

And:

```txt
to blink with pin, n [
  gpio.write: pin, true;
  wait: 120
]
```

desugars to:

```txt
blink = fn(pin, n) {
  gpio.write(pin, true)
  wait(120)
}
```

### 9.3 Local binding

```txt
here total is 0
```

desugars to the current block-local binding form:

```txt
total = 0
```

### 9.4 Mutation

```txt
set frame[0] to 7
```

desugars to:

```txt
set frame[0] = 7
```

### 9.5 Calls

```txt
gpio.write: pin, true
```

desugars to:

```txt
gpio.write(pin, true)
```

Likewise:

```txt
call action with frame[0]
```

desugars to:

```txt
action(frame[0])
```

### 9.6 Prefixed groups

```txt
in led [
  pulse.ms is 120;
  to blink with pin, n [
    wait: pulse.ms
  ]
]
```

desugars conceptually to:

```txt
led.pulse.ms = 120
led.blink = fn(pin, n) {
  wait(led.pulse.ms)
}
```

The important property is that `in led [ ... ]` does not introduce a new
runtime object kind.
It is surface grouping over stable top-level slots.

## 10. Implementation Considerations

The proposal is still primarily parser, shell, and pretty-printing work.
That is why it remains realistic even though it is bolder than the previous
draft.

### 10.1 Lexer and parser

Relevant current files:

- `src/frothy_parser.c`
- `src/frothy_ir.h`

The main parser work is:

- add `is`, `to`, `with`, and `as`
- parse bracketed blocks
- parse `set place to expr`
- parse `:` as the primary call marker
- parse `to name [ ... ]`
- parse `to name with ... [ ... ]`
- keep `in prefix [ ... ]` draft-only until the next design pass
- keep the accepted `v0.1` forms valid during rollout

Important implementation notes:

- `boot` does not need to become a special event form; `to boot [ ... ]` is
  ordinary named code
- REPL sugar should remain a shell concern where possible
- when `in prefix [ ... ]` eventually lands, it should lower to prefixed slot
  names, not a hidden module object

Expected cost:

- bracket blocks: low to medium
- `is` and `set ... to ...`: low
- `to` forms: low to medium
- `:` calls: medium
- deferred `in prefix [ ... ]`: medium

### 10.2 REPL command parsing

Relevant current files:

- `src/frothy_shell.c`
- `src/frothy_inspect.c`

Prompt sugar such as:

- `words`
- `show @name`
- `core @name`
- `info @name`
- `remember`
- `restore`
- `wipe`
- `blink LED_BUILTIN, 3`

should be implemented in the REPL front end first.

That means:

- the accepted `v0.1` callable forms remain valid
- file syntax does not have to accept every REPL shortcut immediately
- `@name` can remain command-only initially
- bare simple calls can stay prompt-only sugar over the `:` call form

Expected cost:

- bare command recognition: low
- `@name` command-surface parsing: low
- prompt-only simple-call sugar: medium
- full expression-level `@name`: deferred

### 10.3 IR and evaluator

The proposal should not require new semantic IR nodes.

All major forms lower to what already exists:

- `name is expr` -> top-level slot write
- `here name is expr` -> local write
- `set place to expr` -> existing place-write handling
- `to` forms -> `FN` plus top-level slot write
- `:` call forms -> ordinary call expression
- later `in prefix [ ... ]` -> prefixed top-level names

So the evaluator in `src/frothy_eval.c` should not need new execution
machinery.

### 10.4 Persistence

The proposal does not change persistence behavior.

The following remain true:

- top-level slots are the persisted unit
- locals do not persist
- `boot` is still just an ordinary slot holding `Code`
- canonical IR remains the persisted code truth

So the snapshot design accepted in ADR-106 remains intact.

### 10.5 Pretty-printing and `see`

To make this surface feel real, `see` should eventually prefer the new
source-like forms where possible:

- `threshold is 42`
- `to count [ frame[0] ]`
- `to blink with period [ ... ]`
- `in led [ ... ]`

This does not require exact-source persistence in snapshots.
It can begin as:

- parser-attached source-form metadata
- REPL-side sidecar metadata
- or a surface-aware re-render from canonical IR where the shape is simple

Exact-source persistence is still optional.
Canonical IR remains normative.
Current in-tree `show` output uses canonical local names such as `arg0` and
`local0` when authored names are not retained by the lowered IR.

### 10.6 Multiline implementation

Relevant current files:

- `src/frothy_shell.c`

The shell already tracks incomplete grouped and quoted input.
To support this proposal well, multiline continuation should also account for:

- trailing `is`
- trailing `to`
- trailing `with`
- trailing `:`
- trailing binary operators
- `to name` waiting for `[` or `with`
- `to name with ...` waiting for `[`
- `repeat ...` waiting for `[`
- `when condition` waiting for `[`
- `unless condition` waiting for `[`
- `in prefix` waiting for `[`

This is still shell work, not evaluator work.

### 10.7 Testing impact

The minimum new proof surface should include:

- parser coverage for `is`
- parser coverage for bracketed blocks
- parser coverage for `to` forms
- parser coverage for `set ... to ...`
- parser coverage for `:` calls
- parser coverage for `call expr with ...`
- parser coverage for `repeat`, `when`, and `unless`
- parser coverage for `and` and `or`
- REPL smoke for `show`, `info`, and `remember`
- REPL smoke for one-line simple-call sugar
- REPL smoke for continuation after `is`, `to`, `:`, `repeat`, `when`, and
  `unless`
- `see` output checks for the new rendered forms where implemented

The existing accepted forms should remain covered during the transition.

## 11. Migration And Rollout

A low-risk rollout should proceed in this order:

1. add bracketed blocks and `;`
2. add `is`
3. add `to` forms
4. add `set ... to ...`
5. add `:` calls
6. add prompt verbs `show`, `info`, and `remember`
7. add prompt-only simple-call sugar
8. add `repeat`, `when`, `unless`, `and`, and `or`
9. improve `see` rendering
10. keep `in prefix [ ... ]`, records, modules, cond/case, and `try/catch`
    in draft until the next design pass

Roadmap position:
spoken-ledger syntax tranche 1 now covers steps 1 through 9 in tree. It is
mostly parser and shell work, it changes the feel of the language
substantially, and it still keeps canonical IR plus the evaluator largely
unchanged.

During rollout, the current `v0.1` forms should remain accepted:

- top-level `name = expr`
- block-local `name = expr`
- `set place = expr`
- `name = fn(args) { ... }`
- parenthesized calls
- `words()`
- `see("name")`
- `core("name")`
- `slotInfo("name")`
- `save()`

That keeps tests, transcripts, and proof scripts working while the surface is
improved incrementally.

## 12. Alternatives Rejected For This Step

These ideas were considered and intentionally left out of this proposal.

### 12.1 `=`

Rejected as the preferred new surface binding form.

It is easy to type, but it drags the eye back toward conventional code and
does not help Frothy feel like a live notebook language.

### 12.2 `<-`

Rejected because it is more awkward to type than it is worth and introduces a
more alien symbol than the draft needs.

### 12.3 `put ... into ...`

Rejected for now.

It is explicit and humane, but it reads a little too cargo-like in ordinary
code.
`set place to expr` preserves the important semantic distinction with less
surface weight.

### 12.4 `on boot`

Rejected because it implies an event or callback system.
The accepted architecture is simpler than that.
`boot` is an ordinary well-known slot, so `to boot [ ... ]` tells the truth.

### 12.5 Fully bare calls everywhere

Rejected because they would blur the useful distinction between reading a slot
value and calling it, especially in a one-namespace live image language.

Prompt-only simple-call sugar is acceptable.
Making it the whole language is not.

### 12.6 Significant indentation

Rejected because Frothy is meant to be used line by line in the REPL.

Indentation-sensitive syntax is pleasant in files, but it becomes awkward in a
serial prompt where reshaping previous lines is expensive or impossible.

### 12.7 Slash paths

Rejected in favor of dotted names.

Slash paths carry strong REBOL associations and can work well, but dotted names
fit the current Frothy naming model better and feel less jarring in mixed host
and hardware code.

### 12.8 Full expression-level `@name`

Deferred.

`@name` is valuable immediately for inspection and control-surface work.
General expression-level binding values are useful too, but they should not be
smuggled in accidentally through REPL sugar.

## 13. Summary

The next-stage Frothy surface should make five things obvious:

- the top level is a live image ledger
- named code is still ordinary slot rebinding
- locals are temporary and belong only here
- the REPL is a first-class programming surface, not a second-rate shell
- one-line and multiline entry must both feel natural

This proposal achieves that by:

- preferring `is` for binding
- using `to` for named code
- keeping `here` for locals
- using `set ... to ...` for mutation
- using brackets for blocks and `;` for one-line separation
- using `:` for the core call surface
- adding narrow prompt-only call sugar where it genuinely helps
- using `show @name` and related commands for image inspection

That is a much stronger change in language feeling without demanding a
fundamental runtime rewrite.
