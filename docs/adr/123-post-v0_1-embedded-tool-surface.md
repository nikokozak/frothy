# Frothy ADR-123: Post-v0.1 Embedded Tool Surface

**Date**: 2026-04-15
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 2, 3, 7, 8, 9, 10
**Roadmap queue item**: post-`v0.1` embedded tool surface tranche
**Related ADRs**: `docs/adr/108-frothy-ffi-boundary.md`, `docs/adr/121-workshop-base-image-board-library-surface.md`

## Context

Frothy is no longer in the phase where the accepted `v0.1` spec can be treated
as the whole user-facing ceiling of the language.

That spec is still the correct authority for the core model:

- stable top-level slots
- lexical locals
- explicit mutation
- image-oriented save / restore / dangerous.wipe
- non-capturing `Code`
- ordinary FFI calls through the base image

But current Frothy now needs a more serious embedded-programming feel:

- small math helpers that make range work obvious instead of clever
- integer-only random helpers that fit the no-floats profile cleanly
- short, discoverable names that feel useful to novices and not insulting to
  experienced users

Without that distinction, `v0.1` gets misread as "do not widen the language
surface," even when the widening is a narrow base-image helper/tooling cut that
does not reopen the core semantic model.

## Options Considered

### Option A: Keep treating `v0.1` as the whole active ceiling

Leave the authority prose as-is and continue to treat helper growth as ad hoc
workshop polish.

Trade-offs:

- Pro: no new framing work.
- Con: makes Frothy look artificially smaller than it now is.
- Con: discourages obvious embedded-helper additions.
- Con: keeps the language/tooling story feeling like a proof harness.

### Option B: Treat `v0.1` as the semantic floor and grow the embedded tool surface deliberately

Keep `v0.1` authoritative for the core model, but accept that post-`v0.1`
Frothy can and should widen the maintained base-image/tooling surface through
small reviewable tranches.

Trade-offs:

- Pro: keeps the language core stable.
- Pro: makes room for practical embedded helpers now.
- Pro: gives the repo a truthful authority story.
- Con: requires a small amount of control-doc clarification.

### Option C: Replace `v0.1` with a full new language spec first

Rewrite the whole active spec before adding more helpers.

Trade-offs:

- Pro: one clean future authority document.
- Con: too large for the actual decision at hand.
- Con: risks reopening semantics that are already correct.
- Con: delays useful tooling behind a documentation rewrite.

## Decision

**Option B.**

Frothy `v0.1` remains the authoritative statement of the core semantic model.
It is the floor, not the whole present-day product ceiling.

Post-`v0.1` Frothy may widen the maintained user-facing surface through
accepted helper/tooling tranches so long as they do not violate the core model.

The first embedded-tool-surface tranche is:

- math helpers in the base image:
  `math.abs`, `math.min`, `math.max`, `math.clamp`, `math.mod`,
  `math.wrap`, `math.map`, and `math.mapClamped`
- short aliases for the helpers:
  `abs`, `min`, `max`, `clamp`, `mod`, `wrap`, `map`, and `mapClamped`
- integer random helpers in the base image:
  `random.seed!`, `random.seedFromMillis!`, `random.next`,
  `random.below`, and `random.range`
- short aliases for the random surface:
  `rand`, `rand.below`, `rand.range`, `rand.seed!`,
  and `rand.seedFromMillis!`

Naming policy held by this tranche:

- dotted family names are the canonical public spelling for grouped helper
  surfaces, but they are still ordinary stable top-level slots, not a second
  namespace
- family prefixes are for domainful clusters: stateful helpers, hardware
  capabilities, or related operations that should inspect and teach as one
  bundle
- family heads should stay short and concrete such as `math`, `random`,
  `gpio`, `adc`, and `led`, not vague buckets like `util`
- bare aliases are reserved for very common pure transforms where the shorter
  spelling improves readability and collision risk stays low
- because of that rule, math helpers may ship both `math.*` and bare aliases,
  while stateful or capability words should not ship as bare generic names
  like `seed`, `range`, `read`, or `write`
- shortened family aliases such as `rand.*` are acceptable when they keep the
  domain cue while making common code lighter to type
- reference docs should teach canonical family names first and list short
  aliases as convenience rather than as the primary naming surface

Rules held by this tranche:

- no new value classes
- no floats
- no closure or persistence-model changes
- no hidden overlay objects just to support helper convenience
- random state is runtime/native state and is therefore not saved
- range mapping stays integer-only and therefore truncates through existing
  integer division rules

## Consequences

- Frothy can now grow the parts that make it feel custom-made for embedded
  programming without pretending every such change is a semantic rewrite.
- The repo authority story becomes clearer: the stable core lives in `v0.1`,
  while serious embedded helpers can still land as accepted post-`v0.1`
  surface work.
- The native boundary stays narrow: most helper growth remains Frothy-native in
  `base.frothy`, with only a small integer random surface added natively.
- The naming story becomes more deliberate: Frothy can present grouped helper
  families without pretending they are module objects or reopening the
  one-namespace rule.
- Future helper additions should continue to be small, reviewable, and
  explicitly embedded-first rather than turning into a grab bag of unrelated
  convenience words.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/spec/README.md`
- `docs/roadmap/Frothy_Embedded_Tool_Surface_Tranche_1.md`
- `boards/posix/lib/base.frothy`
- `boards/esp32-devkit-v1/lib/base.frothy`
- `boards/esp32-devkit-v4-game-board/lib/base.frothy`
