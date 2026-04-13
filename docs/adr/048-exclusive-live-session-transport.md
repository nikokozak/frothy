# ADR-048: Exclusive Live Session Transport

**Date**: 2026-03-22
**Status**: Accepted
**Spec sections**: `docs/spec/Froth_Interactive_Development_v0_5.md` (Direct/Link modes, interrupt semantics), ADR-035 (host daemon architecture), ADR-037 (host-centric deployment)

## Context

Froth currently supports two overlapping interaction models on the same serial byte stream:

- Direct Mode REPL traffic
- COBS-framed host protocol traffic (`FROTH-LINK/1`, ADR-033)

ADR-034 formalized this as a console multiplexer: raw bytes outside `0x00` delimiters remain Direct Mode traffic, framed bytes become protocol messages, and raw `0x03` outside a frame remains interrupt.

That design achieved early tooling quickly, but bench work on real ESP32 hardware exposed the deeper cost of the mixed-stream model:

1. **Ambiguous failures.** A dropped or misclassified frame boundary can turn a valid response body into user-visible console garbage.
2. **Protocol contamination from ordinary output.** The current design explicitly documents that `0 emit` may confuse host frame parsing.
3. **`key` depends on a side channel.** During framed eval, `key` currently relies on the host timing out, guessing that input is needed, and sending raw bytes outside the protocol.
4. **Error handling is structurally muddy.** When trust in the line is lost, the host cannot always distinguish "console output", "valid protocol body with a missing delimiter", and "true corruption".
5. **The daemon inherits transport ambiguity.** Host logic must demultiplex a stream that is simultaneously human I/O and machine protocol, which makes correctness harder to reason about and test.

Two bench findings made the problem concrete:

- ESP-IDF boot/app logs on UART0 were a real source of transport contamination until explicitly disabled.
- Even after quieting firmware logs, pure `INFO_REQ`/`INFO_RES` traffic could intermittently leak a valid COBS frame body into the console path, implying that the mixed-stream architecture itself still permits ambiguous interpretation when framing is disturbed.

This is the wrong failure shape for a system that needs to remain small, legible, removable on constrained targets, and trustworthy enough for high-assurance applications.

The project still needs all of the following:

- Raw terminal-first autonomy: "the device is the computer" remains true.
- Editor-driven live development from the daemon and VS Code.
- Interactive programs that use `key` and `key?`.
- Host-side multi-file resolution, build, and flash workflows across target sizes.
- A transport model with bounded, explicit failure behavior.

The key question is no longer "how do we patch the mux?" It is "what interaction model gives the smallest robust system?"

## Options Considered

### Option A: Keep the mixed-stream design and harden it

Continue with ADR-033 + ADR-034:

- raw Direct Mode bytes and framed protocol share the same live stream
- the daemon keeps classifying incoming bytes as either console or frame data
- `key` during framed eval continues to rely on raw byte injection outside the frame protocol
- more host/device resynchronization logic is added as edge cases are discovered

Trade-offs:

- Pro: smallest migration from the current implementation.
- Pro: preserves simultaneous raw terminal and framed host use on one stream.
- Con: the core ambiguity remains. A byte may still be interpreted as console, protocol, or corruption depending on timing and parser state.
- Con: `key` remains heuristic-driven rather than protocol-defined.
- Con: more "recovery" logic increases complexity precisely where assurance requires determinism.
- Con: a system built on mixed semantics tends to accumulate technical debt in the daemon, extension, and firmware at the same time.

### Option B: Exclusive Live session on the same UART

Keep Direct Mode as the default human-facing REPL, but require an explicit mode transition into a framed-only host session:

- `Direct`: raw REPL, terminal-friendly, no host assumptions
- `Live`: framed-only protocol, no raw console bytes or raw input bytes on the wire

Interactive input becomes explicit protocol traffic rather than a raw side channel. Program output becomes explicit protocol events rather than raw UART bytes.

Trade-offs:

