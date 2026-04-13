# ADR-057: Runtime UART Console Rebinding via Autorun

**Date**: 2026-04-05
**Status**: Accepted
**Spec sections**: `docs/spec/Froth_Interactive_Development_v0_5.md` (Direct Mode, Live Mode, boot behavior)
**Related ADRs**: ADR-028 (board/platform architecture), ADR-034 (console multiplexer architecture), ADR-048 (exclusive live session transport), ADR-053 (first board-level I2C/UART integration surface)

## Context

Froth's current console model assumes one fixed platform-owned console path:

- `platform_emit`
- `platform_emit_raw`
- `platform_key`
- `platform_key_ready`
- `platform_check_interrupt`

On ESP32 today that path is effectively UART0 in
`platforms/esp-idf/platform.c`, and ADR-053 reinforced that split by keeping
board-level `uart.init` on auxiliary UARTs only.

That was correct for the first UART tranche, but it is too rigid for the next
hardware step. The typewriter project needs the **main Froth console** to move
at runtime to another UART path such as:

- UART1 or UART2 on different pins
- a different baud rate

Two constraints matter:

1. This should be ordinary Froth behavior, not a compile-time build variant.
2. Persistence should reuse Froth's existing model (`autorun` + `save`), not a
   second platform-owned configuration store.

The current docs are in tension here:

- ADR-053 says ESP32 user UART stays off the console transport.
- ADR-048 says the active console/Live transport must remain fail-closed and
  unambiguous.
- `src/froth_boot.c` already gives a clean persistence hook because safe boot
  happens before restore and `autorun`.

The decision is therefore not "should ESP32 gain a special case?" It is "what
is the smallest general console-rebinding model that preserves Froth's current
transport guarantees?"

## Options Considered

### Option A: Compile-time console selection

Choose the console UART, pins, and baud at build time.

Trade-offs:

- Pro: straightforward implementation.
- Pro: no runtime state management.
- Con: not live.
- Con: does not use Froth's own persistence model.
- Con: turns a device-level behavior change into build-system policy.
- Con: does not even solve the immediate runtime UART use case.

### Option B: Runtime rebinding with platform-owned config persistence

Allow live console switching, but persist the selected endpoint in NVS or other
platform storage outside Froth snapshots.

Trade-offs:

- Pro: live switching works.
- Pro: boot can apply the chosen console before `autorun`.
- Con: duplicates Froth's existing persistence story.
- Con: introduces a second configuration plane with its own recovery and drift
  problems.
- Con: over-integrates the feature into ESP-IDF instead of treating it as a
  Froth interaction capability.

### Option C: Runtime UART rebinding as an optional console capability, persisted via `autorun`

Expose runtime console-rebinding words in Froth. The platform owns the live
switching mechanism, but desired startup behavior is persisted only by normal
Froth code in `autorun`, then `save`.

Trade-offs:

- Pro: live switching works.
- Pro: persistence remains ordinary Froth, not hidden platform state.
- Pro: safe boot continues to be the recovery path automatically.
- Pro: generalizes beyond one ESP32-specific use case.
- Pro: keeps the kernel surface small.
- Con: the console will still start on the platform default path at power-up,
  then move when `autorun` runs.
- Con: if `autorun` moves the console away from UART0, the user must know the
  new endpoint or use safe boot to recover.

## Decision

**Option C: runtime UART rebinding as an optional console capability,
persisted via ordinary Froth state.**

The deciding factor is simplicity with the right ownership boundary.

The console path should remain a platform responsibility, but the desired
startup behavior should remain a Froth program responsibility. Froth already
has a persistence mechanism and a boot hook for exactly this kind of policy:
define `autorun`, then `save`.

This ADR therefore proposes:

1. Keep the existing single-console `platform_*` API shape.
2. Allow a platform to retarget that console path at runtime.
3. Standardize a minimal Froth surface for UART rebinding.
4. Persist desired console routing only through `autorun` and snapshots.
5. Preserve a fixed safe-boot recovery path on the platform default console.
6. Replace ADR-053's permanent "UART0 only" runtime rule with a narrower rule:
   UART0 remains the default boot and recovery console, but not the only
   allowed runtime console route.

