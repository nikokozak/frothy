# Frothy Language Specification

Status: Accepted working spec
Version: 0.1
Date: 2026-04-09

## 1. Purpose

Frothy is a small live language for programmable devices.

It is the accepted fork direction from Froth that keeps the strongest properties
of the original project:

- live interaction on the device,
- stable top-level identity,
- coherent redefinition,
- explicit save / restore / wipe,
- simple foreign bindings,
- transparent inspection of the running image,
- and a practical host-to-ESP32 development loop.

It deliberately does not preserve Froth's user-visible stack model.

Frothy 0.1 is designed to be small enough for one careful developer to
understand completely. Its expressive power should come from a few orthogonal
rules that compose cleanly.

## 2. Core Idea

Everything user-facing in Frothy is either:

- a value,
- or a place that holds one.

Frothy should be understood through three laws.

### 2.1 Stable named places

The top-level image is a set of stable named slots.

A top-level name always refers to the same slot identity.
What changes is the value currently stored in that slot.

### 2.2 Small set of actions

Code only does a few things:

- read a value,
- bind a local name,
- set a place,
- call code,
- choose with `if`,
- repeat with `while`.

Everything else is built from those actions.

### 2.3 Persistent image, not persistent execution

`save()` remembers the current top-level overlay image.
`restore()` rebuilds that overlay image.
`wipe()` clears it.

Running evaluation, local scopes, and machine pointers do not persist.

This is the intended Frothy "aha":
the language is a live board of stable named places that you can inspect,
redefine, save, and reconnect.

## 3. Core Semantic Model

### 3.1 Scope

Frothy has one namespace of values and two scopes:

- the top-level image,
- lexical local scopes created by blocks.

The top-level image is a set of stable named slots.
A top-level evaluation step creates or rebinds the value stored in a slot.
Rebinding changes the slot's current value, not its identity.

There is no separate function namespace.
`Code` is a value like any other.

### 3.2 Values

Frothy Core has these language-visible value classes:

- `Int`
- `Bool`
- `Nil`
- `Text`
- `Cells`
- `Code`

`Cells` is the only collection value in `v0.1`.
It is a fixed-size mutable indexed store.

`Code` is callable.

The language does not expose:

- raw pointers,
- general foreign handles,
- interrupt tokens,
- or implementation-private control objects.

### 3.3 Integers

Integers are signed machine-range integers.
For the Frothy/32 reference profile, they are 30-bit signed immediates carried
in a 32-bit value word.

Arithmetic wraps with two's-complement semantics.
Division and remainder by zero are runtime errors.

### 3.4 Booleans

`true` and `false` are the only booleans.

Conditions in `if` and `while` must evaluate to `Bool`.
Frothy does not use general truthiness.

### 3.5 Nil

`nil` is a distinguished empty value.

`nil` is the result of:

- an empty block,
- a `while` expression,
- an `if` without `else` whose condition is false,
- or a foreign function that returns no meaningful value.

### 3.6 Text

Text values are immutable byte strings.
Unicode semantics are out of scope for `v0.1`.

### 3.7 Code

`fn(...) { ... }` yields `Code`.

A function may use:

- its parameters,
- names it binds inside its own body,
- and top-level names.

It does not capture outer locals in `v0.1`.

### 3.8 Cells

`cells(n)` creates the only collection value in `v0.1`:
a fixed-size mutable indexed store.

Elements begin as `nil` and may hold only:

- `Int`
- `Bool`
- `Nil`
- `Text`

## 4. Names

### 4.1 Name syntax

A name is an ASCII identifier.
`.` is permitted inside names.
In `v0.1`, `.` has no built-in namespace semantics.

Examples:

- `blink`
- `LED_BUILTIN`
- `gpio.write`
- `board.led.pin`

User-defined and base-image names follow the same rule.
Names containing dots are expected to be most common in the base image,
especially for foreign bindings and board-provided values.

### 4.2 Lookup

Value lookup proceeds in this order:

1. current local scope,
2. enclosing local scopes,
3. top-level slots.

### 4.3 Binding