- Pro: the wire contract is unambiguous while Live Mode is active.
- Pro: `key` and `key?` can remain first-class in editor-driven development.
- Pro: corrupt frames can be dropped without ever surfacing as user-visible console output.
- Pro: the daemon becomes simpler because it no longer needs to classify one stream into two active semantic channels.
- Pro: the feature can be compiled out on constrained targets without affecting host-side build/flash workflows.
- Con: raw terminal interaction and live editor interaction can no longer share one active session.
- Con: the transport/session boundary must be redesigned, not merely patched.
- Con: ADR-033 and ADR-034 need to be superseded in their current form.

### Option C: Separate physical or logical channels for console and tooling

Use a second UART, USB endpoint, or equivalent separate path for tooling while keeping the primary console raw.

Trade-offs:

- Pro: cleanest separation in theory.
- Pro: raw console and structured tooling could coexist physically.
- Con: not portable across the intended target set.
- Con: increases hardware assumptions and board complexity.
- Con: does not help the many devices that only expose one practical serial path.
- Con: contradicts the design goal that tooling should work across Froth targets without requiring special hardware arrangements.

## Decision

**Option B: Exclusive Live session on the same UART.**

The deciding factor is not convenience. It is failure shape.

The system must fail closed when transport trust is lost. A framed response must never degrade into "maybe user output." Raw user I/O must never degrade into "maybe protocol." `key` must not depend on timeout heuristics. Those requirements are fundamentally at odds with a permanently mixed stream.

This ADR therefore replaces the mixed-stream Link Mode with an explicit two-mode model:

1. **Direct Mode**
   Raw serial REPL, prompt, stack display, terminal-friendly, safe boot, rescue path.

2. **Live Mode**
   Framed-only daemon/editor session. All input and output are explicit protocol messages or events. No raw bytes are interpreted as application console traffic while Live Mode is active.

This preserves the "device is the computer" principle from the interactive spec because Direct Mode remains primary and sufficient. It also preserves the host-centric deployment workflow from ADR-037 because `reset + eval` remains the core editor/file-send story.

What changes is the session contract on the wire.

### Review notes (Mar 22)

**The NUL-framed attach recognizer is the right compromise.** Alternatives considered during review: UART break (unambiguous but not portable across USB-serial adapters and not in the VFS/stdio path), DTR/RTS toggle (triggers ESP32 auto-reset circuit, non-starter), text escape sequence (violates fail-closed because it can be produced by the evaluator or pasted input), and no in-band trigger (unusable for editor-driven development). NUL-framing wins because 0x00 genuinely does not occur in normal Froth REPL input: the reader rejects it, string escapes don't produce it, and keyboards can't type it. The only source is hardware noise, which is bounded by `uart_flush`, the safe boot window, and the accumulation bounds specified below.

The recognizer must enforce two accumulation bounds to cap worst-case input loss from stray 0x00 bytes:

1. **Byte limit.** If the recognizer accumulates more than `FROTH_LINK_COBS_MAX` bytes without a closing 0x00, it abandons the frame and discards. At 115200 baud this is ~24ms of input.
2. **Timeout.** If no closing 0x00 arrives within 50ms of the opening 0x00, the recognizer abandons and discards. This is belt-and-suspenders with the byte limit and makes worst-case latency explicit.

With both bounds, a single stray 0x00 at serial connect costs at most one or two lost keystrokes, once. During normal typing, the recognizer is invisible.

**Device discovery needs a lightweight probe.** The current host discovers Froth devices by sending HELLO_REQ and checking for HELLO_RES, with no state transition. In this ADR, Direct Mode only recognizes ATTACH_REQ, so discovery would require attach + immediate detach on every candidate port. That is too heavy. The Direct Mode recognizer should accept `HELLO_REQ` as a second recognized message type: stateless, returns device info, stays in DIRECT_IDLE. This keeps port scanning cheap and non-destructive.

**Output flushing strategy.** Section 7 specifies that all Live Mode output becomes OUTPUT_DATA but does not specify flush policy. Per-`emit` flushing (one frame per byte) is too chatty. Per-eval flushing kills live output during long programs. The implementation should flush on `\n` or when the output buffer is full, whichever comes first. This gives line-buffered live output during normal programs and bounded latency for binary output.

## Specification

### 1. Interaction modes

The device supports two mutually exclusive runtime interaction modes:

