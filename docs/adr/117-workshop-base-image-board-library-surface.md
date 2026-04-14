# Frothy ADR-117: Workshop Base-Image Board/Library Surface

**Date**: 2026-04-14
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 4, 8, 10 and Appendix C
**Roadmap queue item**: `Workshop base-image library and board surface`
**Related ADRs**: `docs/adr/108-frothy-ffi-boundary.md`, `docs/adr/107-interactive-profile-boot-and-interrupt.md`

## Context

The workshop path needs more than the narrow M9 board FFI names, but it still
must preserve the accepted Frothy boundary:

- keep the native ABI small
- keep the workshop surface Frothy-native where possible
- keep the preflashed library in the base image so it survives `wipe()`
- do not reopen the native boundary or the persistence model first

The post-`v0.1` queue item for workshop board/library work specifically calls
for blink, animation, `millis`, ADC, GPIO, and related helpers.

## Decision

The first workshop base-image cut is:

- add `millis()` as a native base-image uptime read backed by
  `platform_uptime_ms()`, wrapped into the existing immediate integer range
- widen the native board surface by only one more GPIO primitive:
  `gpio.read(pin)`
- keep existing native board bindings unchanged:
  `gpio.mode`, `gpio.write`, `ms`, `adc.read`, `uart.*`, and seeded pins
- add a Frothy-native preflashed workshop library loaded from
  `boards/<board>/lib/workshop.frothy`

The shipped workshop base-library names are:

- `blink(pin, count, wait)`
- `animate(count, wait, step)`
- `led.pin`
- `led.on()`, `led.off()`, `led.toggle()`, `led.blink(count, wait)`
- `gpio.input(pin)`, `gpio.output(pin)`, `gpio.high(pin)`, `gpio.low(pin)`,
  `gpio.toggle(pin)`
- `adc.max`
- `adc.percent(pin)`

## Base/Overlay Rule

The workshop library is seeded as base image, not as overlay.

Implementation rule:

- the embedded workshop source is evaluated during `frothy_base_image_install()`
- base-image seeding runs with `boot_complete` cleared so top-level writes are
  marked base rather than overlay
- the base-image code keeps an explicit list of workshop slot names so reset and
  `wipe()` reinstall them even after overlay redefinition

User-visible consequence:

- redefining `blink` or `adc.percent` in the session is allowed
- `wipe()` restores the preflashed workshop definitions

## Held Boundary

This tranche does not:

- redesign the native ABI
- add new handle/value classes
- calibrate ADC into volts or board-specific physical units
- widen into PWM, I2C, UART teaching helpers, or board-specific sensor APIs
- migrate inherited `boards/**/lib/board.froth` into product surface by default

## Proof

Host proof stays mandatory:

- `cmake -S . -B build && cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `sh tools/frothy/proof_m10_smoke.sh --host-only`

Board proof remains the exact workshop command path:

- `sh tools/frothy/proof_m10_smoke.sh --assume-blink-confirmed <PORT>`

The proof ladder must show:

- `millis()` is base/native and monotonic across `ms(...)`
- `gpio.read` round-trips on host and board
- `blink` and `adc.percent` are base/code slots
- overlay redefinition is removed by `wipe()`
- the preflashed workshop library survives reset/wipe as part of the base image
