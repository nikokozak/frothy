# ADR-056: Keep `StringRef` for Immutable Text; Move Dynamic Bytes and Formatting to PAD/Buffers

**Date**: 2026-04-02
**Status**: Proposed
**Spec sections**: `docs/archive/spec/Froth_Language_Spec_v1_1.md` Section 8 (FROTH-String-Lite / FROTH-String), Section 14 (FROTH-Addr), `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md` Sections 7-10
**Related ADRs**: ADR-023 (String-Lite heap layout), ADR-024 (FROTH-Addr), ADR-032 (`mark` / `release`), ADR-043 (transient string buffer), ADR-046 (number-to-string primitives), ADR-047 (unified string length limit), ADR-054 (FROTH-CellSpace)

## Context

Froth's original String-Lite design was intentionally small and specific:

- immutable text values,
- one stack slot per string,
- good REPL ergonomics,
- and no hidden allocation in hot paths.

That original shape is still strong. The spec says so directly:

- String-Lite exists to make common output pleasant,
- it deliberately avoids two-slot `addr len` strings on DS,
- and it explicitly excludes general string manipulation as a fail-slow trap on
  small devices.

The complexity entered later when strings were asked to solve more than "text":

- repeated top-level literal evaluation created heap churn,
- FFI wanted a way to hand byte payloads into Froth,
- runtime formatting wanted dynamically produced textual values,
- and protocol assembly started to look like "string work."

ADR-043 solved the immediate workshop problem by introducing a transient string
ring plus descriptor-backed lifetimes, promotion, and stale detection.
ADR-046 then added runtime number-to-string constructors on top of that model.

That shipped practical functionality quickly, but it also changed the meaning
of the string subsystem:

- `StringRef` stopped being just immutable text,
- string lifetime became a language-visible concept,
- and string operations started doing double duty for both human text and
  mutable/transient byte work.

That is now the wrong long-term direction for two reasons.

### 1. The current string model has too many user-visible storage classes

Today the user must reason about:

- permanent heap strings,
- transient descriptor-backed strings,
- promotion to permanent storage,
- stale access,
- unified length limits that partly exist because of the transient ring,
- and runtime constructors that look like text values but are really formatted
  scratch output.

That is too much machinery for a language whose core pitch is directness.

### 2. CellSpace resolves aggregate data, not byte-level text/formatting work

ADR-054 establishes the next mutable-data direction:

- tagged Froth cells,
- cell-indexed addressing,
- managed aggregate storage.

That is the right answer for framebuffers, counters, lookup tables, and mutable
records.

It is not a good reason to collapse strings into "just bytes in the data
space." CellSpace is cell-oriented and tagged by design. Forcing byte-packed
text or protocol buffers into that same model would immediately create a second
byte-addressing story inside a cell-indexed region, which cuts against the
clarity ADR-054 is meant to restore.

The correct architectural split is therefore:

- **StringRef for immutable text**
- **PAD/buffers for mutable bytes and formatting work**

## Problem Statement

Froth currently lacks a clean separation between:

1. immutable human-facing text values,
2. mutable or transient byte buffers,
3. runtime formatting scratch,
4. and native/FFI byte interop.

If that separation is not restored, strings will keep accumulating storage
rules, lifetime rules, and special cases.

## Options Considered

### Option A: Keep ADR-043 as the long-term string model

Keep the hybrid permanent/transient StringRef model:

- literals and kept strings remain heap-backed,
- runtime strings remain transient by default,
- the ring buffer, descriptor table, promotion rules, and stale detection stay
  language-visible,
- and formatting words continue to manufacture StringRef values by default.

Trade-offs:

- Pro: already implemented.
- Pro: solves repeated top-level literal churn and runtime string allocation
  pressure immediately.
- Con: too many storage/lifetime concepts for one value type.
- Con: stale-string semantics leak implementation detail into normal programs.
- Con: blurs text values and scratch/buffer work into one abstraction.
- Con: keeps ADR-046 pointed toward "dynamic managed strings by default" rather
  than explicit formatting/buffer work.

### Option B: Remove `StringRef`; make all strings `addr len`

Abandon one-slot string values and move fully to a Forth-style `addr len`
model for all text and byte work.

Trade-offs:

- Pro: unifies text with raw byte/buffer handling.
- Pro: matches classic Forth practice.
- Pro: makes slicing and subviews cheap.
- Con: gives up one of Froth's genuinely good ergonomics for ordinary text.
- Con: reintroduces the two-slot shuffling String-Lite was created to avoid.
- Con: pushes immutable text and mutable byte buffers back into the same model
  again, just a different one.
