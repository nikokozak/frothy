# ADR-033: Link Transport v1 (COBS binary framing)

**Date**: 2026-03-15
**Status**: Accepted
**Spec sections**: Froth_Interactive_Development_v0_5.md (Link Mode), FROTH-REPL profile

## Context

The interactive development spec (v0.5) defines Link Mode using `STX` (0x02) / `ETX` (0x03) framing with text payloads and `#ACK`/`#NAK` response lines. ADR-030 standardizes `ETX` (0x03, Ctrl-C) as the interrupt byte. These two uses collide: `ETX` cannot simultaneously delimit link frames and interrupt runaway code.

This is not a cosmetic issue. It affects runtime interrupt behavior, safe boot, host framing, and any future transport (TCP, WebSocket, test harness). It must be resolved before link tooling ships.

The current design also has structural problems beyond the byte collision:

- Multi-line expressions need second-order framing (the host must decide when a form is "complete" before sending, or the device must accumulate across frames).
- Text response lines (`#ACK`, `#NAK`) mix with program output on the same stream, making parsing fragile.
- There is no structured error reporting, inspection, or capability discovery.

This ADR replaces the current Link Mode design with a binary-framed transport that avoids all of these problems.

## Options Considered

### Option A: Fix STX/ETX with escaping

Keep the current framing but add an escape mechanism for 0x03 inside frames.

Trade-offs:
- Pro: minimal spec change.
- Con: every implementation must handle escaping correctly. The spec becomes harder to teach. Human terminal mixing gets harder to reason about. The fundamental problem (text framing on a binary stream) remains.

### Option B: COBS-encoded binary frames with 0x00 delimiter

Use Consistent Overhead Byte Stuffing (COBS) with 0x00 as the frame delimiter. Payloads are binary (fixed-width integers, length-prefixed strings).

Trade-offs:
- Pro: 0x00 never appears in normal REPL text (Froth does not emit NUL bytes). No collision with Ctrl-C (0x03). COBS is well-understood, adds at most 1 byte per 254, and has a small C implementation (~40 lines encode, ~30 lines decode). Binary payloads avoid string escaping overhead on the device.
- Con: not human-readable on the wire. Requires a tool to inspect frames during debugging.

### Option C: Length-prefixed binary frames (no COBS)

Use a start byte + length prefix + raw payload.

Trade-offs:
- Pro: simpler than COBS.
- Con: if a length byte is corrupted, the receiver cannot resynchronize until the next valid start byte. COBS provides self-synchronization: any 0x00 byte resets the frame state. On noisy serial links, this matters.

## Decision

**Option B: COBS-encoded binary frames with 0x00 delimiter.**

The deciding factors:
1. 0x00 does not collide with any existing Froth I/O (Ctrl-C, printable text, escape sequences).
2. COBS provides self-synchronization on corrupt/partial frames, which matters on real serial links.
3. Binary payloads keep the device-side implementation small: no JSON serializer, no string escaping.
4. The framing layer is transport-independent. The same frame format works over UART, TCP, WebSocket, or a test harness pipe.

The transport is named `FROTH-LINK/1`.

## Specification

### 1. Wire framing

Each frame on the wire:

```
0x00 <COBS-encoded bytes> 0x00
```

The 0x00 bytes are delimiters, not part of the COBS-encoded data. COBS decoding applies only to the bytes between the two delimiters.

Rules:
- A 0x00 byte outside an active frame starts frame capture (resync marker).
- Bytes accumulate until the next 0x00 (frame terminator).
- The captured bytes are COBS-decoded. If decode fails, the frame is silently dropped.
- If a 0x00 arrives while accumulating a frame, the partial frame is discarded and the 0x00 is treated as a new resync marker. This provides self-synchronization on corrupt or partial frames.
- Bytes outside frame regions are ordinary console traffic (Direct Mode REPL).
- Raw 0x03 outside frames remains an interrupt request.
- Bytes inside frames are data, not console control. A 0x03 inside a frame is just a data byte.

This is the key property the current STX/ETX design lacks.

### 2. Console multiplexer

A new logical layer sits between `platform_key`/`platform_emit` and the REPL. Its job is to split the incoming byte stream into:

- Direct Mode bytes (forwarded to the REPL)
- Framed link messages (forwarded to the link dispatcher)
- Raw interrupt bytes outside frames (set `vm->interrupted`)

This layer is protocol logic. It does not live in `froth_repl.c`, in platform code, or in the evaluator. Recommended files: `froth_console_mux.h` / `froth_console_mux.c`.

### 3. Decoded frame layout

All multi-byte integers are little-endian.

