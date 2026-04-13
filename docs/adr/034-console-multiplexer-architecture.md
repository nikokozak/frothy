# ADR-034: Console Multiplexer Architecture

**Date**: 2026-03-15
**Status**: Accepted
**Spec sections**: Froth_Interactive_Development_v0_5.md (Link Mode), ADR-033 (FROTH-LINK/1)

## Context

ADR-033 defines FROTH-LINK/1: COBS-encoded binary frames delimited by 0x00, sharing the primary serial line with the Direct Mode REPL. A console multiplexer must sit between the raw byte stream and the REPL/link dispatcher, classifying each incoming byte as direct console traffic, frame data, or interrupt.

The current architecture has the REPL as the main loop. `froth_repl_start()` calls `platform_key()` in a blocking loop, processes characters, and calls the evaluator when a complete expression is ready. There is no layer between platform I/O and the REPL.

Three questions shape the mux design:

1. **Integration model.** How does the mux relate to the REPL and the link dispatcher?
2. **`key` during framed EVAL.** If host-submitted code calls `key`, the evaluator blocks on `platform_key()`. The host is waiting for EVAL_RES. What happens?
3. **Output during framed EVAL.** When evaluated code calls `emit` or `.`, raw bytes go out the serial line alongside COBS-framed responses. Who manages this?

## Options Considered

### Option A: Thin wrapper

The REPL remains the main loop. A `froth_mux_read_byte()` function wraps `platform_key()`. It silently consumes frame bytes (accumulating into a buffer), dispatches complete frames inline, and returns only direct-mode bytes to the REPL.

Pros: minimal refactor. The REPL stays as-is except for swapping `platform_key()` calls.

Cons: the REPL still owns the loop. Adding a second input source (TCP, second UART, test harness pipe) means either stuffing more logic into `mux_read_byte()` or duplicating the read loop. The mux becomes a bag of special cases rather than an architectural layer.

### Option B: Poll-and-dispatch main loop

The mux becomes the main loop. It calls `platform_key()`, classifies each byte, and routes it to the appropriate handler:

- Direct-mode bytes go to the REPL's line buffer
- Frame bytes accumulate into the frame buffer
- Complete frames go to the link dispatcher
- Raw 0x03 outside frames sets `vm->interrupted`

The REPL stops being a loop. It becomes a pair of functions: one that accepts a byte into its line buffer (handling echo, backspace, continuation), and one that evaluates a completed line. The mux calls these.

Pros: clean extension point. New input sources plug into the mux's poll cycle without touching the REPL. The REPL gets simpler. The boundary between "byte classification" and "line editing" is explicit.

Cons: larger refactor. The REPL's read loop, multi-line accumulation, and evaluate-on-complete logic must be restructured from a loop into stateful functions.

### Option C: Event/callback system

Register handlers for byte classes. The mux fires callbacks when events occur (byte received, frame complete, line complete). Fully decoupled.

Pros: maximum flexibility.

Cons: callback-driven control flow is hard to follow in C, especially on embedded. Overengineered for the current need. The mux has exactly two consumers (REPL and link dispatcher). A callback registry is abstraction for its own sake.

## Decision

**Option B: Poll-and-dispatch main loop.**

The mux is the new heartbeat of the system. The REPL and link dispatcher are handlers that the mux feeds. This is the right shape for a system that will grow to support multiple input sources (host CLI, VS Code, TCP, test harness) without accumulating wrapper hacks.

### Integration details

**Main loop ownership.** `froth_mux_run()` replaces `froth_repl_start()` as the top-level loop called from `froth_boot`. It calls `platform_key()`, classifies the byte, and routes it.

**Mux state machine.** The mux tracks one piece of state: whether it is currently accumulating a frame.

- `MUX_DIRECT`: incoming bytes go to the REPL. A 0x00 byte transitions to `MUX_FRAME`.
- `MUX_FRAME`: incoming bytes accumulate into the frame buffer. A 0x00 byte terminates the frame (COBS-decode, validate, dispatch), then transitions back to `MUX_DIRECT`. If the frame buffer overflows, the partial frame is discarded and the terminating 0x00 starts a fresh accumulation.

In `MUX_DIRECT`, raw 0x03 sets `vm->interrupted`. In `MUX_FRAME`, 0x03 is data.

**REPL restructuring.** The REPL becomes two entry points:

- `froth_repl_accept_byte(uint8_t byte)`: handles echo, backspace, line buffer accumulation, multi-line depth tracking. Returns a status indicating whether a complete expression is ready.
- `froth_repl_evaluate()`: runs `froth_evaluate_input()` on the accumulated buffer, displays errors, prints stack. Resets the line buffer.

