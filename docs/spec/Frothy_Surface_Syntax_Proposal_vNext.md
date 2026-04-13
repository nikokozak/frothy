# Frothy Surface Syntax Proposal

Status: Draft proposal
Version: vNext
Date: 2026-04-12
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
- pretty-printing may recover source-like forms
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

This section describes the proposed next-stage surface precisely.

### 5.1 Top-level forms

At top level, the primary forms should be:

```txt
name = expr
name(args) = expr
name(args) { block }
boot { block }
```

Meaning:

- `name = expr`
  creates or rebinds the stable top-level slot `name`
- `name(args) = expr`
  creates or rebinds the stable top-level slot `name` with a `Code` value
- `name(args) { block }`
  is the block-bodied form of the same
- `boot { block }`
  is surface sugar for binding the ordinary top-level slot `boot` to `Code`

Examples:

```txt
threshold = 42
frame = cells(3)

count() = frame[0]
enabled() = frame[1]

writeFrame() {
  set frame[0] = 7
  set frame[1] = false
  set frame[2] = "ready"
}

boot {
  gpio.mode(LED_BUILTIN, true)
}
```

Why this shape is right:

- top-level definitions stay short
- executable definitions look like named image definitions
- no extra `slot` keyword is required on every line
- the model still matches ADR-101 exactly

### 5.2 Local forms

Inside a block, the primary local binding form should be:

```txt
here name = expr
```

Example:

```txt
sumTo(limit) {
  here total = 0
  here i = 0

  while i < limit {
    set total = total + i
    set i = i + 1
  }

  total
}
```

`here` was chosen deliberately.
It communicates the actual semantic fact:
this name exists here, in this lexical region, and not in the durable image.

This is the main reason it is preferred over `let`.
`let` is conventional, but it does not teach the reader much.

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

Mutation should remain:

```txt
set place = expr
```

Examples:

```txt
set threshold = 43
set frame[0] = 9
```

Why `set` stays:

- it preserves the semantic distinction between binding and mutation
- it catches common live-edit mistakes
- it already matches the accepted `v0.1` model

Concrete safety win:

```txt
threshold = 42
set threshold = 43
```

is valid, but:

```txt
set threshhold = 43
```

should fail if `threshhold` does not already exist.

That is valuable in a live image language.

### 5.4 Anonymous code

Anonymous code remains:

```txt
fn(args) { block }
```

This matters because `Code` is still an ordinary value.

Examples:

```txt
factory() = fn() { 1 }
```

```txt
apply(action) {
  action()
}
```

The proposal does not change the current non-capturing rule.
Nested `fn` values remain legal only when they do not capture outer locals.

### 5.5 Calls stay parenthesized

Ordinary calls should remain:

```txt
name(...)
```

This proposal does not introduce prefix-style no-parens calls such as:

```txt
add 1 2
```

That is intentionally rejected.

Reasons:

- Frothy is not a prefix language
- `count` and `count()` should remain distinct
- code-as-value is clearer when calling stays explicit
- adding bare application would be a much larger language shift than the rest
  of this proposal

So:

- `count`
  means read the slot value
- `count()`
  means call that value as code

That distinction is worth keeping sharp.

### 5.6 No local executable shorthand in the first step

This proposal does not add:

```txt
here helper(x) = ...
```

or:

```txt
helper(x) = ...
```

inside ordinary blocks.

In the first step, local executable values should still be written as:

```txt
here helper = fn(x) { x + 1 }
```

That keeps the new grammar small and focused on the top-level image story.

## 6. Inspection And REPL Surface

The inspection story should improve, but cheaply.

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

### 6.2 Add bare REPL command sugar

At the prompt, the shell should additionally accept:

```txt
words
see @name
core @name
info @name
save
restore
wipe
```

Examples:

```txt
words
see @count
core @count
info @threshold
save
```

This should be implemented as REPL sugar first, not as a new full expression
grammar.

That keeps the feature cheap:

- no new IR kinds
- no new persisted value kind
- no need to settle expression-level binding reference semantics immediately

### 6.3 `@name` means binding, not value

`@name` denotes the named binding itself rather than the value produced by
reading that name.

Given:

```txt
count() = frame[0]
```

then:

- `count`
  means the current value in the slot `count`
- `count()`
  means call that value
- `@count`
  means the slot binding named `count` as an inspectable image object

This is why plain `see(count)` is not enough for binding inspection.
`count` is an expression that evaluates to a value.
Binding inspection needs a way to talk about the slot itself.

For the immediate next stage, `@name` should be supported only in REPL
inspection commands.
Making `@name` a full expression-level construct can be deferred.

### 6.4 `info` is REPL sugar over `slotInfo`

`info @name` should be command-surface sugar for the existing binding metadata
operation now surfaced as `slotInfo("name")`.

`info` is shorter and reads better at the prompt.
The underlying runtime entry point can remain the same at first.