```
offset  size  field
0       2     magic = "FL" (0x46 0x4C)
2       1     version = 1
3       1     message_type
4       2     request_id
6       2     payload_length
8       4     crc32
12      N     payload bytes
```

Total header: 12 bytes.

`crc32` covers bytes 0 through 7 (the header fields before the CRC) concatenated with the payload bytes. This protects both header and payload in a single check. A bit flip in `message_type` or `payload_length` will fail the CRC, not silently route to the wrong handler.

Validation:
- `magic` must equal `"FL"`.
- `version` must equal 1.
- `payload_length` must not exceed `FROTH_LINK_MAX_PAYLOAD` (compile-time, default 256).
- `crc32` must match CRC32 of header bytes [0..7] + payload bytes (reuse `froth_crc32`).
- Unknown `message_type` values receive an ERROR response.

`request_id` values 0x0000 through 0xFFFE are valid. The value 0xFFFF is reserved as a sentinel for ERROR frames when the request could not be parsed (see ERROR payload below). The host should start its counter at 1 and wrap from 0xFFFE back to 1.

No reserved fields. If future versions need more header space, they bump the version byte.

### 4. Message types

Phase 1 message set:

```
0x01  HELLO_REQ       host -> device
0x02  HELLO_RES       device -> host
0x03  EVAL_REQ        host -> device
0x04  EVAL_RES        device -> host
0x05  INSPECT_REQ     host -> device
0x06  INSPECT_RES     device -> host
0x07  INFO_REQ        host -> device
0x08  INFO_RES        device -> host
0xFE  EVENT           device -> host (unsolicited)
0xFF  ERROR           device -> host
```

Phase 1 deliberately omits: LIST_WORDS, streaming stdout capture, concurrent requests, snapshot upload/download, debugger single-stepping. Those can be added later without changing the framing layer.

### 5. Request model

Phase 1: stop-and-wait. The host sends one request. The device processes it to completion. The device returns exactly one response with the same `request_id`. The host does not send the next request until the current one resolves or times out.

Exception: the device may send unsolicited EVENT frames at any time. The host must be prepared to receive and discard (or handle) EVENT frames while waiting for a response.

### 6. Payload formats (binary)

All payloads are binary. Strings are length-prefixed: `u16 length` followed by `length` raw UTF-8 bytes (no NUL terminator in the wire format).

#### HELLO_REQ

Payload: empty (payload_length = 0).

#### HELLO_RES

```
offset  size  field
0       1     cell_bits (8, 16, 32, 64)
1       2     max_payload
3       4     heap_size
7       4     heap_used
11      2     slot_count
13      1     flags (bit 0: snapshots_enabled, bit 1: snapshot_present)
14      2     version_string_len
16      V     version_string (UTF-8, e.g. "0.5")
16+V    2     board_string_len
18+V    B     board_string (UTF-8, e.g. "esp32-devkit-v1")
18+V+B  1     capability_count
19+V+B  ...   capability entries (each: u8 capability_id)
```

V = value of version_string_len. B = value of board_string_len.

`heap_size` and `heap_used` are u32 for consistency with the snapshot format and to accommodate heaps larger than 64KB on 32/64-bit targets. `slot_count` is u16 (max 65535), which is sufficient; implementations must not set `FROTH_SLOT_TABLE_SIZE` above 65535.

Capability IDs (u8):
```
0x01  eval
0x02  inspect
0x03  info
0x04  interrupt
0x05  safe_boot
0x06  snapshots
```

Future capabilities get new IDs. The host feature-detects by scanning the list.

#### EVAL_REQ

```
offset  size  field
0       1     flags (bit 0: want_stack)
1       2     source_len
3       S     source (raw UTF-8 Froth source text)
```

S = value of source_len. The explicit length prefix is consistent with all other string fields in the protocol. `payload_length` must equal `3 + source_len`.

The device evaluates the source with the same safety rules as Direct Mode: implicit top-level `catch`, stack restoration on error.

Console output produced during evaluation is emitted as normal Direct Mode text (not captured into the response frame). The host should expect interleaved text on the byte stream between sending the request and receiving the response.

**OPEN QUESTION A**: Should the device suppress Direct Mode console output during framed evaluation? Or always allow it to interleave? Suppressing is cleaner for the host parser but breaks `emit`/`.` semantics. Allowing interleave means the host mux must handle text between frames.

#### EVAL_RES

```
offset  size  field
0       1     status (0 = ok, 1 = error)
1       2     error_code (0 if ok)
3       2     fault_word_len (0 if none)
5       F     fault_word (UTF-8)
5+F     2     stack_repr_len (0 if not requested or error)
7+F     S     stack_repr (UTF-8, standard stack display format e.g. "[7 42]")
```

F = value of fault_word_len. S = value of stack_repr_len.