### Minimal word surface

Platforms that support runtime UART console rebinding MUST provide:

- `console.uart! ( port tx rx baud -- )`

Platforms SHOULD also provide:

- `console.default! ( -- )`
- `console.info ( -- )`

Meaning:

- `console.uart!` rebinds the active console to the specified UART port, TX
  pin, RX pin, and baud rate.
- `console.info` reports the currently active console transport in a compact,
  human-readable form.
- `console.default!` restores the platform's recovery/default console path.

This ADR is intentionally UART-scoped. It does **not** define a generic
"endpoint object" surface in Froth, and it does not pre-design future
non-UART console paths.

### Persistence model

There is no separate console configuration store.

To persist console routing across boots, the user defines an `autorun` word and
saves the snapshot:

```froth
: typewriter-console   1 17 16 1200 console.uart! ;
: autorun   typewriter-console ;
save
```

Because safe boot happens before restore and `autorun`, recovery remains
available even if the saved `autorun` moves the console to a path the user
cannot currently access.

### Transport and mode rules

Runtime console rebinding must preserve ADR-048's failure shape.

Therefore:

- `console.uart!` MUST fail when a Live session is currently attached.
- `console.default!` MUST fail when a Live session is currently attached.
- Successful rebinding changes the active path for both Direct Mode and any
  future Live attachment.
- Rebinding is allowed during boot/`autorun` and during ordinary Direct Mode
  evaluation.
- If `autorun` moves the console away from the platform default UART, then
  subsequent raw terminal access, `HELLO_REQ`, and `ATTACH_REQ` occur on the
  new active console path, not on the default UART.

This keeps the invariant that one active byte stream has one meaning at a time.

### Switching semantics

`console.uart!` must be atomic from the user's point of view.

Therefore:

- the implementation MUST validate the requested port, pins, baud, and resource
  ownership before changing the active console
- the implementation MUST flush pending console output before committing the
  switch
- if validation or reconfiguration fails, the active console remains unchanged
- the implementation MUST either complete the switch fully or leave the old
  console fully active

### Default boot behavior

This ADR explicitly supersedes ADR-053 section 2's permanent "console/live
session remains on UART0" rule.

The replacement rule is: UART0 is the **default boot and recovery path**, not
the only allowed runtime console route.

At power-up:

1. the platform default console comes up first,
2. safe boot is offered on that path,
3. restore runs,
4. `autorun` may then move the console.

This preserves ADR-053's workshop-friendly default while removing its stronger
runtime restriction.

### Interaction with auxiliary UART handles

The currently active console UART must not also be allocatable as a generic
`uart.init` handle.

If a platform exposes both:

- runtime console rebinding, and
- generic UART handles,

then:

- `uart.init` MUST reject requests that target the active console UART port or
  otherwise overlap the active console route in a way that would steal the
  transport
- `console.uart!` MUST reject requests that target a UART port or route already
  owned by an allocated generic UART handle

## Consequences

- Froth gains a small, explicit runtime console-control surface without adding a
  second persistence system.
- The typewriter use case becomes a normal Froth workflow: program on the
  default console, define `autorun`, save, then reboot into the new path.
- Safe boot remains the recovery story for a bad console route.
- The platform layer becomes responsible for hot-switching the underlying
  console backend while preserving the existing `platform_*` contract.
- After `autorun` moves the console, the default UART is a recovery path, not
  the active host-tooling path.
- The first backend can be ESP32 UART rebinding, but the model does not assume
  ESP-IDF-specific storage or build flags.

## Non-goals

- No compile-time console selection knobs
- No separate NVS-backed console configuration plane
- No generic endpoint-object model in Froth
- No promise that every platform must support runtime console rebinding
- No simultaneous multi-console mirroring in this tranche
- No non-UART console endpoint design in this ADR

## References

- `src/platform.h`
- `src/froth_console.c`
- `src/froth_boot.c`
- `platforms/esp-idf/platform.c`
- `docs/spec/Froth_Interactive_Development_v0_5.md`
- `docs/archive/adr/028-board-platform-architecture.md`
- `docs/adr/048-exclusive-live-session-transport.md`
- `docs/archive/adr/053-board-serial-and-i2c-surface.md`