#### Direct Mode

- default boot mode
- ordinary Froth prompt and REPL behavior
- raw serial terminal compatible
- prompt-time input is a textual REPL profile, not a general binary transport
- `key` and `key?` operate on the direct console stream when user code is running
- raw Ctrl-C interrupt semantics remain unchanged

#### Live Mode

- entered only by explicit attach from Direct Mode
- framed-only protocol
- no raw REPL bytes, no raw prompt, no raw application output, no raw interrupt byte semantics
- `key` and `key?` operate on a Live input queue fed by protocol messages

Invariant:

**At no point is one active byte stream interpreted simultaneously as both user console traffic and framed protocol traffic.**

### 2. State machine

The device-side session state machine is:

1. `DIRECT_IDLE`
   Top-level prompt active, waiting for direct input or attach.

2. `LIVE_IDLE(session_id)`
   Live session attached, waiting for framed requests.

3. `LIVE_EVAL(session_id, seq)`
   One eval request is active. The device may emit framed output events and may accept framed input/control messages.

State transitions:

- boot -> `DIRECT_IDLE`
- successful attach from `DIRECT_IDLE` -> `LIVE_IDLE`
- `EVAL_REQ` from `LIVE_IDLE` -> `LIVE_EVAL`
- completed eval -> `LIVE_IDLE`
- successful detach from `LIVE_IDLE` -> `DIRECT_IDLE`
- lease expiry or hard reset from any Live state -> `DIRECT_IDLE`

There is no background console multiplexer while the device is in Live Mode. Live Mode owns the serial line until detach, lease expiry, or reset.

### 3. Attach / detach

Attach is not a Froth word and is not routed through the evaluator.

Direct Mode runs a **bounded control recognizer**, not a general live multiplexer:

- it recognizes exactly two framed message types: `HELLO_REQ` and `ATTACH_REQ`
- it ignores every other framed payload
- it never forwards recognized control bytes to the Froth reader

This recognizer exists because a text-line attach command is not fail-closed: if attempted at the wrong time, it can become ordinary program input. A NUL-delimited frame cannot occur in normal Direct Mode console text, so attach attempts remain non-destructive.

Direct control frames use the same COBS framing envelope as Live Mode, but in Direct Mode only `HELLO_REQ` and `ATTACH_REQ` are legal.

Prompt-time Direct Mode therefore reserves NUL-framed attach traffic. This is an explicit trade-off: the prompt is a textual control surface, not a byte-transparent binary channel. Binary-transparent interactive I/O remains available:

- while user code is actively reading with `key` in Direct Mode
- or through `INPUT_DATA` / `OUTPUT_DATA` in Live Mode

The recognizer is intentionally bounded:

- it is active only at a prompt boundary with an empty Direct Mode line buffer
- it only accepts candidate control frames up to a small fixed encoded-size cap
- it abandons a partial candidate frame if accumulation exceeds a short timeout without a closing `0x00`

Recommended defaults:

- encoded Direct-control candidate cap: 64 bytes
- Direct recognizer timeout: 50 ms

If a stray `0x00` starts accumulation and no valid tiny control frame follows promptly, the device abandons the candidate and returns to normal prompt behavior instead of consuming keystrokes indefinitely.

Attach frame rules:

1. `message_type = ATTACH_REQ`
2. `version = 2`
3. `seq = 0`
4. `session_id` is the proposed non-zero 64-bit session chosen by the host
5. payload is empty in Phase 1

Hello frame rules:

1. `message_type = HELLO_REQ`
2. `version = 2`
3. `seq = 0`
4. `session_id = 0`
5. payload is empty in Phase 1

Successful `HELLO_REQ` response:

- `HELLO_RES` with device capabilities and no mode transition

`HELLO_REQ` is discovery-only. It must leave the device in `DIRECT_IDLE`.

Successful device response:

- `ATTACH_RES` with status `OK`

Failure response:

- `ATTACH_RES` with status `BUSY`, `UNSUPPORTED`, or `INVALID`

Attach preconditions:

1. device is in `DIRECT_IDLE`
2. no eval or autorun is active
3. the direct REPL line buffer is empty
4. the device is at a top-level prompt boundary