`error_code` carries the positive `froth_error_t` value (1-299 range). The internal sentinel `FROTH_ERROR_THROW` (-1) must never appear on the wire; the dispatcher resolves it to the actual thrown error code before encoding.

Stack representation is textual in phase 1 (the standard `[v0 v1 ... vN]` format). A future version may add a structured binary stack encoding.

#### INSPECT_REQ

```
offset  size  field
0       2     name_len
2       N     name (UTF-8)
```

N = value of name_len. `payload_length` must equal `2 + name_len`.

#### INSPECT_RES

```
offset  size  field
0       1     found (0 = not found, 1 = found)

If found = 0, no further fields.

If found = 1:
1       1     kind
                0x10 = undefined (slot exists but no binding)
                0x11 = primitive
                0x12 = quotation (user-defined via def)
                0x13 = value (number, string, pattern, etc.)
2       1     origin
                0x00 = base (loaded during boot, not overlay)
                0x01 = overlay (user-defined after boot)
3       1     has_prim (0 or 1; a slot can have both prim and overlay impl)
4       2     stack_effect_len (0 if unavailable)
6       N     stack_effect (UTF-8, e.g. "( a b -- c )")
6+N     2     help_len (0 if unavailable)
8+N     M     help (UTF-8)
8+N+M   2     display_len
10+N+M  K     display (UTF-8, human-readable one-line summary, e.g. "[1 +]" or "<primitive>")
```

This provides everything `see` currently prints, without scraping text.

`kind` values start at 0x10 to avoid numeric overlap with `froth_cell_tag_t` (0x00-0x06). These are protocol-level classifications, not cell tags.

**OPEN QUESTION B**: Should INSPECT_RES include the raw quotation body as tagged cells (binary), or only the textual display? Binary gives the host richer data (for disassembly, cross-reference, etc.) but adds complexity. Phase 1 recommendation: text display only.

#### INFO_REQ

Payload: empty (payload_length = 0).

#### INFO_RES

```
offset  size  field
0       4     heap_size
4       4     heap_used
8       4     heap_overlay_used
12      2     slot_count
14      2     slot_overlay_count
16      1     flags (bit 0: snapshots_enabled, bit 1: snapshot_present, bit 2: safe_boot_supported)
17      2     version_string_len
19      V     version_string
```

V = value of version_string_len. Heap fields are u32 for consistency with HELLO_RES and the snapshot format.

#### EVENT

```
offset  size  field
0       1     event_type
1       N     event_data (type-specific)
```

Phase 1 defines no event types. The message type is reserved so the host can be written to handle unsolicited device messages from the start. Future events: definition change notification, heap pressure warning, interrupt acknowledgment.

**OPEN QUESTION C**: Should phase 1 include an event for "word defined/redefined"? This would let the host track device-side changes made via Direct Mode without polling. Cost: the evaluator would need to emit a frame after every `def`. Benefit: the sync/drift panel works properly. Recommendation: defer to phase 2 unless implementation proves trivial.

#### ERROR

```
offset  size  field
0       1     category
                0x01 = decode (COBS decode failure)
                0x02 = crc (payload CRC mismatch)
                0x03 = unsupported (unknown message_type)
                0x04 = busy (device is mid-evaluation, stop-and-wait violated)
                0x05 = overflow (payload_length exceeds max_payload)
1       2     detail_len
3       N     detail (UTF-8, optional human-readable explanation)
```

ERROR frames use `request_id` 0xFFFF if the request could not be parsed far enough to extract one. Otherwise they echo the request's ID. (See header section for request_id conventions.)

### 7. Ctrl-C during framed evaluation

If the user sends 0x03 while the device is processing an EVAL_REQ, the interrupt flag is set as usual. The evaluator catches the error via the implicit `catch`. The device sends a normal EVAL_RES with `status = 1`, `error_code = 14` (ERR.INTERRUPT). No special handling needed in the protocol.

### 8. Reconnection

Link Mode has no persistent state. Each HELLO_REQ/HELLO_RES is a fresh handshake. If the host disconnects and reconnects, it sends HELLO again. The device does not need to track "am I in link mode" as a persistent flag; it simply responds to framed messages whenever they arrive. Direct Mode always works.

### 9. Device-side module plan

```
src/froth_link.h        frame encode/decode, COBS codec, header parse/build
src/froth_link.c        ^^
src/froth_console_mux.h byte stream splitting (direct vs. frame vs. interrupt)
src/froth_console_mux.c ^^
src/froth_link_dispatch.h   request dispatch, response building
src/froth_link_dispatch.c   ^^
```

