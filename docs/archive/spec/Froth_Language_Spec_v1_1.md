# Froth Language Specification

**Status:** Candidate (pre-freeze)  
**Version:** 1.1 (2026-03-05)
**Scope:** This document specifies the *core* semantics of Froth (FROTH-Core) and a small set of optional, strictly layered profiles intended to remain stable for decades.  
**Non-goals:** Garbage collection; implicit allocation in hot paths; a large mandatory standard library.

---

## Design intent

Froth is a tiny, embedded-first, concatenative language intended to:

- Run interactively **on-device** (REPL-first), with deterministic memory use.
- Scale from small microcontrollers (AVR/ATtiny-class) to larger ones (RP2040, ESP32-class).
- Preserve the essential Forth virtues (small image, direct hardware access, incremental development), while fixing:
  - **coherent redefinition** (word identity is stable; implementations can change safely),
  - **recoverable errors** (structured unwinding with stack restoration),
  - and enabling modern tooling via a canonical stack-rewrite primitive (**`perm`**) plus optional *named stack frames*.

Froth programs are sequences of *words* executed left-to-right. Each word transforms the **data stack (DS)**, so juxtaposition corresponds to composition (*concatenative* semantics).

Froth additionally treats program fragments as first-class data via **quotations** (`[ ... ]`).

---

### Froth’s mental model on an embedded device (informative)

It helps to think of Froth as a **small virtual machine** that sits *inside* your firmware image and is able to
call outward to hardware libraries (HAL/SDK), and also accept callbacks from them.

A typical embedded stack looks like this:

```
+--------------------------------------------------------------+
| Your Application (mostly Froth code, some C for hot paths)    |
+---------------------------+----------------------------------+
| Froth VM (interpreter + slots + heaps + REPL + snapshots)     |
+---------------------------+----------------------------------+
| Optional RTOS / scheduler (e.g., FreeRTOS)                    |
+---------------------------+----------------------------------+
| Vendor HAL / SDK (ESP-IDF, Pico SDK, STM32 HAL, ...)          |
+---------------------------+----------------------------------+
| Bare metal: registers, interrupts, DMA, peripherals           |
+--------------------------------------------------------------+
```

Key points:

- **Froth is not “above” your firmware** — it is *part of* the firmware. It can be compiled into a bare-metal
  application, or run as one task in an RTOS.
- **Froth does not replace your vendor SDK.** Instead, it provides a REPL-first systems language that can call into
  the SDK via FFI, and can host glue logic and control planes.
- **Froth is comfortable at the metal.** If you want to poke registers directly, you can: the optional FROTH-Addr profile provides full-width native addresses with width-specific memory access (`@8`/`@16`/`@32`, `!8`/`!16`/`!32`), or you can bind register-level accessors as FFI primitives. If you want to stay at the HAL level, you can do that too.

### Why the primitives are named this way (informative)

Froth’s core word names aim to be:

- **short**, so they read well in stack code;
- **literal**, so newcomers can guess them;
- **stable**, so they can be used as “IR markers” for tooling.

A few important ones:

- **`perm`** — short for *permutation*. It rewires a window of the stack in one canonical operation. Tooling can
  synthesize and optimize `perm` patterns, which is why it is a core primitive.
- **`pat`** — “pattern validate/compile.” The surface literal `p[ ... ]` is human-friendly; `pat` turns it into
  a compact, validated PatternRef that `perm` can apply quickly.
- **`choose`** — selects one of two values without executing it. This is intentionally separate from `call`, so
  the language stays orthogonal and “combinator-friendly.” (`if` is definable as `choose call`.)
- **`call`** — execute a callable (a quotation or a slot reference). This unifies “call a word” and “call a quote.”
- **`catch`/`throw`** — adopted from standard Forth because they work well on embedded systems: deterministic, no GC,
  and they map cleanly to “recover and keep the REPL alive.”

---

## Conformance profiles

A *conforming Froth system* MUST implement **FROTH-Core** and **FROTH-Base**.

Additional profiles are optional and strictly layered: they must not change the meaning of correct FROTH-Core programs.

### FROTH-Core (mandatory)

FROTH-Core defines:

- the reader forms in Section 2,
- the abstract machine model in Section 3,
- execution semantics in Section 4,
- the core word set in Section 5,
- and the error/unwinding model in Section 6.

### FROTH-Base (mandatory)

FROTH-Base defines the minimal primitive vocabulary needed for a practical interactive system:

- arithmetic, comparisons, bitwise operations,
- minimal character I/O,
- and a user-visible auxiliary stack (`>r` / `r>`) used by standard combinators.

FROTH-Base is specified in Section 7.

### FROTH-Named (optional)

FROTH-Named makes the stack readable without abandoning concatenative execution:

- stack-effect declarations become *bindings* for named entry values,
- name references compile to `perm`-based shuffles with static stack-effect verification,
- values never leave the data stack; there are no locals frames and no allocation.

Specified in Section 8.

### FROTH-Checked (optional)

FROTH-Checked adds a lightweight **kind** safety net:

- a parallel *kind stack* (KS),
- **contracts** attached to slots (checked at call boundaries),
- optional user-defined kinds for handle families.

Specified in Section 9.

### FROTH-Region (optional)

FROTH-Region provides deterministic, non-GC reclamation for meta/allocation-heavy workflows:

- explicit heap watermarks (`mark` / `release`) suitable for initialization-time generation and interactive tooling.

Specified in Section 10.

**FROTH-Region-Strict (optional recommendation):** a stricter mode that makes dynamic allocation fail-fast by requiring an active region for runtime object constructors (at minimum `q.pack` and `pat`). Recommended on very small targets. See the FROTH-Region-Strict subsection.

### FROTH-String-Lite (optional)

FROTH-String-Lite adds **immutable byte strings** as first-class values, optimized for **REPL output and simple inspection**:

- The reader form `"..."` produces a **StringRef** (opaque immutable object; typically UTF-8 encoded bytes).
- Programs manipulate strings as **one stack value** (not `addr len` pairs), which reduces shuffling and makes kind checks straightforward.
- The profile is intentionally small: it focuses on **output, equality, and byte-level access**, not string building.

Specified in **FROTH-String-Lite** (see Section “FROTH-String-Lite”).

### FROTH-String (optional; extends FROTH-String-Lite)

FROTH-String extends String-Lite with **explicit allocation and interop** primitives (e.g., copying bytes from a buffer into a StringRef via `s.pack`). All allocation remains explicit and can be **fail-fast gated** by FROTH-Region-Strict.

Specified in **FROTH-String** (see Section “FROTH-String”).

**Design constraint:** Froth does not include a garbage collector. Therefore Froth’s string profiles deliberately avoid allocation-heavy convenience words such as concatenation and slicing (see rationale in the string sections).

### FROTH-REPL (optional recommendation)

FROTH-REPL standardizes a minimal, tool-friendly stack display protocol.

Specified in Section 11.

### FROTH-Stdlib (optional recommendation)

FROTH-Stdlib standardizes a small vocabulary of quotation combinators and common helpers, definable on FROTH-Core+Base.

Specified in Section 12.

### FROTH-Perf (optional)

FROTH-Perf permits additional callable representations (ITC/DTC/native) and inline caches without changing FROTH-Core semantics.

Specified informally in Section 13.

### FROTH-Addr (optional)

FROTH-Addr provides full-width native machine addresses as a first-class value type, enabling direct hardware register access without compromising the compact tagged-value core. NativeAddr values are non-persistable by construction, ensuring snapshot safety.

Specified in Section 14.

---

## Reader (lexical structure and forms)

### Tokens and whitespace

- Tokens are separated by ASCII whitespace (space, tab, CR, LF).
- Implementations MAY support comments. Recommended:
  - Line comment: `\` to end-of-line
  - Paren comment: `( ... )` (nesting not required)

### Atomic tokens

Froth MUST support:

1. **Integer literals**: base-10 signed integers (at minimum).  
   Implementations MAY support other bases, but MUST document them.

2. **Identifiers**: sequences of non-whitespace characters excluding delimiters `[` `]` and the quote prefix `'`.  
   Identifiers are resolved to **slots** (Section 3.4).