Attach is only guaranteed to succeed when those preconditions are true.

If the recognizer observes `HELLO_REQ` or `ATTACH_REQ` while those preconditions are false, it must remain non-destructive:

- it must not forward bytes to the Froth reader
- it must not modify the REPL line buffer, evaluator state, or stack state
- it may return `ATTACH_RES(BUSY)` for attach if it can do so without reentering the evaluator
- otherwise it may ignore the request and let the host time out

On successful attach:

1. both sides clear transport state
2. the device enters `LIVE_IDLE(session_id)`
3. Direct Mode prompt emission stops until detach or session loss

`DETACH_REQ` is a framed message accepted only in `LIVE_IDLE`. On success, the device returns `DETACH_RES`, clears Live session state, returns to `DIRECT_IDLE`, and re-emits the normal prompt.

Rationale:
This keeps Direct Mode autonomy while making attach fail-closed and non-destructive.

### 4. Live framing

Live Mode uses COBS framing with `0x00` delimiters.

Wire form:

```text
0x00 <COBS-encoded frame> 0x00
```

Frame header:

```text
offset  size  field
0       2     magic = "FL"
2       1     version = 2
3       1     message_type
4       8     session_id
12      2     seq
14      2     payload_length
16      4     crc32
20      N     payload
```

Rules:

1. `session_id` is a non-zero 64-bit value chosen by the host at attach time and echoed in every Live frame.
2. Frames with the wrong `session_id` are dropped.
3. `seq = 0` is reserved for attach-related messages and `KEEPALIVE`.
4. Normal request sequences start at `1` immediately after attach and advance by `+1`, wrapping from `0xFFFF` back to `1`.
5. Responses echo the initiating normal request `seq`.
6. Device events emitted during eval use the active eval `seq`.
7. `crc32` covers header bytes `[0..15]` plus payload.
8. Invalid COBS, bad CRC, bad magic, bad version, impossible lengths, and malformed payloads are rejected at the protocol layer and never exposed as console output.
9. The host must generate `session_id` with strong freshness guarantees and must not intentionally reuse a prior session ID within the daemon lifetime. Cryptographically strong randomness is recommended.

This preserves the good part of ADR-033: small binary framing with strong delimiting and integrity checking. What changes is that Live Mode no longer shares semantics with raw console traffic.

### 5. Message set

#### Host -> device

- `HELLO_REQ` (Direct Mode only)
- `ATTACH_REQ` (Direct Mode only)
- `KEEPALIVE`
- `INFO_REQ`
- `RESET_REQ`
- `EVAL_REQ`
- `INPUT_DATA`
- `INTERRUPT_REQ`
- `DETACH_REQ`

#### Device -> host

- `HELLO_RES` (Direct Mode only)
- `ATTACH_RES` (Direct Mode only)
- `INFO_RES`
- `RESET_RES`
- `EVAL_RES`
- `OUTPUT_DATA`
- `INPUT_WAIT`
- `ERROR`
- `DETACH_RES`

Rules:

1. Only one normal request may be active at a time:
   `INFO_REQ`, `RESET_REQ`, `EVAL_REQ`, `DETACH_REQ`.
2. In `LIVE_IDLE`, the device accepts only the next expected normal request sequence.
3. While an eval is active, the only additional host messages permitted are:
   `KEEPALIVE`, `INPUT_DATA`, `INTERRUPT_REQ`.
4. `KEEPALIVE` always uses `seq = 0`.
5. `INPUT_DATA` and `INTERRUPT_REQ` must use the active eval `seq`.
6. During eval, the device may emit zero or more:
   `OUTPUT_DATA`, `INPUT_WAIT`, `ERROR`
   followed by exactly one terminal `EVAL_RES`.
7. `KEEPALIVE` is one-way. It extends the Live session lease and has no response.
8. A frame with the correct `session_id` but an unexpected `seq` receives `ERROR(BAD_SEQ)` and is otherwise ignored.

### 5a. Live servicing model

Live Mode must be serviceable during long-running eval, not only during blocking `key`.

The session layer therefore exposes a bounded nonblocking poll hook, conceptually:

- `froth_live_poll_control()`

The implementation name is not normative, but the behavior is:

1. It reads and processes at most a bounded amount of serial input.
2. It accepts only the message types legal for the current Live state.
3. In `LIVE_EVAL`, it must be invoked:
   - at the same executor safe points already used for interrupt polling
   - inside the blocking wait path for Live `key`
   - immediately before emitting the terminal `EVAL_RES`
4. It may enqueue input bytes, refresh the lease, or set the interrupt flag.
5. It must not evaluate Froth source or reenter the evaluator.

This keeps protocol logic out of the kernel proper. From the kernel's perspective, the requirement is still just "poll a session-aware control hook at safe points."

Important limitation:

Live liveness is cooperative, not preemptive. A board primitive that blocks or spins without returning to executor safe points, and without explicitly polling the Live control hook, may starve `KEEPALIVE`, `INPUT_DATA`, and `INTERRUPT_REQ` until it returns.

Therefore:

- kernel words and cooperative FFI get the full Live guarantees
- long-running or blocking board primitives must either poll the Live control hook explicitly
- or be documented as uninterruptible / Live-hostile operations

### 6. Payload compatibility

Where practical, existing structured payload layouts from ADR-033 are retained:

- `HELLO_REQ`: empty payload
- `HELLO_RES`: existing binary device-info payload
- `INFO_REQ`: empty payload
- `INFO_RES`: existing binary device-info payload
- `RESET_REQ`: empty payload
- `RESET_RES`: existing post-reset info payload
- `EVAL_REQ`: existing flags + source text payload
- `EVAL_RES`: existing status + error + optional stack representation payload

New payloads:

#### `OUTPUT_DATA`

```text
offset  size  field
0       2     byte_count
2       N     raw output bytes
```

`byte_count` must equal `payload_length - 2`.

#### `INPUT_WAIT`

```text
offset  size  field
0       1     reason = 1  (waiting for key)
```

Additional reasons may be added later, but Phase 1 defines only `1`.

#### `INPUT_DATA`

```text
offset  size  field
0       2     byte_count
2       N     raw input bytes
```

#### `ERROR`

Protocol-level error response. Existing ADR-033 error framing may be retained, but `ERROR` is a protocol message only. It is never rendered as raw console text.

### 7. Output semantics

#### Direct Mode

Unchanged:

- `emit`, `.`, `s.emit`, prompt text, and stack display write to the raw console stream

#### Live Mode

All output becomes framed `OUTPUT_DATA` events.

Rules:

1. Program output is never emitted as raw UART bytes while Live Mode is active.
2. `OUTPUT_DATA` carries raw bytes, not assumed text.
3. Prompts and automatic REPL stack display are suppressed in Live Mode.
4. The device buffers output bytes and flushes an `OUTPUT_DATA` frame:
   - when a newline is emitted
   - when the output buffer becomes full
   - immediately before emitting any non-output terminal frame such as `INPUT_WAIT`, `EVAL_RES`, `ERROR`, `RESET_RES`, or `DETACH_RES`
   - optionally after a short max-latency timer if the buffer is non-empty
5. Human-readable console rendering in the daemon/editor is a host concern, not a wire concern.

Recommended defaults:

- `FROTH_LIVE_OUTPUT_CAP = 128`
- max output flush latency: 25 ms

This removes the current `0 emit` ambiguity entirely.

### 8. Input semantics

#### Direct Mode

Unchanged:

- `key` reads the raw console stream
- `key?` tests that stream

#### Live Mode

The session owns a fixed-size input FIFO.

Rules:

1. `INPUT_DATA` appends bytes to the FIFO.
2. `key?` checks FIFO non-destructively.
3. `key` pops one byte if available.
4. If `key` finds the FIFO empty:
   - emit one `INPUT_WAIT`
   - block in a Live-session wait loop until:
     - input arrives, or
     - interrupt arrives, or
     - the session lease expires

`INPUT_WAIT` is edge-triggered:

- it is emitted once when `key` first blocks on an empty FIFO
- it is not repeated until at least one byte is received or the eval unwinds

This is the direct replacement for the timeout heuristic described in ADR-034.

