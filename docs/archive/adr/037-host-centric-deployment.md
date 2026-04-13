# ADR-037: Host-Centric Deployment with Overlay User Program

**Date**: 2026-03-17
**Status**: Accepted
**Spec sections**: Froth_Interactive_Development_v0_5 (Direct/Link modes, boot sequence), Froth_Snapshot_Overlay_Spec_v0_5 (overlay model, save/restore/wipe), ADR-014 (embedded stdlib), ADR-026 (snapshot persistence), ADR-030 (safe boot)

## Context

Froth targets hardware ranging from ESP32 (NVS flash, plenty of RAM) to RP2040, STM32, MSP430, and eventually tethered 8-bit targets. Not all targets support runtime persistence. ESP32 has NVS; most others have no equivalent, or flash write is painful enough (erase-before-write, sector alignment, limited endurance) that it can't be the default workflow.

The question: how do users deploy Froth programs to devices, and what is the canonical development workflow?

Three deployment mechanisms exist or are planned:

1. **Compile-time embedding.** The CMake pipeline (ADR-014) converts `.froth` files into `const char[]` headers baked into firmware. Already used for stdlib. Could embed a user program the same way.
2. **Link protocol EVAL.** The host sends source to the device over serial (ADR-033/034). The device evaluates it. Definitions persist in RAM until power cycle or reset.
3. **Snapshot persistence.** `save`/`restore`/`wipe` (ADR-026/027). ESP32 uses NVS. POSIX uses files. Gated behind `FROTH_HAS_SNAPSHOTS`.

The interactive development spec (Froth_Interactive_Development_v0_5) states: "The device is the computer. A person with a serial terminal and nothing else can write, test, modify, persist, and recover Froth programs. No host toolchain is required."

That principle holds for devices with persistence. For devices without it, the host is required for durable deployment. This ADR establishes the workflow model that works across all targets and defines how the three mechanisms interact.

## Options Considered

### Option A: Embedded program as base layer (pre-boot_complete)

The embedded user program evaluates before `boot_complete`. Its definitions are base layer, not overlay. They survive `wipe` and can't be snapshot-overridden.

Trade-offs:
- Pro: deployed application is permanent. `wipe` only clears REPL experiments.
- Con: live editing diverges from flash state. If the user removes a definition from their `.froth` file and tests via EVAL, the old base-layer definition is still present. False positives. The interactive editor can't faithfully simulate what a reflash would produce without actually reflashing.
- Con: no way to "undo" a base-layer definition without reflashing.

### Option B: Embedded program as overlay (post-boot_complete), re-evaluated on reset

The embedded program evaluates after `boot_complete`. Its definitions are overlay. `reset` clears overlay, restores the watermark, then re-evaluates the embedded program.

Trade-offs:
- Pro: `reset` returns to "embedded program state," which is useful.
- Con: `save` captures embedded program definitions. Next boot: `restore` loads them, then embedded program runs again. Double definitions, heap waste, and saved user modifications get overwritten.
- Con: `reset` re-evaluating the embedded program is wrong for the editing workflow. The user called `reset` to get a clean slate for the new version of their file, not to reload the old one.

### Option C: Embedded program as overlay, evaluated only at cold boot, reset is clean slate

The embedded program evaluates after `boot_complete`, but only on cold boot when no snapshot restore succeeded. `reset` clears overlay and restores the watermark without re-evaluating anything. The device is at stdlib baseline after `reset`.

Trade-offs:
- Pro: no double-definition problem. If restore succeeded, embedded program doesn't run.
- Pro: `reset` produces a clean stdlib-only state. The host sends the new program version via EVAL. Device state after reset + EVAL matches what a reflash would produce. No false positives.
- Pro: `save` after interactive editing captures only what the user EVALed, not the embedded program's definitions (since the embedded program didn't run if restore was active, and after reset the user sends fresh content).
- Con: if the device has no snapshot and no host connected, `reset` wipes the user back to stdlib with no way to reload the embedded program without rebooting. Acceptable: `reset` without a host is a power-user operation.

## Decision

**Option C.** The embedded program is overlay, evaluated once at cold boot (when no snapshot restore succeeds), and `reset` is a clean slate to stdlib.

### Boot sequence

```
register prims → platform_init → evaluate stdlib →
boot_complete = 1 → set watermark →
safe boot window (750ms, Ctrl-C skips restore + autorun) →
if FROTH_HAS_SNAPSHOTS and not safe boot: attempt restore →
if restore failed or skipped: evaluate embedded user program →
autorun → REPL/mux
```

The embedded user program is optional. When not configured, boot proceeds directly to `autorun` (which silently fails if undefined, per existing `[ 'autorun call ] catch drop` pattern).

### `reset` primitive

Available on all targets. Does the following in order:

1. Clear all overlay slots (`froth_slot_reset_overlay`).
2. Restore heap pointer to watermark.
3. Clear DS, RS, CS to empty.
4. Clear `thrown`, `last_error_slot`, `call_depth`.
5. Invalidate `mark`/`release` state (set mark sentinel).
6. Return a special error code that causes the REPL/mux to abort to top level and re-prompt.

`reset` does NOT re-evaluate the embedded program. After `reset`, the device is at stdlib baseline. The host sends new content via EVAL.

`reset` is not safe to call mid-execution (inside quotations, with RS frames, etc.). It must abort to top level. Implementation: `reset` returns a dedicated error code (e.g. `FROTH_ERROR_RESET`). The REPL/mux catches this code specifically, reinitializes its own state, and re-prompts. This is similar to how `throw` with no `catch` is handled.

### `wipe` revision

`wipe` = `reset` + erase snapshot storage. Only on `FROTH_HAS_SNAPSHOTS` targets. On non-persistent targets, `wipe` either does not exist or is an alias for `reset`.

### Snapshot compatibility

The existing ABI hash in the snapshot header (CRC32 over cell_bits + endianness + format version) remains. Add a second field: CRC32 of the embedded user program source (or 0 if no embedded program). On restore, if either hash mismatches, skip restore and log a warning. This handles:
- Cell width or format changes (ABI hash).
- Embedded program changes between flashes (source hash).
- Reflash with identical program but different C prims or stdlib: not detected by hash. Documented: `wipe` after any reflash that changes the C layer.

### Interactive editing workflow

1. User edits `.froth` file on host.
2. Editor (or CLI) sends `RESET_REQ` via link protocol. Device clears to stdlib baseline.
3. Editor sends file content via `EVAL_REQ`. Device evaluates it.
4. User tests (via EVAL or direct REPL).
5. When satisfied: reflash to make permanent, or `save` on persistent targets.

On file save or explicit "send," the editor issues reset + eval of the whole file. No selective removal of definitions. The whole-file eval is fast (milliseconds for typical programs) and correct by construction: device state after reset + eval matches what a cold boot with that file embedded would produce.

### `RESET_REQ` in link protocol

New message type. Host sends `RESET_REQ`, device executes `reset`, sends `RESET_RES` with status. Idle-only (stop-and-wait). Not valid mid-eval.

### Safe boot interaction

Safe boot (Ctrl-C during boot window) skips restore AND autorun. It does NOT skip the embedded user program, because the embedded program only defines words (no side effects at top level). Execution only happens through `autorun`, which safe boot gates.

Contract: embedded user programs must be definition-only. Top-level side effects (I/O, GPIO, delays) belong in `autorun`, not at file scope. A broken embedded program with top-level side effects is a firmware bug. Fix: reflash.

### Tier applicability

This ADR applies to all tiers:
- **Tier 1 (32-bit, full Froth):** embed + flash is the deployment path. `save` is a bonus on capable platforms.
- **Tier 2/3 (tethered):** the host owns the program by definition. The mechanism differs (the host compiles and pushes pre-resolved tokens rather than source text) but the principle is the same.

The existing spec's "device is the computer" principle is not contradicted. On targets with persistence, the device is fully autonomous. On targets without persistence, the host provides what the device can't: durable storage. The device still runs the program, owns the REPL, and provides the interactive feedback loop.

## Consequences

- The CMake embedding pipeline (ADR-014) gains a second use: user programs, not just stdlib. A CMake variable (e.g. `FROTH_USER_PROGRAM`) points at the `.froth` file.
- `reset` becomes a kernel primitive, registered unconditionally (not gated behind snapshots).
- `wipe` semantics change slightly: it now calls `reset` internally rather than duplicating the overlay-clear logic.
- The REPL/mux must handle the `FROTH_ERROR_RESET` return code as a top-level abort, not a displayable error.
- Tooling (CLI, VS Code extension) should implement "reset + eval file" as a single compound operation.
- The snapshot header grows by 4 bytes (embedded-source CRC32 field).
- Embedded programs with top-level side effects are a foot-gun. No enforcement mechanism beyond documentation and convention.

## Deferred

- Multi-file project manifests (dependency ordering, `froth.toml`).
- Source rescue (recovering `.froth` from device state via INSPECT).
- `reset` with selective preservation (keep specific overlay words across reset).
- Enforcement of "definition-only" constraint on embedded programs (lint or static check).

## References

- ADR-014: Compile-time embedded standard library
- ADR-026: Snapshot persistence implementation
- ADR-027: Platform snapshot storage API
- ADR-030: Platform interrupt polling and safe boot window
- ADR-033: FROTH-LINK/1 binary transport
- ADR-034: Console multiplexer architecture
- Froth_Interactive_Development_v0_5.md: "The device is the computer"
- Froth_Snapshot_Overlay_Spec_v0_5.md: overlay model, save/restore/wipe semantics
- docs/archive/concepts/target-tiers-and-tethered-mode.md: tier model
