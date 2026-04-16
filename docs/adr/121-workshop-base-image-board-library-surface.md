# Frothy ADR-121: Workshop Base-Image Board/Library Surface

**Date**: 2026-04-14
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 4, 8, 10 and Appendix C
**Roadmap queue item**: `Workshop base-image library and board surface`
**Related ADRs**: `docs/adr/108-frothy-ffi-boundary.md`, `docs/adr/107-interactive-profile-boot-and-interrupt.md`, `docs/adr/119-tm1629-board-base-surface-and-registry.md`, `docs/adr/123-post-v0_1-embedded-tool-surface.md`

## Context

The workshop path needs more than the narrow M9 board FFI names, but it still
must preserve the accepted Frothy boundary:

- keep the native ABI small
- keep the workshop surface Frothy-native where possible
- keep the preflashed library in the base image so it survives `wipe()`
- do not reopen the native boundary or the persistence model first

The post-`v0.1` queue item for workshop board/library work specifically calls
for blink, animation, `millis`, ADC, GPIO, display helpers, input helpers, and
one truthful shipped demo-board behavior.

## Decision

The maintained workshop base-image cut is:

- add `millis()` as a native base-image uptime read backed by
  `platform_uptime_ms()`, wrapped into the existing immediate integer range
- widen the native board surface by only one more GPIO primitive:
  `gpio.read(pin)`
- keep existing native board bindings unchanged:
  `gpio.mode`, `gpio.write`, `ms`, `adc.read`, `uart.*`, and seeded pins
- load the Frothy-native workshop library from `boards/<board>/lib/base.frothy`
  as base image

The shipped workshop base-library names are:

- `blink(pin, count, wait)`
- `animate(count, wait, step)`
- `led.pin`
- `led.on()`, `led.off()`, `led.toggle()`, `led.blink(count, wait)`
- `gpio.input(pin)`, `gpio.output(pin)`, `gpio.high(pin)`, `gpio.low(pin)`,
  `gpio.toggle(pin)`, `gpio.high?(pin)`, `gpio.low?(pin)`, `gpio.pulse(pin, wait)`
- `adc.max`
- `adc.percent(pin)`
- `math.abs`, `math.min`, `math.max`, `math.clamp`, `math.mod`, `math.wrap`,
  `math.map`, `math.mapClamped`, `math.inRange?`, `math.sign`,
  `math.approach`, `math.deadband`
- `abs`, `min`, `max`, `clamp`, `mod`, `wrap`, `map`, `mapClamped`,
  `inRange?`, `sign`, `approach`, `deadband`
- `random.seed!`, `random.seedFromMillis!`, `random.next`,
  `random.byte`, `random.below`, `random.range`, `random.chance?`,
  `random.percent?`
- `rand`, `rand.byte`, `rand.below`, `rand.range`, `rand.chance?`,
  `rand.percent?`, `rand.seed!`, `rand.seedFromMillis!`
- `tm1629.raw.*`, `tm1629.*`, `matrix.*`, `grid.*`, `joy.*`, `knob.*`

The shipped demo-board namespace is:

- `demo.pong.*`
- `boot`

Naming rule for this shipped base-library surface:

- Frothy still has one namespace of stable slots; dotted names are grouping
  conventions, not module objects
- board and capability surfaces stay prefixed so the domain stays visible:
  `gpio.*`, `led.*`, `adc.*`, `tm1629.*`, `matrix.*`, `joy.*`, `knob.*`
- pure ubiquitous integer helpers may also expose bare aliases, which is why
  `math.*` ships `map`, `mod`, `clamp`, `wrap`, `min`, `max`, and `abs`
- random keeps a short family alias `rand.*` instead of bare generic names so
  sketches stay short without losing the stateful-domain cue

The tiny public workshop repo is exported from that same canonical
`base.frothy` source. It is not an independently authored second demo source.

## Base/Overlay Rule

The workshop library and shipped Pong demo are seeded as base image, not as
overlay.

Implementation rule:

- the embedded workshop source is evaluated during `frothy_base_image_install()`
- base-image seeding runs with `boot_complete` cleared so top-level writes are
  marked base rather than overlay
- board-owned base names are captured at install time so reset and
  `dangerous.wipe` reinstall them even after overlay redefinition

User-visible consequence:

- redefining `blink`, `matrix.init`, or `demo.pong.frame` in the session is
  allowed
- `dangerous.wipe` restores the preflashed workshop definitions and shipped
  Pong demo

## Held Boundary

This tranche does not:

- redesign the native ABI
- add new handle/value classes
- calibrate ADC into volts or board-specific physical units
- widen into PWM, I2C, UART teaching helpers, or board-specific sensor APIs
- add a second workshop runtime mode or an independent starter scaffold

## Proof

Host proof stays mandatory:

- `cmake -S . -B build && cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `sh tools/frothy/export_workshop_repo.sh check`

Board proof remains the exact workshop command path:

- `sh tools/frothy/proof.sh workshop-v4 <PORT>`

Use `--live-controls` only for an explicit manual joystick/button extension of
that proof.

The proof ladder must show:

- `millis()` is base/native and monotonic across `ms(...)`
- `gpio.read` round-trips on host and board
- board helpers and `demo.pong.*` are base-owned slots
- overlay redefinition is removed by `dangerous.wipe`
- the preflashed workshop library and shipped Pong demo survive reset/wipe as
  part of the base image