The dispatcher calls existing VM functions: `froth_evaluate_input`, `froth_slot_find_name`, `froth_slot_get_impl`, `froth_slot_get_prim`, `froth_ffi_find_entry`, etc. It does not duplicate evaluator or inspector logic.

The link layer is gated behind `FROTH_HAS_LINK` (CMake, default ON for POSIX and ESP32, default OFF for minimal builds). When disabled, the console mux is a no-op passthrough: all bytes go to the REPL.

### 10. FROTH_LINK_MAX_PAYLOAD

Default: 256 bytes. CMake-configurable via `set(FROTH_LINK_MAX_PAYLOAD 256 CACHE STRING "Max link frame payload bytes")`. This caps the maximum payload in any single link message. For EVAL_REQ, this limits source text length. For larger sends, the host splits into multiple requests (already natural since the host should sync by top-level form).

256 bytes is sufficient for most single definitions. It can be raised on targets with more RAM.

**Receive buffer sizing.** The device needs a buffer large enough for one COBS-decoded frame: `FROTH_LINK_MAX_PAYLOAD + 12` bytes (payload + header). The COBS-encoded wire representation is slightly larger (COBS adds at most 1 byte per 254 of input), but decoding happens in-place or into the same buffer, so the decoded size is the binding constraint. The implementation should define `FROTH_LINK_FRAME_BUF_SIZE` as `(FROTH_LINK_MAX_PAYLOAD + 12 + 2)`, with 2 bytes of margin for COBS overhead on small frames.

## Open Questions

**A. Console output during EVAL_REQ.** Should `emit`, `.`, and other I/O words produce output on the Direct Mode text stream during framed evaluation? Or should output be captured/suppressed? Allowing interleave is simpler on the device but requires the host mux to handle text between request and response. Suppressing breaks user expectations (e.g. a definition that prints debug output during load). Recommendation: allow interleave, document that the host must tolerate it.

**B. Raw quotation bodies in INSPECT_RES.** Should the response include binary tagged cells, or only the textual display? Binary enables richer host-side analysis but adds payload size and complexity. Recommendation: text-only in phase 1.

**C. Definition-change events.** Should the device emit an EVENT frame when `def` is called? This would let the host track Direct Mode changes without polling. Adds a frame emit inside `def` (or in the evaluator post-`def`). Recommendation: defer to phase 2.

**D. Host language and architecture.** This ADR does not prescribe the host implementation language. The protocol is language-neutral. The host CLI/daemon architecture is a separate decision (see tooling proposal doc).

**E. Stray 0x00 bytes.** A NUL byte on the serial line (noise, reset glitch) will trigger frame capture. If the subsequent bytes don't decode as a valid COBS frame, the frame is silently dropped. This is the correct behavior, but the host should be aware that a burst of noise could consume a few bytes of Direct Mode input. In practice, this is rare and self-correcting.

## Implementation timeline

Given the current TIMELINE.md, this work slots into the week of Mar 15-21:

- **Mar 15-16**: Device-side link layer in C. COBS codec, console mux, frame parser, dispatcher with HELLO and EVAL. Test on POSIX build by piping COBS frames through stdin. ADR-033 accepted.
- **Mar 17-18**: Host CLI skeleton. Open serial port, send HELLO, print response, send EVAL, print result. Proves end-to-end protocol. Language decision made here.
- **Mar 19-21**: AI-assisted buildout of host CLI commands, daemon, VS Code extension skeleton. Iterative review.

This replaces the current TIMELINE entries for "Web editor + Link Mode" (Mar 14-15) and is more concrete.

Kernel work (memory addresses, ESP libraries, audio FFI) can interleave with this work on alternating sessions.

## Consequences

- The current Link Mode section of Froth_Interactive_Development_v0_5.md must be rewritten to reference this transport.
- The `#ACK`/`#NAK` text protocol is superseded for tool use. `#`-prefixed lines remain useful for dumb terminal integrations and smoke tests, but are no longer the primary tooling interface.
- The REPL code (`froth_repl.c`) does not change. The console mux sits in front of it.
- Future transports (TCP, WebSocket) can reuse the same frame format by wrapping COBS frames in the transport's native framing.
- The `FROTH_HAS_LINK` gate means the link layer adds zero overhead on builds that don't need it.

## References

- [COBS encoding](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing) (Cheshire & Baker, 1999)
- [Froth Interactive Development Spec v0.5](../spec/Froth_Interactive_Development_v0_5.md)
- [ADR-030: platform_check_interrupt and safe boot](030-platform-check-interrupt-and-safe-boot.md)
- [Tooling and Link Architecture Proposal](../concepts/tooling-and-link-architecture-proposal-2026-03.md)
- [Target Tiers and Tethered Mode](../concepts/target-tiers-and-tethered-mode.md)