Inside a block, `name = expr` creates a local in the current lexical scope.

If that scope already has a binding for `name`, this is an error.
Use `set name = expr` to mutate an existing local.

A nested block may shadow an outer local or a top-level slot by creating a new
local binding with the same name.

At top level, `name = expr` creates or rebinds a stable top-level slot.

### 4.4 Mutation

`set place = expr` mutates an existing place.

A place is either:

- a name,
- an indexed cells element.

If no existing place is found, `set` is a runtime error.

## 5. Surface Syntax

### 5.1 Top-level forms

A top-level form is either:

- `name = expr`
- `set place = expr`
- a REPL expression

`name = expr` creates or rebinds the stable top-level slot `name`.
`set place = expr` mutates an existing top-level place.

Examples:

```txt
unit = 120
frame = cells(64)

pulse = fn(pin, duration) {
  gpio.write(pin, 1)
  ms(duration)
  gpio.write(pin, 0)
}
```

### 5.2 Blocks

A block is:

```txt
{ item* }
```

A block introduces a lexical scope.

A block item is one of:

- `name = expr`
- `set place = expr`
- `expr`

A block yields the value of its last expression.
If a block has no final expression, it yields `nil`.

### 5.3 Expressions

Expressions are:

- integer literal
- text literal
- `true`, `false`, `nil`
- name reference
- grouped expression: `(expr)`
- function literal: `fn(param1, param2, ...) { block }`
- cells constructor: `cells(positive_integer_literal)`
- index read: `expr[index]`
- call: `expr(arg1, arg2, ...)`
- conditional expression: `if expr { block }`
- conditional expression: `if expr { block } else { block }`
- loop expression: `while expr { block }`
- unary operator: `-expr`, `not expr`
- binary operator: one of `* / % + - < <= > >= == !=`

All binary operators have equal precedence and associate left to right.
Parentheses are the only grouping mechanism.

If `if` has no `else` branch and its condition is false, it yields `nil`.

### 5.4 Informal grammar

```txt
top        := top_assign | top_set | expr
top_assign := NAME "=" expr
top_set    := "set" place "=" expr

block      := "{" item* "}"
item       := NAME "=" expr
           | "set" place "=" expr
           | expr

place      := NAME
           | expr "[" expr "]"

expr       := literal
           | NAME
           | "(" expr ")"
           | "fn" "(" params? ")" block
           | "cells" "(" INT_LITERAL ")"
           | expr "[" expr "]"
           | expr "(" args? ")"
           | "if" expr block
           | "if" expr block "else" block
           | "while" expr block
           | unary expr
           | expr binary expr

params     := NAME ("," NAME)*
args       := expr ("," expr)*
```

## 6. Evaluation Rules

### 6.1 Evaluation order

Evaluation is left to right.

Specifically:

- call arguments left to right,
- array/index base before index,
- assignment target place resolution before store,
- binary operator left operand before right operand.

### 6.2 Blocks

Evaluating a block:

1. creates a lexical scope,
2. evaluates items in order,
3. returns the value of the last expression item,
4. or `nil` if there is no final expression item.

### 6.3 Calls

Applying an expression requires that the resulting value is `Code`.
Calling any other value is a runtime error.

Examples:

```txt
blink(2)
gpio.write(pin, 1)
action()
(chooseAction())()
```

This preserves the one-namespace model:
code can be read, passed around, rebound, and called as an ordinary value.

Function arity is fixed and exact.
Calling a function with the wrong number of arguments is a runtime error.

### 6.4 Conditionals

`if`:

1. evaluates its condition,
2. requires `Bool`,
3. evaluates exactly one branch,
4. yields the chosen branch's value,
5. or `nil` if no branch is chosen because no `else` is present.

### 6.5 Loops

`while`:

1. evaluates its condition,
2. requires `Bool`,
3. if true, evaluates the body and repeats,
4. if false, yields `nil`.

The body value is discarded.

### 6.6 Non-capturing rule

Implementations must perform lexical resolution for `fn` bodies and reject
illegal outer-local capture before runtime evaluation.