- Con: does not fit well with ADR-054's cell-indexed tagged CellSpace model.

### Option C: Keep `StringRef` for text, use explicit PAD/buffer mechanisms for bytes

Retain immutable one-slot text values, but stop asking them to solve buffer and
formatting problems.

Under this model:

- string literals remain `StringRef`,
- `s.emit`, `s.len`, `s@`, and `s.=` remain the core String-Lite surface,
- `s.pack` remains the explicit bridge from bytes to immutable text,
- repeated literal churn is solved by literal interning,
- runtime formatting and protocol assembly move to explicit PAD/buffer work,
- and the transient string ring stops being the long-term language direction.

Trade-offs:

- Pro: preserves the strongest part of String-Lite.
- Pro: simplifies the user model substantially.
- Pro: aligns with ADR-054 instead of fighting it.
- Pro: creates a clean boundary between text values and bytes.
- Con: requires a follow-on PAD/buffer design for dynamic formatting and byte
  assembly.
- Con: current ADR-043 / ADR-046 implementations become workshop expedients
  rather than the target model.

### Option D: Opaque managed string/buffer hierarchy

Keep `StringRef`, but add a second family of managed byte-buffer handles with a
separate accessor vocabulary and conversion words.

Trade-offs:

- Pro: could be safe and cleanly separated.
- Pro: avoids `addr len` shuffling at the buffer boundary too.
- Con: increases surface area quickly.
- Con: repeats the same handle-heavy mistake Froth is already correcting in the
  mutable-data story.
- Con: harder to compose than explicit buffer/PAD mechanisms.

## Decision

**Option C: keep `StringRef` as Froth's immutable text value, narrow it back to
String-Lite scope, and move dynamic byte/formatting work to explicit PAD or
buffer-oriented mechanisms.**

This ADR therefore **supersedes ADR-043 as the long-term direction** for
Froth strings, while acknowledging that ADR-043 remains the current
implementation until the new direction is landed.

## Decision Details

### 1. `StringRef` stays

`StringRef` remains a first-class Froth value type:

- immutable,
- one DS slot,
- heap-backed,
- byte-indexed for inspection,
- suitable for literals, printed text, labels, keywords, and other textual
  values.

This keeps the core ergonomic win from String-Lite.

### 2. String-Lite is narrowed back to text

The long-term required String-Lite surface remains intentionally small:

- `"..."` literals
- `s.emit`
- `s.len`
- `s@`
- `s.=`

This ADR reaffirms the original spec boundary: String-Lite is for immutable
text values, not for a general runtime string-manipulation subsystem.

### 3. Literal interning replaces language-visible transient-literal machinery

Repeated evaluation of the same literal text must not keep filling the heap.

Therefore the long-term string-literal model is:

- string literals are interned by byte sequence,
- the evaluator performs an "intern or reuse" lookup when materializing a
  literal,
- and identical literal bytes within the same live heap image should reuse the
  same immutable `StringRef` when practical.

Observable consequence:

- repeated top-level EVAL of `"up"` reuses the same immutable text object
  instead of allocating a fresh transient descriptor or a fresh permanent heap
  object every time.

This removes the main motivation for making literals transient in the language
model.

Important clarification:

- this is an implementation-level canonicalization rule, not a new
  language-visible notion of string identity,
- byte equality (`s.=`) remains the only semantic equality rule,
- and the language does not expose pointer identity for StringRefs.

#### Interning table rules

This ADR does not require a specific hash-table design. It does require the
following semantics:

- interning is by exact byte sequence,
- the intern index is a VM-managed implementation detail, not a language value,
- serialized state persists strings, not the intern index itself,
- the intern table is advisory and rebuildable rather than authoritative state,
- any operation that rewinds or replaces heap state (`release`, `wipe`,
  `restore`) must invalidate, prune, or rebuild intern entries that point into
  discarded heap regions,
- the runtime may rebuild or lazily repopulate the intern index after boot,
  restore, or deserialization.

This avoids hidden permanence. Interning must not make a top-level literal
survive `release` merely because it was once indexed.

### 4. The long-term model removes language-visible transient string lifetimes

The following are **not** part of the long-term string model:

- transient ring-backed strings,
- stale-string exceptions as ordinary language semantics,
- implicit promotion on `def`,
- `s.keep` as a central ownership mechanism,
- descriptor-backed temporary string identity.

In other words:

- no transient string ring in the target design,
- no stale detection in the user model,
- no string lifetime bookkeeping exposed as a language concept.

If the VM uses internal scratch buffers for REPL formatting or transport, that
is an implementation concern, not a language feature.

### 5. Bytes and formatting move to explicit PAD/buffer work

Dynamic assembly of bytes is a different problem from immutable text values.

The long-term answer for:

- numeric formatting,
- protocol assembly,
- line buffering,
- byte-oriented FFI interop,
- mutable text construction,
- and other "working bytes" tasks

is **not** "manufacture a managed `StringRef` by default."

It is:

- an explicit PAD-style scratch model,
- or an explicit byte-buffer profile,
- or both.

This ADR does **not** pick the final PAD/buffer surface. It does set the
architectural rule:

- **working bytes are explicit**
- **immutable text is `StringRef`**

### 6. `s.pack` remains the bytes-to-text bridge

`s.pack` stays as the explicit allocation boundary from bytes into immutable
text.

This means:

- byte-oriented code can build or receive data through PAD/buffers,
- and only call `s.pack` when a real immutable text value is needed.

This preserves Froth's explicit-allocation discipline.

This ADR does not change the current `s.pack ( addr len -- s )` contract in the
spec. In the near term, `s.pack` remains the explicit raw-bytes boundary from
FFI/native buffers into immutable text. A future PAD/buffer profile may add
friendlier packing helpers, but this ADR does not require changing `s.pack`'s
existing role.

### 7. ADR-046 is revised in direction

ADR-046 solved a real immediate need, but its long-term direction changes under
this ADR.

The `n>s` / `n>hexs` / `n>bins` family should no longer define the future of
runtime formatting by manufacturing managed StringRefs by default.

Long-term direction:

- formatting words should target PAD/buffer-oriented output first,
- any conversion to `StringRef` should be explicit,
- and if `n>s`-style words remain available, they should be treated as
  compatibility conveniences rather than the canonical formatting model.

The same applies to dynamic assembly helpers such as `s.concat`: they are not
the core long-term String-Lite direction.

### 8. ADR-047 remains mostly valid, but only for `StringRef` creation

ADR-047's user-facing goal still stands:

- one clear maximum size for immutable string values.

After this ADR, that limit applies to `StringRef` creation points:

- literals,
- `s.pack`,
- and any retained convenience constructors that still produce immutable
  strings.

Future PAD/buffer mechanisms may have their own storage-capacity controls.
Those are buffer concerns, not StringRef semantics.

### 9. Snapshot model simplifies

In the target model:

- `StringRef` values persist as ordinary immutable heap objects,
- snapshot writers do not need language-visible transient-string rejection
  paths,
- and the intern table is not authoritative serialized state.

This is a simplification compared with ADR-043.

## Consequences

- The user model gets simpler: immutable text is one thing, working bytes are
  another.
- Froth keeps one of its best readability wins: one-slot text values for
  literals and printed labels.
- The main rationale for language-visible transient strings disappears once
  literal interning exists.
- REPL and tooling formatting should use internal scratch buffers rather than
  manufacturing transient language values.
- Future byte/PAD work becomes necessary, but it is now a clearly bounded
  follow-on problem rather than a reason to overload `StringRef`.
- Existing ADR-043 behavior may remain temporarily for workshop practicality,
  but it should be treated as transitional once this ADR is accepted.

## Non-goals

- This ADR does not define the final PAD vocabulary.
- This ADR does not define the final byte-buffer profile surface.
- This ADR does not add byte-addressed access to ADR-054 CellSpace.
- This ADR does not add Unicode semantics beyond the current "bytes, commonly
  UTF-8" stance.
- This ADR does not require immediate removal of the current workshop string
  implementation before the replacement exists.

## References

- `docs/archive/spec/Froth_Language_Spec_v1_1.md`
- `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md`
- `docs/archive/adr/023-string-lite-heap-layout.md`
- `docs/archive/adr/024-native-address-profile.md`
- `docs/archive/adr/032-mark-release-heap-watermark.md`
- `docs/archive/adr/043-transient-string-buffer.md`
- `docs/archive/adr/046-number-to-string-primitives.md`
- `docs/archive/adr/047-unified-string-length-limit.md`
- `docs/adr/054-first-froth-cellspace-profile.md`
