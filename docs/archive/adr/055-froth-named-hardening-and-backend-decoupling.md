# ADR-055: Harden FROTH-Named by Decoupling User Semantics from Literal `perm` Lowering

**Date**: 2026-04-02
**Status**: Proposed
**Spec sections**: `docs/archive/spec/Froth_Language_Spec_v1_1.md` Section 8 (FROTH-Named), Section 13 (FROTH-Perf), Section 6 (errors)
**Related ADRs**: ADR-018 (`: ;` sugar), ADR-040 (CS trampoline executor), ADR-049 (`times.i`), ADR-050 (first staged FROTH-Named implementation), ADR-051 (binding-intent primitives), ADR-054 (FROTH-CellSpace)

## Context

ADR-050 landed a first shippable FROTH-Named slice on the plain `:` surface.
That was the right move. It proved that:

- semantic `( ... -- ... )` headers on plain `:` definitions are viable,
- named straight-line helpers materially improve readability,
- and the language can lower name references without introducing closures or
  heap allocation.

But ADR-050 deliberately shipped a **narrow** first pass:

- named references lower directly to generated `perm` sequences,
- the compiler tracks a preserved entry frame literally on DS,
- and named mode rejects dynamic/effect-unknown operations such as raw `call`,
  `catch`, and user-authored `perm`.

That first-pass restriction was honest and useful. It is also now the next
language ceiling.

The problem is not the **user model**. The user model is good:

- names are labels on entry-stack values,
- not mutable locals,
- not hidden heap objects,
- and not closures.

The problem is that the current implementation strategy has started to leak
through the abstraction:

- definitions reject because of backend-specific depth/window constraints,
- `FROTH_MAX_PERM_SIZE` acts like a language ceiling rather than a codegen knob,
- and the difference between "the named semantics" and "the current lowering"
  is too blurred.

The TM1629 pass made that visible:

- straight-line named helpers got better,
- but more dynamic or stateful helpers still had to fall back to plain stack
  style.

The path forward is not to abandon FROTH-Named semantics in favor of
conventional mutable locals. It is to separate:

1. the **language contract** users write against, and
2. the **backend strategy** the compiler uses to realize that contract.

Constraints:

1. Keep plain `:` as the definition surface.
2. Keep named inputs as read-only aliases for entry values.
3. Keep nested quotation capture illegal.
4. Introduce no hidden heap allocation.
5. Preserve snapshot-safe canonical representations.
6. Widen the usable subset of named code without turning FROTH-Named into a
   different language.

## Options Considered

### Option A: Keep ADR-050 first pass as the long-term model

Treat literal `perm` emission and the current preserved-entry-frame DS model as
the defining implementation of FROTH-Named.

Trade-offs:

- Pro: simple and honest.
- Pro: minimal runtime machinery.
- Pro: directly reflects the current spec text.
- Con: backend limits become user-visible language limits.
- Con: `FROTH_MAX_PERM_SIZE` and direct-DS frame preservation remain semantic
  blockers.
- Con: too many ordinary definitions still get pushed back into plain stack
  style.

### Option B: Replace FROTH-Named with conventional locals

Turn names into a conventional locals system with dedicated mutable frames and
potential future capture rules.

Trade-offs:

- Pro: easy for users coming from non-concatenative languages.
- Pro: can accept a wider class of programs quickly.
- Con: changes the meaning of the feature rather than fixing its current
  implementation ceiling.
- Con: moves Froth away from "labels on stack values" and toward a second,
  different programming model.
- Con: invites further pressure for mutable locals, capture, and closure-like
  features.

### Option C: Preserve the current user model, but make lowering backend-agnostic

Keep the current surface and semantics, but insert an explicit compiler/backend
boundary:

- names lower first to a backend-independent internal form,
- backends may realize that form via literal `perm`, fused micro-ops, or a
  fixed non-allocating read-only frame,
- and the language contract remains "named entry values," not "this exact
  sequence of `perm` patterns."

Trade-offs:

- Pro: preserves the current semantics while removing accidental dependence on
  one lowering strategy.
- Pro: lets the implementation accept more useful programs without adding heap
  allocation or mutable locals.
- Pro: keeps `perm` important without making it the only backend path.
- Con: compiler/runtime complexity increases.
- Con: requires a disciplined story for canonical persistence versus backend
  caches.
- Con: some effect-unknown operations still need rejection or explicit future
  escape hatches.

## Decision

**Option C: keep FROTH-Named semantics, decouple them from literal `perm`
emission, and harden the feature by widening the accepted subset through
backend freedom rather than surface redesign.**

The key line is:

- **the user model is normative**
- **the current lowering strategy is not**

### 1. Preserve the existing FROTH-Named user model

This ADR keeps the language-level contract established by the spec and ADR-050:

- plain `:` remains the only surface,
- `( in1 in2 -- out1 )` remains the binding syntax,
- names are **read-only aliases** for entry-stack values,
- names shadow slots inside the body,
- `'name call` remains the explicit slot-call escape when a slot must be named
  under a shadowing collision,
- names do not capture into nested quotations,
- no hidden heap allocation is introduced.

This ADR is therefore not a redefinition of FROTH-Named. It is a hardening and
backend-decoupling ADR.

### 2. Introduce an explicit backend boundary for named lowering

The compiler pipeline should become:

1. surface `:` definition with semantic signature
2. backend-independent **named compile form**
3. backend-specific executable form

The current implementation effectively jumps straight from surface syntax to
literal generated `perm`.