## 7. Newline And Multiline Rules

Because Frothy is meant to be entered interactively, newline behavior must be
predictable.

### 7.1 Default rule

A newline ends the current form unless the form is clearly incomplete.

### 7.2 A form is incomplete when

Any of these are true:

- `(` is unclosed
- `[` is unclosed
- `{` is unclosed
- a string literal is unclosed
- the line ends with `=`
- the line ends with `,`
- the line ends with a binary operator
- the parser has seen `name(args)` and is still waiting for `=` or `{`
- the parser has seen `boot` and is still waiting for `{`

### 7.3 Long expression bodies

If an expression-bodied definition is too long for one line, it should use
parentheses:

```txt
average() = (
  sensor.a() +
  sensor.b() +
  sensor.c()
) / 3
```

That is safe and predictable at a terminal.

### 7.4 Block bodies for real logic

Anything with real control flow or several intermediate values should switch to
the block-bodied form:

```txt
average() {
  here a = sensor.a()
  here b = sensor.b()
  here c = sensor.c()
  (a + b + c) / 3
}
```

This keeps the language free of indentation-sensitive rules while still making
multiline entry pleasant.

### 7.5 Prompt modes

The REPL should visibly distinguish complete-entry mode from continuation mode:

```txt
frothy> count() = frame[0]
frothy> blink(period) {
...>   led.on()
...>   ms(period)
...>   led.off()
...>   ms(period)
...> }
```

The continuation prompt should appear only while the parser still expects the
rest of the form.

## 8. Worked Examples

### 8.1 Small stateful sketch

```txt
frame = cells(3)

count() = frame[0]
enabled() = frame[1]
label() = frame[2]

writeFrame() {
  set frame[0] = 7
  set frame[1] = false
  set frame[2] = "ready"
}
```

### 8.2 Loop with local state

```txt
sumTo(limit) {
  here total = 0
  here i = 0

  while i < limit {
    set total = total + i
    set i = i + 1
  }

  total
}
```

### 8.3 Hardware-facing style

```txt
led.pin = LED_BUILTIN

led.on() = gpio.write(led.pin, true)
led.off() = gpio.write(led.pin, false)

blink(period) {
  led.on()
  ms(period)
  led.off()
  ms(period)
}

boot {
  gpio.mode(led.pin, true)
}
```

### 8.4 REPL session shape

```txt
frothy> threshold = 42
frothy> frame = cells(1)
frothy> count() = frame[0]
frothy> set frame[0] = 9
frothy> count()
9
frothy> see @count
count() = frame[0]
frothy> save
```

## 9. Desugaring To The Accepted `v0.1` Core

This proposal is designed to lower onto the current semantic core with minimal
runtime change.

### 9.1 Top-level value definition

```txt
threshold = 42
```

desugars directly to the current top-level slot binding:

```txt
threshold = 42
```

No semantic change.

### 9.2 Executable slot with expression body

```txt
count() = frame[0]
```

desugars to:

```txt
count = fn() { frame[0] }
```

### 9.3 Executable slot with block body

```txt
writeFrame() {
  set frame[0] = 7
}
```

desugars to:

```txt
writeFrame = fn() {
  set frame[0] = 7
}
```

### 9.4 Local binding

```txt
here total = 0
```

desugars to the current block-local binding form:

```txt
total = 0
```

Again, no semantic change.

### 9.5 Boot block

```txt
boot {
  gpio.mode(LED_BUILTIN, true)
}
```

desugars to:

```txt
boot = fn() {
  gpio.mode(LED_BUILTIN, true)
}
```

## 10. Implementation Considerations

The proposal is primarily parser, shell, and pretty-printing work.
That is why it is realistic.

### 10.1 Lexer and parser

Relevant current files:

- [frothy_parser.c](/Users/niko/Developer/Frothy/src/frothy_parser.c)
- [frothy_ir.h](/Users/niko/Developer/Frothy/src/frothy_ir.h)

The main parser work is:

- add `here` as a reserved keyword
- parse `name(args) = expr` at top level
- parse `name(args) { block }` at top level
- parse `boot { block }` as top-level sugar

Important implementation note:
`boot` does not need to become a globally reserved keyword.
The parser can special-case the top-level form `boot { ... }`.
That avoids breaking ordinary name syntax more than necessary.

Likewise, `name(args) = expr` and `name(args) { block }` should be recognized
only in the top-level parser path.
Elsewhere, `name(args)` remains an ordinary call expression.

Expected cost:

- `here`: low
- top-level executable-slot forms: low to medium
- `boot { ... }`: low

### 10.2 REPL command parsing

Relevant current files:

- [frothy_shell.c](/Users/niko/Developer/Frothy/src/frothy_shell.c)
- [frothy_inspect.c](/Users/niko/Developer/Frothy/src/frothy_inspect.c)

Bare commands such as:

- `words`
- `see @name`
- `core @name`
- `info @name`
- `save`
- `restore`
- `wipe`

should be implemented in the REPL front end first.

That means:

- they are shell-level sugar
- the accepted `v0.1` callable forms remain valid
- `@name` can remain command-only initially

Expected cost:

- bare command recognition: low
- `@name` command-surface parsing: low
- full expression-level `@name`: deferred

### 10.3 IR and evaluator

The proposal should not require new semantic IR nodes.

All major forms lower to what already exists:

- top-level definitions -> `WRITE_SLOT`
- locals -> `WRITE_LOCAL`
- mutation -> existing place-write handling
- executable slot forms -> `FN` plus top-level slot write
- boot block -> ordinary top-level `Code`

So the evaluator in
[frothy_eval.c](/Users/niko/Developer/Frothy/src/frothy_eval.c)
should not need new execution machinery.

This is one of the main reasons the proposal is worth doing.

### 10.4 Persistence

The proposal does not change persistence behavior.

The following remain true:

- top-level slots are the persisted unit
- locals do not persist
- `boot` is just an ordinary slot holding `Code`
- canonical IR remains the persisted code truth

So the snapshot design accepted in ADR-106 remains intact.

### 10.5 Pretty-printing and `see`

To make this surface feel real, `see` should eventually prefer the new
source-like forms where possible:

- `count() = frame[0]`
- `writeFrame() { ... }`
- `boot { ... }`

This does not require exact-source persistence in snapshots.
It can begin as:

- parser-attached source-form metadata
- REPL-side sidecar metadata
- or a surface-aware re-render from canonical IR where the shape is simple

Exact-source persistence is still optional.
Canonical IR remains normative.

### 10.6 Multiline implementation

Relevant current files:

- [frothy_shell.c](/Users/niko/Developer/Frothy/src/frothy_shell.c)

The shell already tracks incomplete grouped and quoted input.
To support this proposal well, multiline continuation should also account for:

- trailing `=`
- trailing binary operators
- top-level `name(args)` waiting for `=` or `{`
- `boot` waiting for `{`

This is still shell work, not evaluator work.

### 10.7 Testing impact

The minimum new proof surface should include:

- parser coverage for `here`
- parser coverage for executable-slot top-level forms
- parser coverage for `boot { ... }`
- REPL smoke for bare commands
- REPL smoke for continuation after `=`
- `see` output checks for the new rendered forms where implemented

The existing accepted forms should remain covered during the transition.

## 11. Migration And Rollout

A low-risk rollout should proceed in this order:

1. add `here`
2. add top-level `name(args) = expr`
3. add top-level `name(args) { block }`
4. add `boot { block }`
5. add bare REPL commands
6. add command-surface `@name`
7. improve `see` rendering

Roadmap position:
this rollout should begin immediately after the urgent transport slice, not as
a late polish pass and not after broader workspace work. It is mostly parser
and shell work, it makes the public model read less like inherited Froth, and
it keeps canonical IR plus the evaluator unchanged.

During rollout, the current `v0.1` forms should remain accepted:

- top-level `name = expr`
- block-local `name = expr`
- `name = fn(args) { ... }`
- `words()`
- `see("name")`
- `core("name")`
- `slotInfo("name")`

That keeps tests, transcripts, and proof scripts working while the surface is
improved incrementally.

## 12. Alternatives Rejected For This Step

These ideas were considered and intentionally left out of this proposal.

### 12.1 `slot` at top level

Rejected for now because it adds noise to the place where the meaning is
already obvious.
The top level already is the image.

### 12.2 `:=` and `<-`

Rejected for now because they do not buy enough over:

- bare top-level `=`
- `here`
- `set`

The semantic distinction matters.
The extra punctuation does not.

### 12.3 `let`

Rejected in favor of `here`.
`let` is familiar, but it does not describe the scope fact that matters.

### 12.4 No-parens calls

Rejected because they would shift Frothy toward a different calling model and
blur the useful distinction between reading a slot value and calling it.

### 12.5 `module`

Deferred.
Dotted names already organize a lot of real hardware code, and a true module
surface can come later if it proves necessary.

### 12.6 `volatile` or `session`

Deferred.
These state classes are easy to make confusing in a persistent live image
model.

### 12.7 Types

Deferred.
They may be valuable later, but they are not part of the minimal surface
cleanup.

## 13. Summary

The next-stage Frothy surface should make four things obvious:

- the top level is the live image
- a named executable definition is still just a slot
- locals are temporary and belong only here
- the REPL is a first-class way to inspect and patch the image

This proposal achieves that by:

- keeping top-level definitions compact
- using `here` for locals
- keeping `set` for mutation
- keeping calls parenthesized
- adding cheap REPL sugar for inspection
- and using `@name` to talk about bindings without forcing a new runtime value

That is a meaningful user-facing improvement with modest architectural cost.