3. **Quoted identifier**: `'name`
4. **String literal**: `"..."` (FROTH-String)  
   Produces a **StringRef** value on DS. The literal is UTF-8 encoded and may include a minimal escape set:
   - `\"` for a quote
   - `\\` for a backslash
   - `\n` for newline (LF)
   - `\t` for horizontal tab
   - `\r` for carriage return (CR)

   **Note:** Froth’s string operations are byte-oriented. Unicode semantics (code points, normalization, grapheme clusters) are outside this profile’s scope.  
   Produces a **SlotRef** value on DS without calling it.

### String literal: `"..."` (reserved)

Froth’s reader **reserves** the double-quote delimiter `"` for string literals. This avoids future compatibility breaks even on builds that do not enable string support.

A string literal begins at `"` and ends at the next unescaped `"`.

Supported escape sequences (and only these):

- `\"` → byte `0x22`
- `\\` → byte `0x5C`
- `\n` → byte `0x0A`
- `\t` → byte `0x09`
- `\r` → byte `0x0D`

Any other backslash escape MUST raise a reader error.

**Completeness:** if the reader reaches end-of-input (line or frame) before finding the terminating `"`, the expression is **incomplete** and the REPL MUST continue accumulation (see FROTH-Interactive).

**Semantics:**

- If `FROTH-String-Lite` is enabled, `"..."` produces a **StringRef** value on DS.
- If `FROTH-String-Lite` is not enabled, encountering `"` MUST raise `ERR.FEATURE` (feature: STRING; fail-fast), rather than treating it as part of an identifier.

**Inside quotations:** string literals are stored as literal StringRef tokens (like QuoteRef literals). Executing the quotation pushes the same StringRef value (i.e., the string literal is allocated once at definition/load time, not on each execution).

### Quotation form: `[ ... ]`

`[ ... ]` reads a list of tokens and constructs a **QuoteRef** (immutable sequence of quote tokens).

Inside `[ ... ]`, tokens are *stored*, not executed, with the following mapping:

- Integer literal → stored as a literal token that pushes that integer at runtime.
- Nested quotation `[ ... ]` → stored as a literal QuoteRef token (pushes the QuoteRef).
- `'name` inside a quotation → stored as a literal SlotRef token (pushes SlotRef).
- Identifier `name` inside a quotation → stored as a *call token* containing the SlotRef for `name`.

Outside quotations (at top-level), identifiers are executed immediately (Section 4).

### Pattern literal: `p[ ... ]`

`p[ ... ]` reads a list of pattern elements and produces a **PatternRef** (validated, compact pattern object).
If any element is invalid, reading `p[ ... ]` MUST signal `ERR.PATTERN` (Section 6).

Pattern elements may be:

- **Letters**: `a`, `b`, `c`, `d`, ... where `a` = index 0 (TOS), `b` = index 1 (next below TOS), etc.
- **Non-negative integers**: `0` = TOS, `1` = next below TOS, etc.

Letters and integers may be freely mixed within a single pattern.

**Reading direction (TOS-right):** the pattern reads left-to-right as bottom-to-top of the output stack. The **rightmost** element becomes the new TOS. This matches how Froth displays stacks everywhere else in the language (REPL output, stack comments, documentation).

Pattern semantics are defined by `perm` (Section 5.11).

### Optional sugar: `: name ... ;`

FROTH-Core does not require `:` and `;`. If provided, the sugar MUST desugar exactly to:

```froth
' name [ ...body... ] def
```

and MUST NOT introduce hidden compiler state beyond quotation collection.

FROTH-Named (Section 8) extends this sugar with binding stack-effect declarations.

> **Informative implementation note**
>
> A staged implementation may keep plain `:` as the only definition surface,
> ingest an immediately following `( ... -- ... )` header to record arity
> metadata first, and only later enable full named-body lowering for signed
> definitions that actually reference bound input names. This does not change
> the language surface; it is an implementation staging choice.

---

## Abstract machine model

### Values

A Froth value is one of:

- **Number**: a signed integer whose range is implementation-defined. Implementations typically use a cell width of 16, 32, or 64 bits; the usable integer range MAY be smaller than the full cell width if the implementation reserves bits for internal tagging or other purposes. For full-width machine addresses and hardware handles, see the optional FROTH-Addr profile.
- **Object reference**: an opaque reference to an immutable heap object. FROTH-Core requires:
  - QuoteRef
  - SlotRef
  - PatternRef

FROTH-String adds:

- StringRef (optional)

FROTH-Checked adds:

- ContractRef (optional)

Implementations MUST be able to distinguish these classes for core operations (`call`, `perm`, etc.).

### Stacks

Froth has:

- **Data stack (DS)**: holds Values.
- **Return stack (RS)**: a user-visible auxiliary stack holding Values (defined by FROTH-Base, `>r`/`r>`).
- **Control stack (CS)**: internal call/handler stack used to execute nested quotations and implement `catch`/`throw`.

Only DS and RS are visible to programs. CS is not directly accessible.

Stack size limits and overflow behavior are implementation-defined, but under/overflow MUST be detected and signaled as `ERR.STACK` / `ERR.RSTACK`.

### Concurrency note (normative)

FROTH-Core assumes **single-threaded execution**. Concurrency, interrupt handling, and multi-core scheduling are outside FROTH-Core scope.

However, an implementation MUST ensure that updating `slot.impl` via `def` is **atomic with respect to readers** (e.g., a single machine-word pointer store). Readers MUST never observe a partially updated implementation pointer.

### Slots (word identity) and the slot table

An identifier resolves to a **slot** with stable identity, represented by a SlotRef value.

Each slot has:

- `slot.name` (debug/printing only)
- `slot.impl` (the current value or implementation; any Froth value, see 3.5 and 4.2)
- `slot.version` (non-negative integer; used by FROTH-Perf)
- optional `slot.contract` (FROTH-Checked)
- optional `slot.in_arity`, `slot.out_arity` (stack-effect metadata; used by FROTH-Named and tooling)

**Redefinition** updates `slot.impl` (and SHOULD increment `slot.version`).

### Callables

A **callable** is an object the machine can execute. FROTH-Core defines two callable classes:

1. **QuoteRef**: executed by interpreting its stored tokens (Section 4.3)
2. **Primitive**: native callable bound in `slot.impl` (implementation-defined)

FROTH-Perf MAY introduce additional callable representations (threaded code objects, native stubs). These must obey the same visible stack semantics.

### Heap objects and allocation model (no garbage collection)

Froth allocates immutable objects (QuoteRef, PatternRef, ContractRef) in a **linear dictionary heap**:

- Allocation is append-only.
- Objects are immutable.
- There is no mandatory garbage collector.

Reclamation is policy-based and outside FROTH-Core. FROTH-Region (Section 10) provides a deterministic watermark mechanism.

---

## Execution semantics

### Top-level evaluation

The evaluator reads a sequence of tokens and executes them left-to-right:

- Literal values are pushed onto DS.
- Identifiers are resolved to SlotRefs and invoked (as if by `call`), unless quoted with `'`.
- Quotation forms push QuoteRefs as values.

### Invoking a word by identifier

Executing an identifier token `name` performs:

1. Resolve `name` to SlotRef `s` (creating the slot if necessary).
2. Invoke `s` as if executing: `s call`.

### Quote execution

Executing a QuoteRef interprets its stored tokens sequentially:

- Literal token `v` → push `v` on DS.
- Call token `slot` → invoke that slot (same semantics as 4.2).

Errors during execution propagate via `throw` (Section 6).

---

## Core words (FROTH-Core)

Stack effects are written in Forth style: `( inputs -- outputs )`.  
Errors are signaled via `throw` with the error codes in Section 6.

### `call`

**Stack:** `( callee -- ... )`

If `callee` is:

- SlotRef `s`: invoke `s` (load `s.impl`; if callable, execute it; if a plain value, push it on DS).
- QuoteRef `q`: execute `q` per Section 4.3.
- Otherwise: `throw ERR.TYPE`.