### 9. Interrupt semantics

#### Direct Mode

Unchanged:

- raw `0x03` remains the interrupt signal

#### Live Mode

Interrupt is an explicit framed message:

- `INTERRUPT_REQ`

Rules:

1. There is no raw interrupt byte in Live Mode.
2. Executor safe points poll the Live control state and treat `INTERRUPT_REQ` exactly like the existing interrupt flag.
3. A blocking Live `key` wait also observes pending interrupts.
4. If the Live lease expires during eval, the device treats that as a synthetic interrupt and unwinds to top level before returning to Direct Mode.

### 10. Reset semantics

`RESET_REQ` reuses the VM reset behavior established in ADR-037, but it does not force a return to raw prompt mode.

Rules:

1. `RESET_REQ` is valid only in `LIVE_IDLE`.
2. The system must provide one shared internal reset routine implementing ADR-037's reset steps.
3. The Direct Mode `reset` primitive and the Live `RESET_REQ` handler both invoke that same routine.
4. In Live Mode, the session layer, not the REPL, owns the top-level reset handling.
5. `RESET_REQ` clears overlay/runtime state and also clears Live session-local state that must not survive reset:
   - active eval state
   - Live input FIFO
   - pending `INPUT_WAIT` edge state
6. The device remains attached in `LIVE_IDLE`.
7. The device returns `RESET_RES`.
8. No raw prompt text is emitted as part of reset.

This preserves the editor workflow:

1. attach
2. `RESET_REQ`
3. `EVAL_REQ` of the whole file

without reintroducing mixed wire semantics.

### 11. Failure model

The protocol is fail-closed.

Transport-layer rules:

1. Invalid frames are dropped or rejected at the protocol layer.
2. Invalid frames are never forwarded as user-visible console output.
3. If the host does not receive the expected terminal response within timeout, it must mark the Live session failed.

Recovery rules:

1. A failed Live session is not considered trustworthy.
2. Recovery is explicit:
   - `DETACH_REQ` if still possible
   - otherwise physical reset, reconnect, and reattach
3. Tooling may retry only explicitly idempotent operations such as `INFO_REQ`, and only as a fresh request.
4. `EVAL_REQ` is never retried automatically.
5. After session teardown, all frames from the old `session_id` are dropped silently.

This is the central assurance property:

**when transport trust is lost, the system stops pretending it still understands the stream.**

### 12. Lease / liveness

Live Mode is protected by a host lease.

Recommended defaults:

- `FROTH_LIVE_LEASE_MS = 5000`
- host sends `KEEPALIVE` periodically while attached

Rules:

1. Any valid host frame refreshes the lease.
2. `KEEPALIVE` exists so the lease can be maintained during long evals without violating the one-request-at-a-time rule.
3. If the lease expires in `LIVE_IDLE`, the device returns to `DIRECT_IDLE`.
4. If the lease expires in `LIVE_EVAL`, the device interrupts the eval and returns to `DIRECT_IDLE`.

Policy statement:

Live Mode is host-owned and ephemeral. Direct Mode is the durable human-facing fallback. Lease expiry therefore returns control to Direct Mode, even if that means aborting the current Live eval.

This prevents the device from becoming trapped in a dead host session while preserving the project's "device is the computer" principle at the outermost level.

### 13. Feature gating

The transport is optional at build time.

Recommended configuration knobs:

- `FROTH_HAS_LIVE`
- `FROTH_LIVE_MAX_PAYLOAD`
- `FROTH_LIVE_INPUT_CAP`
- `FROTH_LIVE_LEASE_MS`
- `FROTH_LIVE_OUTPUT_CAP`
- `FROTH_DIRECT_CONTROL_TIMEOUT_MS`

Profiles:

#### Direct-only profile

- no Live session support
- no attach recognizer
- no live parser, FIFO, or event emitter
- host-side project resolution, build, and flash still function

#### Direct + Live profile

- full session transport
- intended for ESP32-class and host-native targets

This keeps firmware cost proportional to target class. It also preserves the current host-side project/build story for small devices.

### 14. Host architecture impact

ADR-035 remains correct at a high level: the daemon owns the serial port and presents structured RPC to the CLI and editor.