That is too direct. The compiler should first produce a canonical named form
that captures:

- entry-name loads,
- known-arity calls,
- structured control,
- output-count expectations,
- and cleanup requirements,

without committing to how those operations are realized at runtime.

The exact internal encoding is an implementation detail. The architectural
constraint is not:

- backend-independent compile form first,
- backend-specific realization second.

### 3. Literal `perm` is one backend, not the semantic definition

Valid backend strategies include:

- literal `perm` sequences,
- fused internal shuffle micro-ops,
- peephole-compacted shuffle sequences,
- a fixed read-only named frame with explicit loads,
- or mixed strategies chosen per definition.

The visible semantics are authoritative:

- DS behavior at word boundaries,
- signature-based output checking,
- no name capture,
- and no heap allocation.

If a backend can satisfy those constraints, it is valid.

### 4. The reference implementation should prefer a bounded, non-user-visible frame

The current first pass preserves the entry frame literally on DS. That is what
causes many avoidable compile-time rejections.

The recommended next implementation step is:

- a **bounded, fixed-size named frame**
- stored separately from the user-visible return stack
- enabled only when FROTH-Named is in use

Why not consume RS invisibly by default?

- RS is already user-visible in Froth.
- Hidden RS use creates hard-to-explain interactions with `>r`, `r>`, and `r@`.

Why a fixed frame?

- no heap allocation,
- predictable RAM cost,
- and name loads no longer depend on preserving the original entry frame
  literally on DS.

Tiny targets that do not want this extra machinery may still use a direct-`perm`
backend for the subset they choose to support.

### 5. Widen the accepted named subset, but keep effect discipline

The current named compiler rejects many definitions for reasons that are partly
semantic and partly backend-specific.

This ADR keeps the semantic restrictions and attacks the backend-specific ones.

#### What should improve immediately

Named definitions should no longer be rejected merely because:

- a name reference would require a deeper duplication than the current
  `FROTH_MAX_PERM_SIZE`,
- a known-arity call would consume below the old "live entry frame on DS"
  model,
- or literal `perm` emission would be noisy even though the semantics are
  straightforward.

In other words:

- `FROTH_MAX_PERM_SIZE` becomes a backend/codegen knob,
- not the language ceiling for named code.

#### What remains intentionally constrained

This ADR does **not** make named mode magical.

Effect-unknown operations remain a real issue. Therefore:

- unknown-arity dynamic `call` remains illegal in named mode,
- `catch` remains outside the first hardening tranche unless its effect is made
  statically modelable,
- raw user-authored `perm` remains outside the first hardening tranche unless
  the compiler can explicitly account for it,
- nested quotation capture remains illegal.

This ADR widens the subset by removing accidental backend constraints. It does
not abolish effect discipline.

### 6. Canonical persistence must remain backend-independent

Backend-specific executable forms are caches, not the canonical persisted
representation.

That means:

- snapshots must persist a backend-independent named representation,
- restore must be able to rebuild the chosen backend form from that canonical
  representation,
- backend caches must be invalidated or rebuilt the same way other optimized
  callable forms are handled in FROTH-Perf.

This ADR intentionally does **not** require a snapshot-format redesign in the
same change. It requires the implementation to preserve a canonical,
backend-independent representation rather than serializing opaque backend
frames.

### 7. No new user-facing syntax in this ADR

This hardening pass explicitly does **not** add:

- `:n`
- mutable locals syntax
- `let`
- closure capture
- `IF/ELSE/THEN` syntax
- explicit `raw[` ... `]` named-mode escape regions

Those may be worth discussing separately. They are not required to fix the
current problem.

### 8. Sequencing: pair this with CellSpace, but do not conflate them

ADR-054 and this ADR solve different failures:

- ADR-054 fixes the missing mutable aggregate data model,
- ADR-055 fixes the current named-profile implementation ceiling.

They should be treated as a paired language-correction track:

1. land CellSpace so real data structures exist,
2. then harden FROTH-Named so readable code around those data structures is not
   trapped inside the current narrow subset.

The proof point should be another TM1629-style rewrite:

- same library goal,
- using CellSpace for the framebuffer/state layout,
- and using hardened FROTH-Named for readable straight-line logic without
  visible backend leakage.

## Consequences

- FROTH-Named keeps its current identity as "readable stack code," not a second
  mutable-locals language.
- Backend freedom increases materially. The implementation can choose the
  smallest or most robust strategy per target/profile.
- The compiler becomes more complex, but the complexity is in the right place:
  behind the user model, not exposed as user-facing ceremony.
- `perm` remains a core user/tooling concept, but no longer has to carry the
  full weight of every named definition at runtime.
- Snapshot/caching discipline becomes more important, because backend-specific
  forms must remain rebuildable from a canonical representation.
- The path toward later named optimizations becomes cleaner: post-hardening
  `perm` fusion and fixed-frame tuning are optimization passes, not semantic
  redesigns.

## Non-goals

- No mutable locals profile
- No closure capture
- No hidden heap allocation
- No replacement of plain `:` with a new surface
- No claim that CellSpace and named hardening are one combined feature
- No promise that all effect-unknown operations become legal in named mode

## References

- `docs/archive/spec/Froth_Language_Spec_v1_1.md`
- `docs/archive/adr/050-staged-first-froth-named-implementation.md`
- `docs/archive/adr/051-binding-intent-primitives.md`
- `docs/adr/054-first-froth-cellspace-profile.md`
- `tests/legacy/tm1629d/README.md`