If `s` has neither a primitive binding nor an impl binding: `throw ERR.UNDEF`.

> FROTH-Checked: contract checks MAY occur at slot entry (Section 9).

> **Informative example — calling a quote vs a slot**
>
> ```froth
> ' square [ dup * ] def
> 5 square
> \ -> [25]
>
> [ 2 3 + ] call
> \ -> [5]
> ```
>
> **Mental model:** `call` is the only “invoke” operation. A **slot** is a stable name that *points to* an implementation,
> and a **quotation** is an anonymous callable object. Both are invoked via `call`.

### `def`

**Stack:** `( slot value -- )`

- `slot` MUST be SlotRef or `throw ERR.TYPE`.
- `value` may be any Froth value (number, QuoteRef, SlotRef, PatternRef, StringRef, etc.).
- Set `slot.impl := value`.
- Clear any recorded arity metadata for `slot`.
- SHOULD increment `slot.version`.

If `value` is callable (QuoteRef or Primitive), invoking the slot executes it.
If `value` is a plain value (number, PatternRef, etc.), invoking the slot pushes it on DS.

> **Informative example — redefinition is coherent**
>
> ```froth
> ' tick [ 1 + ] def
> 0 tick tick
> \ -> [2]
>
> \ Hot patch:
> ' tick [ 10 + ] def
> 0 tick
> \ -> [10]
> ```
>
> Because compiled quotations refer to **SlotRefs**, redefinition updates behavior globally. Implementations may still
> cache dispatch targets for speed, but the semantic model is “stable identity, mutable implementation.”

> **Informative example — slots as mutable storage**
>
> ```froth
> ' count 0 def
> count
> \ -> [0]
>
> ' count count 1 + def
> count
> \ -> [1]
> ```
>
> Because `def` accepts any value, slots double as named mutable storage with zero heap cost.
> Raw `def` is the escape hatch. Higher-level data and callable binding words may impose stricter intent.

### `value`, `to`, `assign`, `set`

**Stack:** `( value slot -- )`

- `slot` MUST be SlotRef or `throw ERR.TYPE`.
- `value` MUST NOT be QuoteRef, or `throw ERR.TYPE`.
- Set `slot.impl := value`.
- Set `slot.in_arity := 0`, `slot.out_arity := 1`.

These are intent-specific **data binding** words. They are stricter than `def`:
their job is to bind data-producing slots, not arbitrary implementations.

Because non-QuoteRef slot impls execute by pushing themselves, the stamped
`(0 -- 1)` metadata is truthful for all accepted values, including Number,
StringRef, PatternRef, and SlotRef.

### `is`

**Stack:** `( quote slot -- )`

- `slot` MUST be SlotRef or `throw ERR.TYPE`.
- `quote` MUST be QuoteRef or `throw ERR.TYPE`.
- Set `slot.impl := quote`.
- Clear any recorded arity metadata for `slot`.

`is` is the callable-oriented sibling of the data-binding words. It binds a
quotation to a slot without inferring any stack effect from that quotation.

To rebind one named callable to another, wrap the callee explicitly:

```froth
[ foo ] 'hook is
```

### `get`

**Stack:** `( slot -- impl )`

- `slot` MUST be SlotRef or `throw ERR.TYPE`.
- If `slot` has no impl binding: `throw ERR.UNDEF`.
- Otherwise push `slot.impl`.

> **Informative note**
>
> `get` is primarily for introspection and tooling. For example, a `see` command can retrieve a slot’s current implementation,
> check whether it is a quote or a primitive, and print an appropriate representation.

### `arity!` (stack-effect metadata)

**Stack:** `( slot in out -- )`

- `slot` MUST be SlotRef or `throw ERR.TYPE`.
- `in` and `out` MUST be non-negative numbers or `throw ERR.TYPE`.
- Set `slot.in_arity := in`, `slot.out_arity := out`.

`arity!` does not change runtime execution. It exists to support FROTH-Named compilation and tooling.
Implementations do not verify that the declared metadata matches runtime behavior; named compilation treats it as trusted input.

### `choose`

**Stack:** `( cond a b -- x )`

- Pop `b`, then `a`, then `cond`.
- If `cond` is zero → push `b`, else push `a`.
- No execution is performed.

Library `if` can be defined as:

```froth
' if [ choose call ] def
```

> **Informative example — building `if`**
>
> Froth keeps selection (`choose`) separate from execution (`call`).
>
> ```froth
> ' if [ choose call ] def
>
> 1 [ 111 ] [ 222 ] if
> \ -> [111]
> 0 [ 111 ] [ 222 ] if
> \ -> [222]
> ```
>
> This separation keeps the core orthogonal and makes higher-order combinators easy to express.

### `while`

**Stack:** `( condQ bodyQ -- )`

- `condQ` and `bodyQ` MUST be QuoteRefs or `throw ERR.TYPE`.
- `while` MUST be non-recursive (it must not consume unbounded CS space).

**Disciplined stack rule (normative):**

Let `d0` be the DS depth immediately before each evaluation of `condQ`.

- After executing `condQ`, DS depth MUST be exactly `d0 + 1`. Otherwise `throw ERR.STACK`.
- `while` pops the condition value, returning DS depth to `d0`.
- If the popped condition is non-zero, `while` executes `bodyQ`. After executing `bodyQ`, DS depth MUST be exactly `d0`. Otherwise `throw ERR.STACK`.

**Enforcement and cost model (normative):**

- The semantics of `while` are defined by the disciplined stack rule above. An implementation MUST behave *as if* it checks these DS-depth conditions on every iteration and `throw`s on violation.
- Implementations MAY elide the runtime depth checks when they can prove (statically or by construction) that:
  - `condQ` always leaves exactly one additional value (net stack delta `+1`), and
  - `bodyQ` is stack-neutral (net stack delta `0`)
  for this call site.

Proof mechanisms are profile-specific. Examples include:

- **FROTH-Named compilation:** if `condQ` and `bodyQ` are literal quotations (`[ ... ]`) and all called words have known arities (`slot.in_arity/out_arity`), a compiler can infer the net deltas and omit checks without changing meaning.
- **FROTH-Perf specialization:** an implementation may specialize a loop once it has concrete QuoteRefs for `condQ` and `bodyQ`, then run the specialized form.

This design keeps FROTH-Core simple (no required global static effect system) while still permitting zero-overhead loops in optimized builds.

> **Informative example — a simple polling loop**
>
> ```froth
> \ Assume `key?` returns true when a byte is waiting, and `key` reads it.
> ' drain-uart [
>   [ key? ] [ key drop ] while
> ] def
> ```
>
> **Why `while` is a primitive:** firmware loops cannot rely on recursion. `while` gives non-recursive iteration with
> a clear stack discipline rule (condition quote is 1-output; body quote is stack-neutral).

### `times.i`

**Stack:** `( n q -- )`

- `n` MUST be a Number or `throw ERR.TYPE`.
- `q` MUST be a QuoteRef or SlotRef or `throw ERR.TYPE`.
- If `n <= 0`, execute zero times.
- Otherwise execute `q` exactly `n` times for indices `0 .. n-1`.
- Before each iteration, push the current index as a Number, then invoke `q`.

**Body discipline (normative):** `q` MUST behave as `( i -- )`. Relative to
loop entry, the body consumes the supplied index and leaves DS depth otherwise
unchanged. If the body violates this discipline, the implementation MUST signal
the loop stack-discipline error.

This design keeps indexed counted iteration explicit: the index is an ordinary
DS value, so nested indexed loops compose without hidden binding rules.

### `q.len`

**Stack:** `( q -- n )`

- `q` MUST be QuoteRef or `throw ERR.TYPE`.
- Push the number of tokens in the quotation.

> **Informative note**
>
> `q.len` and `q@` are the minimum “program as data” interface that allows self-hosted tooling (e.g., expanders/weavers)
> without requiring a garbage collector or a full list-processing library.

### `q@`

**Stack:** `( q i -- token )`

