# Frothy Embedded Tool Surface Tranche 1

Status: landed initial tranche
Date: 2026-04-15
Authority: `docs/spec/Frothy_Language_Spec_v0_1.md`, `docs/adr/108-frothy-ffi-boundary.md`, `docs/adr/121-workshop-base-image-board-library-surface.md`, `docs/adr/123-post-v0_1-embedded-tool-surface.md`

## Purpose

This note makes one point explicit:

Frothy has moved past treating the accepted `v0.1` spec as the whole
user-facing ceiling of the language.

`v0.1` is still the stable semantic floor.
It is not a reason to keep the embedded-programming surface tiny forever.

Frothy now needs a serious small-tool surface that feels:

- embedded-first
- powerful
- easy to teach
- pleasant for both novices and experienced programmers

## Landed Surface

The first landed embedded-tool tranche adds these base-image helpers on the
maintained board paths:

- `math.abs`
- `math.min`
- `math.max`
- `math.clamp`
- `math.mod`
- `math.wrap`
- `math.map`
- `math.mapClamped`
- `math.inRange?`
- `math.sign`
- `math.approach`
- `math.deadband`
- `random.seed!`
- `random.seedFromMillis!`
- `random.next`
- `random.byte`
- `random.below`
- `random.range`
- `random.chance?`
- `random.percent?`

Short aliases are also shipped:

- `abs`
- `min`
- `max`
- `clamp`
- `mod`
- `wrap`
- `map`
- `mapClamped`
- `inRange?`
- `sign`
- `approach`
- `deadband`
- `rand`
- `rand.byte`
- `rand.below`
- `rand.range`
- `rand.chance?`
- `rand.percent?`
- `rand.seed!`
- `rand.seedFromMillis!`

## Why These First

These words solve the most immediate "embedded code should feel easy" gaps:

- range remapping without hand-rolled arithmetic in every sketch
- cyclic wraparound without making users remember remainder edge cases
- clamp/min/max helpers for bounds and screen/input work
- integer random helpers that match the no-floats profile honestly

This is the right kind of widening:

- small
- obviously useful
- native only where truly needed
- Frothy-native everywhere else

## Naming Rule

This tranche treats dotted helper families as a language-design tool, not as a
runtime namespace feature.

- canonical names use short dotted family prefixes such as `math.*`,
  `random.*`, `gpio.*`, and `adc.*`
- those prefixes group related surface area for teaching, inspection, and
  future growth, but runtime still remains one flat stable-slot image
- bare aliases are reserved for common pure transforms where the short form is
  genuinely clearer: `map`, `clamp`, `mod`, `wrap`, `min`, `max`, `abs`
- stateful or capability-oriented words should keep a family cue; that is why
  Frothy ships `random.*` and `rand.*`, not bare `seed`, `range`, or `next`
- future helper growth should prefer concrete family heads over vague buckets:
  `led.*`, `gpio.*`, `matrix.*`, `joy.*`, not `util.*`

## Held Boundary

This tranche does not:

- replace the accepted `v0.1` semantic core
- add floats
- add collections beyond existing `Cells`
- reopen persistence rules
- claim that the remaining `vNext` structural draft is already landed

## Proof

Focused host proof:

- `cmake -S . -B build && cmake --build build`
- `ctest --test-dir build --output-on-failure -R 'frothy_ffi|frothy_snapshot|frothy_tm1629_board'`

Small manual real-device spot-check:

- after flashing the maintained ESP32 path, verify these prompt checks manually:
  `map: 5, 0, 10, 0, 100`
  `mod: -1, 8`
  `rand.seed!: 7`
  `rand.range: 3, 7`

## Next Constraint

Future helper growth should continue to feel like one intentional embedded tool
surface, not an unreviewed pile of convenience names.