What changes is the wire model underneath it:

1. The daemon no longer demultiplexes one active raw/framed stream during Live Mode.
2. `OUTPUT_DATA` becomes the source of console events.
3. Interactive input is sent through `INPUT_DATA`, not raw serial injection.
4. Any future PTY support is a host-side adapter layered over `OUTPUT_DATA` and `INPUT_DATA`, not a concurrent raw byte stream sharing the same on-device wire semantics.

This reduces, rather than increases, daemon complexity.

ADR-035 is therefore revised in one important respect:

- the daemon still owns the port and may still expose a PTY later
- but that PTY is no longer a direct passthrough competing with framed RPC on the physical wire
- instead, it is an emulated byte-stream endpoint backed by Live `OUTPUT_DATA` and `INPUT_DATA`

Raw passthrough to the real device UART remains a Direct Mode concern outside the daemon-owned Live session.

### 15. Migration strategy

Clean cut. The v1 mixed-stream protocol (ADR-033, ADR-034) is removed, not deprecated. The old `froth_transport.c`, `froth_link.c`, `froth_console_mux.c`, and Go `internal/protocol` v1 code are replaced in place. There is no coexistence period and no backwards compatibility layer. Git history is the rollback plan. The only consumer of v1 is our own tooling, and mixed-version complexity is exactly the kind of spaghetti this ADR exists to eliminate.

## Consequences

- The transport boundary becomes much easier to reason about: either the system is in raw Direct Mode or in framed Live Mode, never both.
- Prompt-time Direct Mode becomes explicitly text-oriented rather than byte-transparent; that trade-off is intentional so attach can remain fail-closed.
- Direct Mode device discovery remains lightweight through `HELLO_REQ`; discovery no longer requires attach/detach churn.
- `key` and `key?` remain available to interactive editor-driven programs without relying on timing heuristics.
- Corrupt protocol bytes can no longer surface as user console output by design.
- The daemon and extension become simpler because they no longer have to treat one byte stream as two active semantic channels.
- ADR-033 and ADR-034 are superseded. Their implementations are removed, not maintained.
- Simultaneous raw terminal use and live editor use on the same active session becomes intentionally unsupported.
- A future PTY bridge remains possible, but as a host adapter rather than a transport primitive.
- Small targets can opt out of Live support without losing host-side manifest resolution, build, or flash.
- Blocking or CPU-bound FFI that does not cooperate with safe-point polling remains a bounded but explicit limitation of Live responsiveness.

## Validation Requirements

This ADR should not be accepted without proving at least the following:

1. In Live Mode, no unframed application output appears on the wire.
2. In Direct Mode, no live protocol parsing occurs except the explicit attach recognizer.
3. Invalid Live frames never surface as user-visible console bytes.
4. `key` in Live Mode requires no timeout heuristic.
5. Host death produces bounded recovery back to Direct Mode.
6. `0 emit` in Live Mode is safe.
7. `RESET_REQ` + whole-file `EVAL_REQ` remains compatible with ADR-037.
8. A Direct-only build removes Live support cleanly without breaking host build/flash workflows for constrained targets.
9. A stale buffered frame from a prior session cannot be accepted as part of a new session.
10. Representative long-running board primitives are either cooperative with Live polling or explicitly documented as uninterruptible.
11. A stray `0x00` in Direct Mode cannot consume prompt input indefinitely; the bounded recognizer recovers within the configured timeout.
12. Discovery via `HELLO_REQ` does not change device mode.
13. Live output latency is bounded without requiring one frame per emitted byte.

## References

- `docs/spec/Froth_Interactive_Development_v0_5.md`
- `docs/adr/033-link-transport-v1.md`
- `docs/adr/034-console-multiplexer-architecture.md`
- `docs/adr/035-host-daemon-architecture.md`
- `docs/archive/adr/037-host-centric-deployment.md`
- Bench findings from Mar 21–22, 2026:
  - ESP-IDF UART log contamination on ESP32
  - intermittent `INFO_RES` frame-body leakage into the host console path
  - daemon/editor hangs caused by ambiguous mixed-stream interpretation under framing disturbance