The mux calls `accept_byte` for each direct-mode byte. When it signals "expression complete," the mux calls `evaluate`.

Prompt display: the REPL emits `froth> ` or `.. ` at the appropriate times. The initial prompt is emitted when the mux starts. Subsequent prompts are emitted by `evaluate` (after displaying results) or by `accept_byte` (after a continuation line completes).

**Frame buffer.** Static array inside the mux module. Sized at `FROTH_LINK_MAX_PAYLOAD + 16` bytes (12-byte header + worst-case COBS expansion). Governed by the existing `FROTH_LINK_MAX_PAYLOAD` CMake knob.

**EVAL execution.** When the link dispatcher receives an EVAL_REQ, it calls `froth_evaluate_input()` inline. This blocks the mux loop. The dispatcher builds an EVAL_RES from the result and emits it as a COBS-encoded frame via `platform_emit()`. The mux loop then resumes. Stop-and-wait means this is correct: the host does not send another frame until it receives the response.

### `key` during framed EVAL

`key` blocks on `platform_key()` as it always does. No mode flag, no error, no special behavior on the device side.

The linked editor is the primary development mode. Throwing an error on `key` during framed EVAL would make it impossible to develop interactive programs (games, serial command parsers, sensor input) through the editor. That defeats the purpose of the tooling.

What happens in practice:

1. The evaluator hits `key` and blocks on `platform_key()`.
2. The host detects the stall (EVAL_RES timeout) and tells the user: "device is waiting for input."
3. The user types into the host UI. The host sends the byte raw (not framed). `platform_key()` picks it up. Evaluation continues.
4. Ctrl-C (raw 0x03) interrupts as usual if something goes wrong.

**Future enhancement (not Phase 1):** When `key` is called during framed EVAL, emit an `EVENT(INPUT_WAIT)` frame before blocking. This tells the host definitively that the device needs input, rather than relying on a timeout heuristic. The check is gated behind `FROTH_HAS_LINK` so it adds no cost to minimal builds. The system works without it.

### Output during framed EVAL

Passthrough. `emit`, `.`, and other I/O words go straight out via `platform_emit()` as raw bytes. The host distinguishes program output from response frames by the 0x00 delimiters: raw program output never contains 0x00 in normal use (String-Lite is \0-free, `emit` sends the low byte of a cell).

**Edge case: `0 emit`.** Sends a literal null byte. The host sees 0x00 and begins frame accumulation on bytes that are actually program output. When the real response frame starts with 0x00, the accumulated garbage is discarded and the real frame syncs correctly (ADR-033 resync rules). Some program output between the `0 emit` and the next frame may be lost on the host side. This is output corruption, not protocol corruption. Documented, not mitigated.

The mux does not manage the outbound side. Outbound is raw except for link response frames, which the dispatcher emits directly.

### Interrupt handling

Unchanged from ADR-030. Raw 0x03 outside a frame sets `vm->interrupted`. Inside a frame, 0x03 is just data. During EVAL execution (whether from the REPL or from a link EVAL_REQ), `platform_check_interrupt()` polls at executor safe points. The mux does not add new interrupt machinery.

### Feature gating

The frame accumulation and link dispatch paths are gated behind `FROTH_HAS_LINK`. When disabled, `froth_mux_run()` degrades to a byte-forwarding loop that feeds every byte to the REPL. This should compile down to roughly the same code path as the current `froth_repl_start()`.

## Consequences

- `froth_repl_start()` is replaced by `froth_mux_run()` as the system's main loop. This is the largest structural refactor since the executor restructuring (ADR-031).
- The REPL becomes simpler: no read loop, no direct `platform_key()` calls. It is a line buffer with evaluate-on-complete.
- New input sources can plug into the mux's poll cycle without touching the REPL or link dispatcher.
- `key` works normally during framed EVAL. The cost is that the host must handle input-wait detection via timeout (Phase 1) or an explicit EVENT frame (Phase 2+).
- `0 emit` during framed EVAL may cause partial output loss on the host side. Documented behavior.
- The mux depends on COBS decode and CRC32 (from `froth_link.c`), the REPL, and the link dispatcher. It does not depend on the evaluator directly.
- `FROTH_HAS_LINK=OFF` disables frame handling, leaving the mux as a thin passthrough to the REPL.

## References

- ADR-033: FROTH-LINK/1 binary transport (framing rules, resync semantics)
- ADR-030: platform_check_interrupt + safe boot (interrupt handling)
- ADR-031: hardening (executor restructuring precedent)
- `src/froth_repl.c`: current REPL main loop (refactored by this ADR)
- `docs/archive/concepts/tooling-and-link-architecture-proposal-2026-03.md`: mux layer in tooling proposal
