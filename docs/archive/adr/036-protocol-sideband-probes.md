# ADR-036: Protocol Sideband Probes

**Date**: 2026-03-16
**Status**: Accepted
**Spec sections**: ADR-033 (FROTH-LINK/1), ADR-034 (console multiplexer), ADR-035 (daemon)

## Context

The VS Code extension needs live observability into a running device. During a long-running eval (e.g. a sensor loop), the host currently has no way to query device state. The stop-and-wait model blocks all communication until the eval completes.

A general queue model was considered and rejected. Codex review of the device-side C code identified three problems:

1. **No architectural queue.** ESP32 UART RX ring is 256 bytes. Multiple COBS frames can overflow it. The device processes frames inline and synchronously. Queued requests just sit in UART buffers accidentally.

2. **Reentrancy hazard.** Mid-eval frame parsing must never dispatch another EVAL_REQ. `froth_link_frame_complete()` currently dispatches immediately. Also, `key` blocks in `platform_key()` during framed eval (ADR-034), creating byte ownership conflicts if frame bytes are drained mid-eval.

3. **0x03 bug on ESP32.** `platform_key()` on ESP-IDF treats 0x03 as interrupt before the mux sees frame context. This violates ADR-033/034's rule that 0x03 inside a frame is data. Must be fixed independently.

## Options Considered

### Option A: General queue model

Host sends multiple requests without waiting. Device processes sequentially. Responses matched by ID.

Trade-offs: request IDs already work for matching. But no bounded inbound queue, UART buffer overflow risk on ESP32, reentrancy hazard if mid-eval frame parsing dispatches eval. Requires significant mux refactoring and careful `key` conflict resolution. More surface area than needed.

### Option B: Single eval + read-only probe sideband

Eval stays stop-and-wait. A new PROBE mechanism provides read-only device state at executor safe points during eval. Two variants:

**B1 (poll):** Host sends PROBE_REQ, device responds with PROBE_RES at next safe point. Requires the device to check for probe requests at safe points. Risk: stale probes queue up and flood after a long eval.

**B2 (subscribe):** Host sends WATCH_REQ with a list of slot indices (and optionally DS/metadata flags). Device emits EVENT(PROBE) at safe points with current values. No per-probe request overhead. Host-side daemon coalesces updates. Cleaner for continuous monitoring.

### Option C: Explicit yield word

Add a `probe.tick` word that users insert into loops. At each tick, the device checks for and services probe requests. Simplest implementation but requires loop instrumentation.

## Decision

**Option B2 (subscription model)**, with Option C as a complementary future addition.

### Protocol changes

**New message types:**

- `WATCH_REQ` (0x09): Host sends a watch configuration. Payload: `u8 flags` (bit 0 = include DS snapshot, bit 1 = include VM metadata), `u8 slot_count`, `u16[] slot_indices`. Empty slot list with flags=0 clears the watch.
- `WATCH_RES` (0x0A): Device acknowledges watch installation. Payload: `u8 status` (0 = ok, 1 = too many slots, 2 = invalid index).
- `EVENT(PROBE)` (0xFE with probe subtype): Unsolicited. Payload: `u8 probe_flags` (mirrors watch flags), followed by slot values (each: `u16 slot_index`, `u8 tag`, `u32 raw_value`), optionally DS snapshot (`u8 depth`, then depth entries of `u8 tag` + `u32 raw_value`), optionally VM metadata (`u32 heap_used`, `u16 call_depth`).

**Eval stays stop-and-wait.** The host sends one EVAL_REQ, waits for EVAL_RES. During this wait, EVENT(PROBE) frames may arrive. The daemon and host must handle interleaved EVENT and response frames.

WATCH_REQ can only be sent when no eval is active (between evals). The device rejects WATCH_REQ during eval with an ERROR response.

### Device-side implementation

1. **Mux refactor.** Frame state must be persistent (not a local `mode` variable). Split parse from dispatch so mid-eval polling can service probes without re-entering the link dispatcher.

2. **Safe-point probe check.** At executor safe points (where `platform_check_interrupt` is called), the mux checks for complete inbound frames. If the frame is a PROBE-related type, it services it inline. If it's anything else (EVAL, INFO, etc.), it is buffered or dropped with an ERROR.

3. **Read-only constraint.** Probes read slot values via `froth_slot_get_impl()` (pure struct field read, no mutation). DS snapshot reads `vm->ds.items[0..pointer]`. VM metadata reads struct fields. No evaluation, no heap allocation, no stack mutation. `last_error_slot` is not touched.

4. **Probe emission.** Raw cell/tag binary data, not pretty-printed text. The host/daemon formats for display. This keeps device-side cost minimal.

5. **Rate limiting.** The device emits at most one EVENT(PROBE) per N safe-point checks (configurable, default every 100 checks, roughly 10-50ms depending on workload). Prevents flooding.

### What probes cannot see

- Expression results (requires evaluation, unsafe mid-eval)
- Heap object contents (quotation bodies, string data) without traversal
- State inside long C words like `ms` (blocks in `vTaskDelay`, no safe points)
- Slot values that changed since the last safe point but before the current one (probes are snapshots, not traces)

### Required prerequisite

~~Fix the ESP32 0x03 bug~~: **Resolved (2026-03-18).** `platform_key()` now returns 0x03 as a normal byte with the interrupt flag set as a side effect. The mux clears the flag in frame mode (0x03 is COBS data). VFS line-ending conversion also disabled (both RX and TX) to prevent 0x0D corruption of binary frames. CR → LF and CRLF coalescing handled at the mux/REPL level.

### Host-side (daemon)

- Daemon sends WATCH_REQ when clients subscribe to probes.
- Daemon coalesces EVENT(PROBE) frames: only the latest value per slot is kept. Superseded values are discarded.
- Daemon broadcasts probe updates to subscribed clients as JSON-RPC notifications.
- ADR-035's transaction model is extended: during an active eval, the daemon accepts EVENT frames alongside the pending EVAL_RES.

## Consequences

- Eval remains simple and predictable. No queue management on device.
- Live probes work during running loops with sub-second updates at safe points.
- Probes are read-only. No risk of corrupting VM state mid-eval.
- Device-side cost is small: one frame check per N safe-point ticks, one slot read per watched slot.
- The subscription model means no per-probe request/response overhead. Install once, get updates continuously.
- Between-eval probes (arbitrary expression eval) work naturally via normal EVAL_REQ when the device is idle. No special mechanism needed.
- The 0x03 fix is a separate task but blocks this work.
- Mux refactoring (persistent frame state, split parse/dispatch) is the main device-side effort.

## References

- ADR-033: FROTH-LINK/1 (stop-and-wait, frame format, message types)
- ADR-034: Console multiplexer (mux architecture, `key` during eval, frame accumulation)
- ADR-035: Daemon architecture (RPC methods, event broadcast, transaction model)
- `src/froth_executor.c:44`: safe-point interrupt check
- `src/froth_slot_table.c:57-67`: slot_get_impl (pure read)
- `platforms/esp-idf/platform.c:40-46`: 0x03 bug (interrupt before mux classification)
- Codex review session 019cf804-1762-7071-9431-7f84136412f5 (protocol safety analysis)