- `q` MUST be QuoteRef or `throw ERR.TYPE`.
- `i` MUST be a number or `throw ERR.TYPE`.
- If `i` is out of bounds: `throw ERR.BOUNDS`.
- Push the i-th token as a value suitable for use with `call`/execution:
  - literal tokens are returned as their literal value
  - call tokens are returned as SlotRefs

> **Informative example — printing tokens**
>
> A simple `see` can iterate over a quotation:
>
> ```froth
> \ Pseudocode sketch: print each token
> \ ( q -- )
> dup q.len 0
> [ over < ] [
>   \ i q q@ .token
> ] while
> drop drop
> ```
>
> Full token pretty-printing is implementation-specific, but the primitive access model is stable.

### `q.pack`

**Stack:** `( v0 v1 ... v(n-1) n -- q )`

- `n` MUST be a non-negative number (on top of DS), else `throw ERR.TYPE`.
- DS must contain at least `n` additional values beneath `n`, else `throw ERR.STACK`.
- Construct a QuoteRef of length `n` whose tokens are literal tokens containing the values `v0 ... v(n-1)` in that order.
- Push the new QuoteRef.

Reference algorithm:

```
pop n
allocate QuoteRef q with n tokens
for i from n-1 down to 0:
  pop v
  q.tokens[i] := literal(v)
push q
```

**Important (no-GC note):** `q.pack` allocates on the linear heap. Long-running firmware SHOULD NOT use unbounded quotation construction without FROTH-Region or a higher-level policy.

**Fail-fast option (FROTH-Region-Strict):** In strict region mode, `q.pack` MUST `throw ERR.REGION` unless there is at least one active region (an outstanding `mark` not yet released). This turns accidental heap growth into an immediate, debuggable fault on small devices.

> **Informative example — building a quotation deterministically**
>
> `q.pack` exists so you can construct quotations **without heap churn patterns** like repeated concatenation.
>
> ```froth
> \ Pack three cells into a quote:
> 10 20 30 3 q.pack
> \ -> pushes a QuoteRef representing [10 20 30]
> ```
>
> This is useful for metaprogramming tools that temporarily build code and then either `def` it or discard it under a
> region `mark/release` policy.

### `pat`

**Stack:** `( q -- pattern )`

- `q` MUST be QuoteRef or `throw ERR.TYPE`.
- All tokens in `q` MUST be non-negative integer literal cells, else `throw ERR.PATTERN`.
- Produce a compact PatternRef with those indices and push it.

The quotation uses the same TOS-right reading convention as `p[ ... ]`: the leftmost element specifies the deepest output position, the rightmost specifies the new TOS. Index 0 = TOS of the input window.

> **Informative note**
>
> `p[ ... ]` is the human-friendly way to write a pattern. `pat` exists so that an implementation can validate and compile
> patterns into a compact representation once, rather than re-validating a quotation-of-indices on every `perm`.
> Letter-based `p[...]` is preferred for hand-written code; `pat` is for programmatic pattern construction.

### `perm`

**Stack:** `( n pattern -- )`

- `n` MUST be a non-negative number.
- `pattern` MUST be a PatternRef or `throw ERR.TYPE`.

Let `k = len(pattern)` (pattern length). Let `pattern[j]` be the j-th element (0-based from the left, i.e., from the bottom of the output).

Let the top `n` stack items be labeled:

- `x0` = TOS (index 0, letter `a`)
- `x1` = next below TOS (index 1, letter `b`)
- ...
- `x(n-1)` = deepest in window (index n-1)

Requirements:

- For all `j`, `0 <= pattern[j] < n`, else `throw ERR.PATTERN`.
- DS must have at least `n` items, else `throw ERR.STACK`.

**Reading direction (TOS-right):** the pattern reads left-to-right as bottom-to-top of the output stack. After `perm`, those `n` items are replaced by `k` items:

- `pattern[0]` specifies the **deepest** output item
- `pattern[k-1]` specifies the **new TOS**
- Each output item is `x(pattern[j])`, i.e., the input item at the named index.

Equivalently: new items `y0..y(k-1)` (from bottom upward) where `yj = x(pattern[j])`.

This simultaneously performs drop/dup/reorder.

Examples (library-defined):

- `dup`  : `1 p[a a] perm`
- `swap` : `2 p[a b] perm`
- `drop` : `1 p[] perm`
- `over` : `2 p[b a b] perm`
- `rot`  : `3 p[b a c] perm`   (deepest rises to TOS)
- `-rot` : `3 p[a c b] perm`   (TOS sinks to deepest)
- `nip`  : `2 p[a] perm`

Numeric equivalents (using `pat`):

- `dup`  : `1 [0 0] pat perm`
- `swap` : `2 [0 1] pat perm`
- `drop` : `1 [] pat perm`
- `over` : `2 [1 0 1] pat perm`
- `rot`  : `3 [1 0 2] pat perm`
- `-rot` : `3 [0 2 1] pat perm`
- `nip`  : `2 [0] pat perm`

> **Informative example — common stack words in one primitive**
>
> `perm` rewires the top **n** stack items according to a pattern. The pattern is a picture of the resulting stack: leftmost = deepest output, rightmost = new TOS.
>
> ```froth
> \ dup ( x -- x x )
> ' dup  [ 1 p[a a] perm ] def
>
> \ swap ( x y -- y x )      a=y(TOS), b=x
> ' swap [ 2 p[a b] perm ] def
>
> \ over ( x y -- x y x )    a=y(TOS), b=x
> ' over [ 2 p[b a b] perm ] def
>
> \ nip ( x y -- y )          a=y(TOS), b=x
> ' nip  [ 2 p[a] perm ] def
> ```
>
> **Why this matters:** once “stack movement” is canonical (`perm`), tools can generate it, optimizers can fuse it, and
> humans only need to learn one concept instead of 20 shuffle words.

---

## Errors and recovery (FROTH-Core)

### Error values

Errors are non-zero numbers (`e != 0`). FROTH-Core reserves:

- `1`  ERR.STACK    (data stack under/overflow)
- `2`  ERR.RSTACK   (return stack under/overflow)
- `3`  ERR.UNDEF    (unbound slot.impl or undefined primitive)
- `4`  ERR.TYPE     (type/kind mismatch where required)
- `5`  ERR.BOUNDS   (index out of bounds)
- `6`  ERR.PATTERN  (invalid pattern or pattern construction failure)
- `7`  ERR.NOCATCH  (throw with no active handler; for reporting)
- `8`  ERR.SIG      (signature/arity metadata missing or inconsistent; used by FROTH-Named/Checked)
- `9`  ERR.REGION   (invalid region operation; FROTH-Region)
- `10` ERR.FEATURE  (optional feature not enabled; reserved reader forms)

Implementations MAY define additional codes.

### `catch`

**Stack:** `( q -- ... 0 | e )`

- `q` MUST be a QuoteRef or `throw ERR.TYPE`.
- `catch` installs a handler frame capturing:
  - DS depth at entry
  - RS depth at entry
  - CS depth at entry
  - implementation-defined interpreter state needed to resume

Then it executes `q`.

Outcomes:

- **Normal completion:** `catch` removes its handler frame and pushes `0` on DS.
- **On `throw e` (e != 0) inside q:**  
  `catch` restores DS, RS, and CS depths to their saved values, removes its handler frame, and pushes `e` on DS.

> **Informative example — REPL-safe evaluation**
>
> ```froth
> [ 1 drop drop ] catch
> \ -> ERR.STACK (for example)
> ```
>
> In a robust interactive system, every top-level evaluation is wrapped in `catch` so errors do not brick the session.

### `throw`

**Stack:** `( e -- )`

- If `e == 0`: do nothing and return.
- If `e != 0`: unwind control to the most recent active handler frame, as if returning from that `catch` with error `e`.

If no handler frame exists:

- The system MUST enter an implementation-defined **abort** behavior.
- The system SHOULD report ERR.NOCATCH and the error code `e` through diagnostic channels (console/log).
- The system MUST restore a usable top-level evaluation state (REPL/task loop) if possible.

### Diagnostics (recommended, not required)

Implementations SHOULD maintain a fixed-size last error record (no heap allocation) storing:

- last error code
- SlotRef currently executing (if any)
- instruction pointer within QuoteRef (token index)
- optional platform fault info