In `v0.1`, this is a required name-resolution pass rather than an informal
reader check.

## 7. Image

### 7.1 Base and overlay

The running image has two conceptual layers:

- base image, rebuilt on every boot,
- overlay image, created by top-level evaluation after boot.

The base image includes:

- built-ins,
- foreign bindings,
- standard library,
- board library.

The overlay image includes:

- top-level values,
- top-level code,
- top-level cells stores and their contents.

### 7.2 Rebinding

Redefinition changes the value stored in a stable top-level slot.
Callers that resolve through that slot observe the new definition.

This is a core Frothy property.

### 7.3 Rebinding base names

If a top-level write targets a base-image name, the effect in `v0.1` is:

- the stable slot identity is preserved,
- the current live value becomes the overlay value,
- `wipe()` and failed `restore()` return that slot to the boot-rebuilt base
  value.

In other words, base values can be shadowed by overlay rebinding, but the base
image remains the recovery source of truth.

### 7.4 Cells ownership

In `v0.1`, `cells(n)` is only valid in a top-level rebinding form.

This means cells stores are owned by top-level slots in the overlay image.
They are not a general-purpose local allocation mechanism.

### 7.5 Save

`save()` snapshots the overlay image only.

The persisted overlay image is:

- the set of overlay top-level slots,
- plus persistable code, text, cells descriptors, and cells payload directly
  owned by those slot values.

This walk is deliberately shallow in `v0.1`.

### 7.6 Restore

`restore()` replaces the current live overlay image with the persisted overlay
image.
If restore fails, the system must remain in a usable base state.

During restore, persisted references to top-level names are remapped onto the
rebuilt base image by symbol identity.

### 7.7 Wipe

`wipe()` clears both:

- live overlay state,
- and stored overlay state.

After `wipe()`, the running image is base-only again.

### 7.8 Boot

If a top-level name `boot` holds `Code` after restore, the runtime executes it
under top-level recovery before entering the prompt.

## 8. Interactive Profile

### 8.1 Required operations

The interactive profile requires these built-in entry points:

- `save()`
- `restore()`
- `wipe()`
- `words()`
- `see("name")`
- `core("name")`
- `slotInfo("name")`

These may be implemented as base-image `Code` values.

### 8.2 REPL

The REPL:

- reads top-level forms,
- accumulates incomplete multiline input,
- evaluates complete forms,
- prints the result of top-level expression evaluation,
- and keeps the prompt alive on recoverable errors.

Definitions need not print a result.

Incomplete input includes unclosed:

- `(`
- `{`
- string literal

### 8.3 Interrupt

Ctrl-C interrupts:

- the current running evaluation,
- and pending multiline input.

After interruption, the prompt remains usable.

The reference interactive profile must check interruption at safe points during
evaluation, including loop back-edges and ordinary IR dispatch.
Interrupted top-level evaluation must roll back to a usable prompt state.

### 8.4 Inspection

`words()` lists top-level names.

`see("name")` is required at least for overlay `Code` slots in `v0.1`.
It must be derived from canonical IR.

`core("name")` may be implementation-defined debug output in `v0.1`, but should
ideally present canonical IR or a normalized IR dump.

`slotInfo("name")` displays binding metadata such as:

- origin (base or overlay),
- current value kind,
- persistability,
- user-defined or foreign.

Implementations may additionally expose exact stored source text, but exact
source is optional metadata and not part of the `v0.1` contract.

Platforms may additionally expose a reset command outside the Frothy language
contract.

## 9. FFI

Foreign bindings are top-level `Code` values in the base image.
Native runtime state does not persist.

The user-facing call model is ordinary Frothy call syntax:

```txt
gpio.write(2, 1)
ms(250)
adc.read(A0)
```

In `v0.1`, the implementation may reuse Froth's existing stack-oriented FFI
registration and native entrypoints internally.
A cleaner value-oriented native ABI is a post-`v0.1` cleanup goal, not a
required first milestone.

## 10. Errors

Frothy requires recoverable runtime errors for at least:

- wrong arity,
- undefined name,
- invalid set target,
- invalid call target,
- type mismatch,
- division by zero,
- cells element kind rejection,
- cells bounds,
- persistence of a non-persistable value.

Reader and parser errors should be recoverable to the prompt when possible.

## 11. Minimal Example

```txt
unit = 120
frame = cells(16)

pulse = fn(pin, n) {
  gpio.write(pin, 1)
  ms(n)
  gpio.write(pin, 0)
}

blink = fn(pin) {
  i = 0
  gpio.mode(pin, 1)
  while i < 3 {
    pulse(pin, unit)
    ms(unit)
    set i = i + 1
  }
}

boot = fn() {
  blink(LED_BUILTIN)
}
```

This sits in the intended expressive center of Frothy `v0.1`.

## Appendix A. Frothy/32 Reference Profile

This appendix is non-normative for the language itself, but normative for the
first intended implementation profile.

### A.1 Target floor

Frothy/32 targets:

- host-native development,
- ESP32-class 32-bit MCUs.

8-bit and 16-bit targets are out of scope for `v0.1`.

### A.2 Runtime value word

The runtime value word is 32 bits.

The reference encoding uses 2 low tag bits:

- `00`: signed 30-bit integer
- `01`: special immediate (`nil`, `false`, `true`, reserved)
- `10`: slot ID
- `11`: object ID

### A.3 Object kinds

The reference object table includes at least:

- `TEXT`
- `CELLS_DESC`
- `CODE`
- `NATIVE_ADDR`
- `FOREIGN_HANDLE`

### A.4 Canonical code representation

The canonical persisted representation of Frothy code is a tree-shaped semantic
IR.
There is no required bytecode layer in `v0.1`.

The minimum canonical node set for `v0.1` is:

- `LIT`
- `READ_LOCAL`
- `WRITE_LOCAL`
- `READ_SLOT`
- `WRITE_SLOT`
- `READ_INDEX`
- `WRITE_INDEX`
- `FN`
- `CALL`
- `IF`
- `WHILE`
- `SEQ`

Evaluation order in canonical IR is left to right.

### A.5 Code objects

A persisted `Code` object contains at minimum:

- arity,
- local count,
- constant table,
- canonical IR body.

Canonical IR is the persisted source of truth.
Exact source metadata, if any, is auxiliary only.

### A.6 Snapshot model

The persisted image remains pointer-free.
At minimum it contains:

1. header
2. symbol table
3. object table
4. top-level binding table
5. persistent cells payload

Overlay bindings must be distinguished from base bindings, for example through a
per-slot overlay flag or an equivalent stable mechanism.

Snapshot compatibility must be versioned explicitly.
At minimum, compatibility checks must cover:

- snapshot format version,
- canonical IR schema version,
- implementation ABI compatibility.

## Appendix B. Reused Froth Substrate

Frothy is not a new machine from scratch.
It is a new language layer over much of Froth's existing substrate.

The key reuse points are:

- stable slot table for top-level identity,
- heap allocator for names, text, and code objects,
- CellSpace as likely backing store for cells payload,
- boot and safe-boot flow,
- snapshot storage plumbing,
- interrupt plumbing,
- board/platform split,
- FFI registration and board binding structure.

The key things that become internal-only are:

- data stack,
- return stack,
- call stack,
- and stack-centric primitives.

In Frothy, those remain implementation machinery rather than user-visible
concepts.

## Appendix C. Growth Path

Frothy should grow by preserving the same semantic center rather than replacing
it.

The most plausible future layers are:

### C.1 Better data

- richer text utilities
- fixed-layout records
- namespaces or module images built from slots

### C.2 Better abstraction

- explicit environment objects
- controlled binding helpers instead of hidden closures

### C.3 Better performance

- bytecode as a cache, not the source of truth
- optional compiler passes on canonical IR
- more aggressive host tooling

### C.4 Better tooling

- exact-source host sidecars
- project-aware inspection
- richer `see` / `core`
- on-device tests and project packaging

Each layer must preserve:

- stable top-level identity,
- transparent inspection,
- explicit persistence boundary,
- no hidden allocation in hot execution paths.
