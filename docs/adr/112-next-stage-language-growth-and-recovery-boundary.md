# Frothy ADR-112: Next-Stage Language Growth And Recovery Boundary

**Date**: 2026-04-12
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 2, 3, 7, 8, Appendix C; `docs/spec/Frothy_Language_Spec_vNext.md`
**Roadmap milestone(s)**: post-M10 follow-on language definition
**Inherited Froth references**: `docs/archive/adr/049-indexed-counted-iteration.md`, `docs/archive/adr/045-catch-truth-convention.md`

## Context

The accepted Frothy `v0.1` core is now real enough to expose its next limits.

What is still missing is not only syntax polish.
The language is still weak at:

- counted iteration,
- named structured data,
- explicit library encapsulation,
- stronger multi-branch control flow,
- and a Frothy-native structured recovery form that fits Frothy's slot-and-image
  model rather than inherited Froth's stack-visible `catch`/`throw` model.

At the same time, the current active follow-on work is still helper/control
hardening, not a reopened runtime rewrite.

The repo therefore needs one explicit decision about the next language design
boundary before implementation scope drifts.

## Options Considered

### Option A: Keep the next step limited to surface syntax cleanup

Land only the already-proposed `here`, top-level named function sugar, and REPL
inspection sugar.

Trade-offs:

- Pro: cheap and low-risk.
- Pro: keeps the evaluator unchanged.
- Con: does not solve the main remaining expressiveness gaps.
- Con: leaves Frothy looking cleaner but still thin for real library work.

### Option B: Reopen many semantic fronts at once

Treat the next step as a broad language expansion: records, modules, closures,
`try/catch`, and richer abstraction layers together.

Trade-offs:

- Pro: ambitious feature jump.
- Con: too much semantic churn at once.
- Con: risks breaking the small explainable center of Frothy.
- Con: likely collides with the active helper/control follow-on and obscures
  proof discipline.

### Option C: Stage the next language work around a small set of high-value additions

Keep the active helper/control artifact intact, but define the next language
design tranche around:

- indexed counted iteration,
- short-circuit and multi-branch control,
- fixed-layout records,
- module images built from stable slots,
- Frothy-native `try/catch` with named error values and explicit
  non-transactional semantics,
- and an explicit statement that top-level shell/control/boot recovery remains
  the outer boundary even after in-language `try/catch` exists.

Trade-offs:

- Pro: addresses the most important expressiveness gaps directly.
- Pro: preserves the accepted slot/image model.
- Pro: keeps the next-step language design small enough to review.
- Pro: gives the roadmap a concrete language artifact without pretending the
  active helper work vanished.
- Con: defers closures and in-language catch again.
- Con: still requires careful syntax and persistence design work before code.

## Decision

**Option C.**

The next Frothy language tranche is defined by five priorities:

1. indexed counted iteration
2. short-circuit and multi-branch control
3. fixed-layout records
4. module images built from stable slots
5. Frothy-native `try/catch` with named error values and no implicit rollback
6. a clearer recovery-boundary story without reintroducing Froth's global
   stack-visible catch model

Boundary rules:

- the accepted `v0.1` contract remains authoritative for current behavior
- current helper/control broadening remains the active follow-on artifact
- next-stage language work should land first as a draft spec plus timeline
  entries before runtime implementation widens
- modules must preserve stable slot identity
- records must be fixed-layout and persistence-friendly
- Frothy-native `try/catch` is part of the next-stage language definition
- caught errors should be named values rather than anonymous raw integers if
  records are available in the same tranche
- in-language recovery remains additive and explicitly non-transactional unless
  a later ADR says otherwise
- parse, restore, and startup failures remain top-level boundary failures in
  the first step
- interrupt and reset remain boundary-control events in the first step

## Consequences

- The repo now has one explicit next language target beyond surface cleanup.
- Records and modules are both treated as necessary, but for different reasons:
  records for named data shape, modules for library encapsulation.
- The absence of a Froth-style global `catch` is now an intentional Frothy
  boundary, while a Frothy-native `try/catch` is part of the planned next
  language tranche instead of being left vague.
- Language implementation work is gated on design docs rather than implied by
  scattered discussion.
- Later work can still add explicit environments, richer module loading, or a
  Frothy-native `attempt` / `catch` surface, but only on top of this smaller
  semantic center.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`
- `docs/spec/Frothy_Language_Spec_vNext.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
- `PROGRESS.md`
- `TIMELINE.md`
