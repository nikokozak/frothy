# Frothy M9 Board FFI Closeout

Status: closed on 2026-04-12
Milestone: `M9 Board FFI surface`
Primary proof command: `ctest -R frothy_ffi`
Authority: `docs/spec/Frothy_Language_Spec_v0_1.md`, `docs/adr/108-frothy-ffi-boundary.md`, `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`

## Purpose

This note closes M9 by naming the shipped Frothy board-facing base image,
tying it to the checked-in proof layers, and freezing the intended boundary
before M10 starts proving real device programs.

The accepted Frothy rule remains:

- expose foreign bindings as ordinary Frothy `Code` values
- keep the user-facing call style value-oriented
- reuse the inherited Froth registration and native entrypoints internally
  where that keeps `v0.1` smaller

That is the ADR-108 boundary this milestone was supposed to land.

## Shipped Surface

The shipped `v0.1` board-facing base image is intentionally narrow.

Bindings:

- `gpio.mode`
- `gpio.write`
- `ms`
- `adc.read`
- `uart.init`
- `uart.write`
- `uart.read`

Seeded board constants:

- `LED_BUILTIN`
- `UART_TX`
- `UART_RX`
- `A0`

The runtime installs only that Frothy-facing surface from the broader inherited
board table. The filter lives in `src/frothy_ffi.c` and keeps the milestone
boundary explicit instead of implicitly inheriting every older board primitive.

## Proof Mapping

| Surface slice | Proof layer | What it proves |
|---|---|---|
| Frothy shim arity, type conversion, bool coercion, nil returns, stack cleanup, and `Cells` rejection | `ctest -R frothy_ffi` | The value-oriented Frothy shim stays narrow, rejects unsupported value classes, and reuses the inherited entrypoints without leaking stack machinery into the user model |
| Base-image names and host board behavior for `gpio.mode`, `gpio.write`, `ms`, `adc.read`, and `uart.*` | `tools/frothy/proof_m9_ffi_smoke.sh` | The shipped bindings and seeded constants are visible from `words()`, report as base/native/foreign through `slotInfo()`, and exercise the host proof target end-to-end |
| Exercised ESP32 smoke path for GPIO and one input primitive | `tools/frothy/proof_m9_esp32_ffi_smoke.py --port <PORT>` | The real ESP32 image reaches the Frothy prompt and exercises the same narrow board surface with `gpio.mode(LED_BUILTIN, 1)`, `gpio.write(...)`, `ms(...)`, and `adc.read(A0)` |

## Exit Statement

M9 is closed because all of the accepted milestone conditions are now true:

- the Frothy FFI shim is landed
- the user-facing board surface is exposed as base-image native `Code`
  bindings plus seeded constants
- the proof target is focused on `ctest -R frothy_ffi`
- the exercised ESP32 smoke path stays aligned with the same narrow surface

This is enough to move on to M10.

## Non-Goals Held

The milestone did not broaden beyond the accepted boundary.

Not part of M9:

- no native ABI redesign
- no broader board surface than the shipped list above
- no `gpio.read` in the Frothy-facing `v0.1` base image
- no persistable foreign handles
- no shift away from ADR-108’s rule that internal reuse of inherited Froth
  substrate is acceptable while the user-facing Frothy model remains
  value-oriented

## Handoff To M10

M10 should build on this exact board surface rather than widening it.

The first hardware-proof bundle should prove:

- a readable blink program
- saved boot behavior that reruns on restart
- a cells-backed ADC capture sketch

If M10 slips, cut demo breadth before widening the surface.