> **Informative note**
>
> `throw` uses integer error codes so that error signaling does not allocate. Rich error context (word name, IP, etc.)
> can be stored in a fixed “last error record” owned by the VM, not on the heap.

---

## FROTH-Base (mandatory): arithmetic and I/O primitives

FROTH-Base defines a minimal primitive vocabulary required for a practical REPL and embedded work. These words are typically implemented as primitives.

### Integer arithmetic

All arithmetic operates on **numbers** with wraparound semantics (two's complement modulo 2^W), where W is the implementation's usable integer width (which MAY be less than the full cell width if bits are reserved for tagging).

- `+`   `( a b -- sum )`
- `-`   `( a b -- diff )`
- `*`   `( a b -- prod )`
- `/mod` `( a b -- rem quot )`  
  Division truncates toward zero; remainder has the sign of `a`. If `b == 0`, `throw ERR.TYPE` OR an implementation-defined division error (recommended: `ERR.TYPE`).

### Comparisons

Comparisons push a **flag** (a number). Flag convention (normative):  

- false = `0`  
- true  = `-1` (all bits set), matching standard Forth idiom.

- `<` `( a b -- flag )`
- `>` `( a b -- flag )`
- `=` `( a b -- flag )`

### Bitwise operations

- `and`    `( a b -- x )`
- `or`     `( a b -- x )`
- `xor`    `( a b -- x )`
- `invert` `( a -- x )`
- `lshift` `( a n -- x )`  (shift count masked/implementation-defined; recommended: mask to 0..W-1)
- `rshift` `( a n -- x )`  (logical shift; for arithmetic shift provide `arshift` as optional)

### Minimal character I/O

These words define a portable base for REPLs. Encoding is implementation-defined (recommended: ASCII/UTF-8 subset).

- `emit` `( char -- )`  Output one character.
- `key`  `( -- char )`  Read one character (blocking).
- `key?` `( -- flag )`  Non-blocking: input available? (flag convention per 7.2)

### Minimal monotonic timing (recommended)

- `millis` `( -- n )`

`millis` returns a monotonic millisecond counter suitable for elapsed-time
measurement. The value is observational only and wraps according to the
implementation's number representation.

### Return stack transfer (aux stack)

FROTH-Base provides a user-visible auxiliary stack RS (Section 3.2).

- `>r` `( x -- ) ( R: -- x )`  Move TOS from DS to RS.
- `r>` `( -- x ) ( R: x -- )`  Move top of RS to DS.
- `r@` `( -- x ) ( R: x -- x )` Copy top of RS to DS (optional but recommended).

RS under/overflow MUST signal `ERR.RSTACK`.

RS is restored by `catch` on error unwind (Section 6.2), discarding any RS values pushed after the handler frame.

---

## FROTH-Named (optional): named stack frames

### Motivation

FROTH-Named addresses the main readability barrier of stack languages: invisible data flow. It makes stack effects *normative bindings* at definition time and compiles name references down to `perm` sequences. Values never leave DS. No allocation is required.

### Binding syntax

A word definition with named parameters uses a stack-effect declaration as a binding form:

```froth
: distance ( x1 y1 x2 y2 -- d )
  x2 x1 - dup *
  y2 y1 - dup *
  + sqrt
;
```

The declaration `( x1 y1 x2 y2 -- d )` is **normative** in FROTH-Named:

- names left of `--` bind the **N** entry inputs,
- names right of `--` declare **M** outputs (names are documentation; counts are used for verification and cleanup).

If `--` is absent, the parentheses are treated as a comment with no binding semantics.

### Binding semantics

At entry, the top N DS items correspond to the N input names in order:

Stack (top on right): `... x1 y1 x2 y2`  
Depth from TOS: `x1=3, y1=2, x2=1, y2=0`.

A name is a **read-only alias** for the value at that entry position. Names do not create storage. They are compile-time metadata.

### Required metadata for compilation (normative)

FROTH-Named compilation requires net stack effects for each token in the body. The compiler determines stack deltas from:

- literals and quotations: known (+1 push),
- `perm`: known (delta = len(pattern) - n),
- primitives: MUST have `slot.in_arity` and `slot.out_arity` metadata,
- user-defined words: SHOULD have metadata set (via `arity!` or via their own `( ... -- ... )` declaration).

If the compiler encounters a slot call with unknown arity metadata, it MUST reject the definition with `ERR.SIG` unless the call appears inside an explicitly *raw* region (implementation-defined escape hatch).

> **Informative staging note**
>
> A minimal implementation may first treat the declaration as semantic arity
> metadata for any signed definition, while only invoking the full named-body
> compilation path for definitions whose outer body actually references one of
> the bound input names. Signed definitions with no such references need no
> name-lowering to remain semantically correct.

### Compilation algorithm for name references

The compiler tracks `delta`, the net DS depth change since entry (relative to the entry frame base). It must also enforce that DS depth never drops below the entry base.

When encountering a name reference with original depth `d0` (0 = TOS at entry, N-1 = deepest input), the compiler computes current depth:

`d = d0 + delta`.

To duplicate the value currently at depth `d` to TOS while preserving the segment, emit:

- `n = d + 1`
- `pattern = p[ d (d-1) ... 1 0 d ]` (TOS-right: leftmost = deepest output, rightmost = new TOS)
- `n pattern perm`

Reading the pattern left-to-right as bottom-to-top: the original items remain in place (descending from `d` to `0`), plus the targeted value `d` is duplicated on top. This is the generalized "over."

The reference increases `delta` by `+1` because it pushes one additional value.

### Frame invariants and static verification

FROTH-Named implementations MUST enforce:

1. **No underflow into bound inputs:** during compilation, the inferred DS depth must never go below the entry base. Violations -> `ERR.STACK`.
2. **Output count agreement:** at the end of the body, the net delta MUST equal **M** (declared outputs). Otherwise -> `ERR.STACK`.

### Frame cleanup (required)

After executing the body, the stack contains (conceptually):

`... (N original inputs) (M outputs)`

The compiler emits a final cleanup `perm` to drop the original inputs and keep the outputs:

- `n = N + M`
- `pattern = p[ (M-1) (M-2) ... 1 0 ]` (TOS-right: the top M values, listed bottom-to-top)
- `n pattern perm`

This keeps the top M values (the outputs) and drops the N beneath them (the original inputs).

### Interaction with quotations

Names are scoped to their enclosing definition and are **not captured by quotations**:

```froth
: ex ( x -- )
  [ x 1 + ] call   \ ERROR: x not visible inside quotation
;
```

To pass a bound value into a quotation, explicitly push it:

```froth
: ex ( x -- )
  x [ 1 + ] call
;
```

This avoids closures and hidden allocation.

### Name shadowing

Parameter names shadow slot names within the body. To force a slot call when a name collides, use `'name call`:

```froth
: ex ( x -- )
  x        \ name reference
  'x call  \ slot invocation of "x"
;
```

---

### Worked example: named inputs compile to `perm` (informative)

Named stack frames are meant to preserve the **feel** of stack programming while making dataflow legible.

Example:

```froth
: hypot ( x y -- r )
  x x *  y y *  +  sqrt
;
```

A FROTH-Named compiler can lower this without introducing “locals storage” by translating each name reference into a `perm`
that duplicates the appropriate entry value to the top of stack (preserving order), and then applying a cleanup `perm`
at the end to drop original inputs.

You can think of the names as **labels on the entry stack window**, not variables that change.

**Why no capture:** quotations (`[ ... ]`) do not capture named bindings. To pass a named value into a quotation, push it
explicitly first. This keeps Froth free of closure environments and prevents hidden allocation.

### Code size and optimization notes (informative)

FROTH-Named deliberately lowers name references to `perm` so that *values never leave the data stack* and no locals storage is required. On larger MCUs this is typically fine, but on very small flash targets (e.g., AVR) a naïve lowering can inflate code size if many name references occur.

Recommended implementation strategies (often placed in **FROTH-Perf**) that preserve semantics:

- **perm fusion:** adjacent `perm` operations can often be combined or cancelled.
- **peephole optimization:** common patterns like “duplicate depth *d*” can be recognized and replaced with a compact internal micro-op.
- **backend locals frame (optional):** a compiler may internally lower name references to a fixed locals frame (RS-based or dedicated) rather than literal `perm` sequences, *provided the observable DS behavior matches the spec* and no heap allocation is introduced.

The language-level contract remains: named frames are labels on an entry stack window, not mutable variables.

## FROTH-Checked (optional): kinds and contracts

FROTH-Checked adds a safety net designed for embedded constraints:

- no garbage collection,
- no mandatory boxing,
- possible to compile out entirely.

### Kind stack (KS)

A FROTH-Checked implementation maintains a parallel **kind stack (KS)** aligned with DS. Each DS value has an associated KindID tag.

Minimum required KindIDs:

- `K.NUMBER`
- `K.QUOTE`
- `K.SLOT`
- `K.PATTERN`
- `K.STRING` (required if FROTH-String-Lite is enabled)
- `K.ANY` (wildcard for contracts)

Propagation rules (minimum):

- pushing a literal number -> push `K.NUMBER`
- pushing a QuoteRef -> push `K.QUOTE`
- pushing a SlotRef -> push `K.SLOT`
- pushing a PatternRef -> push `K.PATTERN`
- pushing a StringRef -> push `K.STRING` (if supported)
- `perm` MUST permute KS exactly as it permutes DS for the top `n` segment (using the same TOS-right output ordering).

### User-defined kinds (recommended)

To make FROTH-Checked catch real firmware bugs (handle confusion), implementations SHOULD support user-defined kinds.

Provide:

- `kind` `( slot -- )`  
  Creates a new KindID, registers it under `slot`, and defines `slot` as a constant word that pushes that KindID.

- `tag` `( x kindid -- x )`  
  In checked builds: sets the KS tag of `x` to `kindid`.  
  In unchecked builds: MUST at least drop `kindid` (so code remains runnable).

### Contracts

A **contract** (ContractRef) contains:

- `arity` (non-negative integer)
- `kinds[0..arity-1]` expected kinds for the **top arity values** on DS at call entry:
  - `kinds[0]` applies to the deepest of the arity segment,
  - `kinds[arity-1]` applies to TOS.

`K.ANY` means “do not check this position”.

### Contract literal (optional reader form): `k[ ... ]`

A FROTH-Checked implementation MAY provide `k[ ... ]` which reads a list of kind names and produces a ContractRef.

Minimum kind names required:

- `number`, `quote`, `slot`, `pattern`, `any`

If user-defined kinds exist, `k[ ... ]` SHOULD accept identifiers that name registered kinds (created via `kind`). Unrecognized kind names MUST raise `ERR.SIG`.

### `sig` (attach contract)

**Stack:** `( slot contract -- )`

- `slot` MUST be SlotRef or `throw ERR.TYPE`
- `contract` MUST be ContractRef or `throw ERR.TYPE`
- Set `slot.contract := contract`

### Contract enforcement

Before entering `slot.impl`, if `slot.contract` exists:

- Ensure DS depth >= arity, else `throw ERR.STACK`
- For each position i in 0..arity-1:
  - expected = contract.kinds[i]
  - actual = KS tag of the corresponding DS value
  - If expected != K.ANY and actual != expected: `throw ERR.TYPE`

---

### Practical contracts (informative)

Kinds/contracts are an optional safety net intended for:

- better error messages at the REPL,
- safer FFI boundaries (cells vs quotes vs slots),
- and enabling compile-time tooling (e.g., named-frame effect tracking).

A typical pattern is to attach only **arity** and a few coarse kinds:

```froth
\ ( a b -- sum )
' + k[number number -- number] sig
```

In checked builds, calling `+` with a quotation on top would `throw ERR.TYPE` instead of producing silent corruption.

In unchecked builds, contracts may be ignored or used only for diagnostics.

## FROTH-Region (optional): deterministic watermark allocation

FROTH-Region provides a deterministic mechanism to reclaim heap allocations without GC.

### `mark`

**Stack:** `( -- m )`

Push a **mark** `m` representing the current heap watermark (an opaque number).

### `release`

**Stack:** `( m -- )`

Restore the heap watermark to `m`, invalidating any heap objects allocated after `m`.

If `m` is invalid or out of range: `throw ERR.REGION`.

**Safety rule (normative):** After `release`, programs MUST NOT use references to invalidated objects. FROTH-Region does not require full reachability checks.

**Recommended practice:** use `mark`/`release` for initialization-time generators, macro expanders, and REPL tooling where lifetimes are well-scoped. Avoid unbounded `q.pack` loops in production firmware.

---

### FROTH-Region-Strict (optional): fail-fast allocation gating

Some devices are too small to tolerate “accidental heap growth” (e.g., an unintended `q.pack` inside a long-running `while`). **FROTH-Region-Strict** is an optional tightening of FROTH-Region that makes runtime allocation an explicit, scoped choice.

An implementation claiming **FROTH-Region-Strict** MUST enforce:

1. **Active region tracking:** the VM maintains a region depth counter `R` (initially 0).
   - `mark` increments `R`.
   - `release` decrements `R`.
2. **LIFO release (recommended for simplicity; normative for strict mode):** `release` MUST only accept a mark corresponding to the most recently executed `mark`. Otherwise it MUST `throw ERR.REGION`.
3. **Allocation gating:** if `R == 0`, runtime heap-allocation operations accessible to user code MUST `throw ERR.REGION`.

At minimum, the gated operations MUST include:

- `q.pack` (dynamic quotation construction)
- `pat` (dynamic pattern compilation)

**Rationale:** strict mode does not “prevent leaks” if a region is kept open forever. Its purpose is to turn accidental dynamic allocation into a fail-fast error and to encourage explicit lifetime scoping.

### Why regions matter (informative)

Froth intentionally avoids garbage collection. Metaprogramming tools (weavers, linkers, code generators) still need a way
to allocate temporary objects safely. Regions provide that deterministically:

```froth
mark           \ -> m
\ build temporary quotes/patterns here
release        \ rewind heap to m
```

A common policy is:

- “scratch allocations” happen inside a marked region,
- only committed definitions (installed via `def`) are kept.

This keeps interactive tooling safe on embedded devices without GC pauses.

## FROTH-String-Lite (optional): immutable strings for output

FROTH-String-Lite introduces **immutable byte strings** as a first-class value type.

### Design goals

- **Make common REPL output pleasant.** Static messages should be trivial to print.
- **Avoid two-slot `addr len` strings** on DS. A single value reduces shuffling and improves readability in a concatenative language.
- **Preserve embedded determinism.** No garbage collector; no hidden allocation in hot paths.

### Value type: StringRef

A **StringRef** is an immutable heap object containing a byte sequence.

- The byte sequence is **recommended** to be UTF-8 encoded for human text, but Froth’s string operations treat it as raw bytes.
- StringRef values occupy **one** DS slot.
- Equality and indexing are byte-based.

### Required words (String-Lite)

If FROTH-String-Lite is enabled, the following words MUST be provided:

- `s.emit ( s -- )`  
  Emit all bytes of `s` to the current console/output stream. Implementations MAY optimize internally, but the observable behavior MUST match repeated `emit`.

- `s.len ( s -- n )`  
  Push the byte length of `s`.

- `s@ ( s i -- byte )`  
  Read the byte at index `i` (0-based). If `i` is out of range, MUST `throw ERR.BOUNDS`.  
  The returned `byte` is a number in `0..255`.

- `s.= ( s1 s2 -- flag )`  
  Push Froth boolean true iff the two strings have identical byte sequences. (`true = -1`, `false = 0` per FROTH-Base.)

### Reader form and allocation model

A string literal `"..."` produces a StringRef value (see Reader section: String literals).

- In a definition like `: greet "Hello" s.emit ;`, the string literal is allocated **once** at definition time as part of the quotation object.
- Executing `greet` does **not** allocate.

This preserves Froth’s “no hidden allocation in hot paths” principle while keeping the REPL pleasant.

### Examples

Static message:

```froth
: greet  "Hello, Froth!" s.emit ;
```

Token comparison:

```froth
: is-quit?  "quit" s.= ;
```

Byte-level parsing:

```froth
: first-byte  0 s@ ;
```

### Interaction with FROTH-Checked

If `FROTH-Checked` is enabled alongside `FROTH-String-Lite`:

- The implementation MUST provide the built-in kind `K.STRING`.
- String literals MUST push a value tagged as `K.STRING`.
- `s.emit`, `s.len`, `s@`, and `s.=` SHOULD have contracts that require `K.STRING` inputs.

This moves common mistakes (passing a number where a string is expected) from “weird behavior later” to an immediate, local `ERR.TYPE`.

### What String-Lite deliberately excludes (and why)

String-Lite is *not* a string manipulation library.

- No concatenation primitive: concatenation would allocate on the linear heap and is a classic fail-slow trap on small devices.
- No substring/slicing: “views” create lifetime coupling that a no-GC heap cannot track safely.

If you need dynamic formatting later, use a **fixed scratch buffer** (PAD-style) and pack once (see FROTH-String and future guidance below).

---

## FROTH-String (optional): explicit allocation and interop

FROTH-String extends FROTH-String-Lite with explicit allocation utilities for interop with native buffers and FFI.

### Additional word: `s.pack`

If FROTH-String is enabled, the implementation MUST provide:

- `s.pack ( addr len -- s )`  
  Allocate a new StringRef by copying `len` bytes from memory starting at `addr`.

**Notes:**

- `s.pack` is an **allocator**. Its purpose is to bridge from FFI / buffers into an immutable StringRef.
- In `FROTH-Region-Strict` mode, `s.pack` MUST throw `ERR.REGION` unless a region is active (fail-fast).
- `addr` is a raw address (number) and is inherently unsafe. This is acceptable because it is explicit and is primarily used at system boundaries.

### Guidance: PAD-style formatting (informative)

For dynamic formatting (post-sprint), prefer a fixed scratch buffer over heap concatenation. A minimal PAD vocabulary could be:

- `pad.reset ( -- )`
- `pad.emit  ( c -- )`
- `pad.s     ( -- addr len )`
- `pad.type  ( -- )` (optional)
- `pad.pack  ( -- s )` (optional allocator)

This mirrors the spirit of “pictured numeric output” in classic Forth: deterministic, bounded, and allocation-free until explicitly packed.

## FROTH-REPL (optional): stack visualization protocol

This section specifies a recommended REPL behavior for consistent tooling across serial terminals, WebSerial, and IDE integrations.

### After-line stack display (recommended)

After executing each top-level line, the REPL SHOULD print the DS state:

```text
> 3          [3]
> 4          [3 4]
> +          [7]
> 5          [7 5]
> *          [35]
```

Format: `[v0 v1 ... vN]` where `v0` is stack bottom and `vN` is TOS.

Object references use abbreviated forms:

- `<q:N>` QuoteRef with N tokens
- `<s:name>` SlotRef for named slot
- `<p:N>` PatternRef with N indices

### Named-frame entry display (recommended)

When executing a FROTH-Named definition, implementations SHOULD support a verbose mode that displays the entry bindings:

```text
> 10 20 30 40 distance
  entering distance: x1=10 y1=20 x2=30 y2=40
  [28]
```

This MUST be gated behind a verbosity flag (e.g., `verbose on` / `verbose off`) to avoid noise in production.

### Single-step mode (recommended)

A step mode (`step on`) SHOULD display DS after each executed token within a line or traced definition:

```text
> step on
> 3 4 + 5 *
  3           [3]
  4           [3 4]
  +           [7]
  5           [7 5]
  *           [35]
  [35]
```

For constrained serial links, implementations MAY restrict stepping to definitions explicitly marked for tracing (e.g., `trace 'distance`).

### Desugaring display (recommended)

When FROTH-Named or other sugar is used, a mode (`desugar on`) SHOULD display the lowered form (e.g., inserted `perm` patterns in TOS-right letter notation and cleanup), ideally including an optimized form when applicable.

### Machine-readable protocol (optional)

Implementations MAY emit machine-readable lines prefixed with `#`:

```text
#STACK [3 4 7]
#ENTER distance x1=10 y1=20 x2=30 y2=40
#STEP + [7]
#ERR 4 ERR.TYPE at <s:gpio.write> token 2
```

Lines beginning with `#` are reserved for tooling; all other output is human-oriented.

## FROTH-Stdlib (optional): standard combinators and helpers

This section standardizes a small vocabulary definable on FROTH-Core+Base.

### Basic stack words (definitions)

```froth
' dup  [ 1 p[a a] perm ] def
' swap [ 2 p[a b] perm ] def
' drop [ 1 p[] perm ] def
' over [ 2 p[b a b] perm ] def
' 2over [ 4 p[d c b a d c] perm ] def
' 2dup [ 2 p[b a b a] perm ] def
' 2drop [ 2 p[] perm ] def
' 2swap [ 4 p[b a d c] perm ] def
' rot  [ 3 p[b a c] perm ] def
' -rot [ 3 p[a c b] perm ] def
```

### `if`

```froth
' if [ choose call ] def
```

Usage: `cond [then] [else] if`.

### Small flag/data helpers

```froth
' flag>n [ [ 1 ] [ 0 ] if ] def
```

`flag>n` converts Froth truth values into ordinary numeric `0` or `1`.

### Quotation combinators

These are standardized by name and behavior. They are definable on FROTH-Core+Base.

**Assumed effects (normative for this stdlib set):**

- Unless stated otherwise, each quotation/slot argument `f`, `g`, `h` is assumed to have stack effect `( x -- y )` (consume one value, produce one value).
- `dip` is stack-polymorphic: it runs its callee on the stack **with one saved value removed**, then restores that value.

#### dip

- **Stack:** `( x q -- ... x )`
- Execute `q` with `x` removed from DS; then restore `x` on normal completion.
- If `q` throws, `x` is not restored (control does not return normally).

Reference definition (requires FROTH-Base `>r` / `r>`):

```froth
' dip [ swap >r call r> ] def
```

#### keep

- **Stack:** `( x q -- y x )` where `q : ( x -- y )`
- Execute `q` on `x` while preserving `x`.

Reference definition (pure DS shuffles):

```froth
' keep [ over swap call swap ] def
```

#### bi

- **Stack:** `( x f g -- y z )` where `f:(x--y)` and `g:(x--z)`
- Apply two quotations/slots to the same value.

Reference definition (pure DS shuffles; no `dip` required):

```froth
' bi [ -rot keep rot call ] def
```

#### tri

- **Stack:** `( x f g h -- y z w )` where `f:(x--y)`, `g:(x--z)`, `h:(x--w)`

Reference definition (uses RS to temporarily save quotations):

```froth
' tri [
  >r >r        \ stash h and g
  keep         \ f(x) x
  r> r>        \ restore g h
  >r           \ stash h
  keep         \ f(x) g(x) x
  r> call      \ h(x)
] def
```

#### bi*

- **Stack:** `( x y f g -- y1 y2 )` where `f:(x--y1)` and `g:(y--y2)`

Reference definition:

```froth
' bi* [ >r swap >r call r> r> call ] def
```

#### bi@

- **Stack:** `( x y f -- y1 y2 )` where `f:(x--y1)` and `f:(y--y2)`
- Requires FROTH-Base `r@` (recommended).

Reference definition:

```froth
' bi@ [ >r swap r@ call swap r@ call r> drop ] def
```

### Counted iteration: `times`

- **Stack:** `( n q -- )`
- Execute `q` exactly `n` times. If `n <= 0`, execute zero times.

**Body discipline (normative):** `q` MUST be stack-neutral `( -- )`. This matches the `while` disciplined-stack rule and prevents accidental stack growth in long-running firmware loops.

Reference definition (uses RS to keep `q`):

```froth
' times [
  >r                      \ stash q on RS; DS: n
  [ dup 0 > ]             \ condQ: n -- n flag
  [ 1 - r@ call ]         \ bodyQ: n -- n  (q must be stack-neutral)
  while
  drop                    \ drop final n
  r> drop                 \ discard q from RS
] def
```

---

## FROTH-Perf (optional): performance representations and optimization

FROTH-Perf is an **optional** profile that permits additional callable representations (ITC/DTC/native) and optimizations, while preserving FROTH-Core semantics.

The core idea is that the **slot** is the semantic unit: a slot has stable identity (name → SlotRef), and its implementation may change over time (redefinition or promotion) without changing how Froth code *names and composes* behavior.

### Additional callable representations (informative)

A FROTH-Core implementation needs only:

- QuoteRefs (quotations)
- SlotRefs (word identities)

A FROTH-Perf implementation MAY additionally represent a slot’s current implementation using:

- **Threaded code** (ITC/DTC) objects
- **Native stubs** (e.g., a trampoline to compiled machine code)
- **Inline caches** for faster dispatch

These representations MUST preserve the same externally visible behavior at the data stack boundary:

- the same arity and stack discipline,
- the same error/throw behavior,
- and the same redefinition model (a slot’s meaning changes when `def` changes its implementation).

### Redefinition, `slot.version`, and caches (normative)

Implementations SHOULD maintain `slot.version` as a non-decreasing integer incremented on each successful `def`.

Any inline cache keyed by slot identity MUST also key by `slot.version` (or an equivalent invalidation mechanism), so redefinition invalidates caches correctly.

### Canonical representation rule (normative)

Snapshot persistence (FROTH-Snapshot) and introspection (`see`, `q@`) require a stable, pointer-free representation.

Therefore, if a FROTH-Perf implementation installs a **non-QUOTE** implementation for a slot (threaded or native), it MUST also retain a **canonical QuoteRef** (or an equivalent decompilable IR that can be re-materialized as a QuoteRef) representing the same semantics.

- `save` MUST persist the canonical QuoteRef, not the optimized representation.
- After `restore`, a slot may start in QUOTE form; promotion is a performance cache and MAY be re-derived later.

If an overlay-owned slot has no canonical persistable representation, `save` MUST fail (see Snapshot spec: `ERR.SNAP.NONPERSIST`).

### `perm` optimization and named lowering (informative)

`perm` is designed to be a canonical “stack movement IR.” In FROTH-Perf, implementations can make stack-heavy code smaller and faster without changing semantics:

- **perm fusion:** adjacent `perm` operations over overlapping windows can often be combined or eliminated.
- **pattern compilation:** compile `p[ ... ]` patterns into compact internal micro-ops.
- **named frames backend:** for tiny flash targets, a compiler MAY lower name references into a fixed locals frame (RS-based or dedicated) rather than literal `perm` sequences, provided observable DS behavior remains identical and no heap allocation is introduced.

## FROTH-Addr (optional): native addresses and hardware handles

FROTH-Addr provides full-width machine addresses as a first-class value type, enabling direct hardware register access on embedded targets without compromising the compact tagged-value core.

### Design rationale

Froth's tagged Number representation (implementation-defined range, typically smaller than the full cell width) is sufficient for arithmetic, loop counts, flags, offsets, and most program logic. However, memory-mapped peripheral registers on 32-bit MCUs often reside at addresses that exceed the Number range (e.g., ESP32 GPIO at `0x3FF44000`). FROTH-Addr solves this boundary concern without taxing every stack slot.

Addresses are semantically distinct from Numbers: they represent machine locations, not arithmetic values. This distinction enables snapshot safety (addresses are non-persistable) and type safety (adding two addresses is meaningless; adding an address and an offset is well-defined).

### Value type: NativeAddr

A **NativeAddr** is an opaque value representing a full-width machine address or hardware handle.

- NativeAddr values occupy one DS slot.
- NativeAddr values are **non-persistable**: `save` MUST refuse to serialize overlay slots whose implementations transitively contain NativeAddr literals. Implementations SHOULD signal `ERR.SNAP.NONPERSIST`.
- NativeAddr values MUST NOT participate in Number arithmetic (`+`, `-`, `*`, `/mod`). Passing a NativeAddr to a Number-only word MUST signal `ERR.TYPE`.
- NativeAddr equality is bitwise equality of the underlying address.

### Required words (FROTH-Addr)

#### Address creation

- `addr.pack ( hi lo -- addr )`
  Combine two Numbers into a NativeAddr. On 32-bit cells: `value = (hi << 16) | (lo & 0xFFFF)`.

Board packages SHOULD provide named address constants as FFI primitives (e.g., `gpio.base ( -- addr )`). This is the expected primary way addresses enter Froth programs.

#### Memory access (width-specific)

All memory access words take `( addr offset -- ... )` form. The offset is an ordinary Number, keeping struct/register field walking cheap.

- `@8  ( addr offset -- byte )`   Read unsigned 8-bit value from `addr + offset`.
- `@16 ( addr offset -- half )`   Read unsigned 16-bit value from `addr + offset`.
- `@32 ( addr offset -- value )`  Read 32-bit value from `addr + offset`.
- `!8  ( value addr offset -- )`  Write low 8 bits of `value` to `addr + offset`.
- `!16 ( value addr offset -- )`  Write low 16 bits of `value` to `addr + offset`.
- `!32 ( value addr offset -- )`  Write 32-bit `value` to `addr + offset`.

All memory access words MUST use volatile semantics. This is required for correct MMIO register access.

#### Address arithmetic

- `addr+ ( addr offset -- addr' )` Add a Number offset to a NativeAddr, producing a new NativeAddr.
- `addr.= ( addr1 addr2 -- flag )` Compare two NativeAddr values for equality.

#### Inspection

- `addr. ( addr -- )` Print the underlying address in hexadecimal.

### Interaction with FROTH-Snapshot

NativeAddr values are non-persistable by construction. Addresses defined at boot via FFI are base-layer primitives and do not affect `save`. User definitions like `: gpio-out@ gpio.base 0x04 @32 ;` are persistable because `gpio.base` is a slot call — the address is reconstructed at boot, not baked into the snapshot.

### Interaction with FROTH-Checked

If FROTH-Checked is enabled, the implementation MUST provide the built-in kind `K.ADDR`. Memory access words SHOULD have contracts requiring `K.ADDR` for the address argument and `K.NUMBER` for the offset.

---

## End-to-end examples

### Named parameters (FROTH-Named)

```froth
: add ( a b -- sum )
  a b +
;
```

A FROTH-Named implementation may lower this to an optimized equivalent that is effectively just `+` plus cleanup.

### Error recovery

```froth
' fail [ 42 throw ] def
[ fail 123 ] catch
\ leaves: 42
```

### Safe REPL tooling with `catch`

Recommended REPL policy:

- wrap each input line in an implicit `catch`,
- print error code and last-error record,
- keep the prompt alive without reboot.

### A minimal hardware blink sketch (informative)

Assuming an FFI profile provides:

- `gpio.mode ( pin mode -- )`
- `gpio.write ( pin value -- )`
- `ms ( n -- )`

you can write:

```froth
: blink ( pin -- )
  dup 1 gpio.mode            \ set output
  [  \ loop:
    dup 1 gpio.write 250 ms
    dup 0 gpio.write 250 ms
    1                         \ true
  ] [ ] while                 \ infinite loop (until interrupted)
;
```

With `CAN` interrupt handling enabled in the REPL, you can stop the loop without rebooting.

### A “safe top-level” policy (informative)

A practical embedded Froth REPL does:

1) Snapshot DS/RS depths.
2) Evaluate user input under `catch`.
3) On error: restore DS/RS, print `ERR.*`, keep prompt alive.
4) Optionally: rollback scratch heap allocations to a `mark`.

This is how Froth stays enjoyable in large projects: experimentation does not corrupt the session.

### Defining a callback hook (informative)

If the FFI layer defines a hook slot `on-midi` and the C runtime calls it with three cells `(status note vel)`,
your Froth handler is just a word:

```froth
: on-midi ( status note vel -- )
  \ ... decode, route, etc. ...
;
```

Hook calls SHOULD enforce stack neutrality so that a buggy handler cannot poison the VM.
