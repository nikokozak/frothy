# Frothy ADR-110: Single-Owner Control Session Transport

**Date**: 2026-04-12
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 1, 2, 8, Appendix B
**Roadmap milestone(s)**: post-M10 follow-on queue
**Inherited Froth references**: `docs/adr/033-link-transport-v1.md`, `docs/adr/034-console-multiplexer-architecture.md`, `docs/adr/035-host-daemon-architecture.md`, `docs/adr/039-host-tooling-ux-and-daemon-lifecycle.md`, `docs/adr/048-exclusive-live-session-transport.md`

## Context

Frothy inherited enough of the Froth transport and host-control shape to get a
real shell and ESP32 proof path working. That was useful bootstrap reuse, but
it is not the right long-term control surface for Frothy.

The Froth transport stack solved real problems:

- framing structured host requests
- keeping a human REPL available
- letting tools and editors drive the board
- coordinating flash, reset, interrupt, and console output

It also accumulated the exact kinds of fragility that Frothy should avoid:

- mixed raw and framed traffic on one live stream
- console/output interleave that host code must classify correctly
- daemon lifecycle and port ownership questions
- quiesce/resume paths around flashing
- pressure to grow PTY passthrough and shared-client behavior

Frothy has a different center of gravity now:

- stable named slots are the public identity
- overlay save / restore / wipe are built in
- inspection is in the base image
- the FFI surface is intentionally narrow

That means Frothy does not need a long-lived host daemon to preserve state.
The device image already is the state.

## Options Considered

### Option A: Keep the inherited daemon and mixed-stream transport shape

Continue the Froth pattern of structured control plus raw console sharing one
transport story, with a host owner process coordinating access.

Trade-offs:

- Pro: feature-rich and familiar from Froth.
- Pro: keeps future editor tooling paths open.
- Con: preserves the most fragile part of the old system.
- Con: requires daemon ownership, flash coordination, and stream classification.
- Con: is too much machinery for Frothy's intended size.

### Option B: Single-owner direct serial session with explicit control mode

Keep one serial owner at a time. Keep the raw Frothy REPL as the default human
mode. Let a host tool switch explicitly into a structured control session when
it needs machine-readable replies.

How it works:

- A human attaches to the ordinary REPL and sees raw text prompts and output.
- A host tool opens the serial port directly. If needed, it interrupts to get
  back to the prompt.
- The host tool sends a plain-text attach command, `.control`, at the prompt.
- The device acknowledges and switches that connection into control-session
  mode.
- Inside control-session mode, traffic is COBS-framed and exclusive.
- There are no raw unframed console bytes inside the session.
- Program output becomes structured `OUTPUT` events.
- Successful evaluation yields structured `OK` / `VALUE` replies.
- Errors yield structured `ERROR` replies.
- The host can send `DETACH` to return the device to the ordinary REPL.
- Closing the port also ends the session cleanly.
- Flashing and reconnecting are simple because no daemon owns the port.

The minimum message set is:

- `HELLO`
- `EVAL`
- `WORDS`
- `SEE`
- `CORE`
- `SLOT_INFO`
- `SAVE`
- `RESTORE`
- `WIPE`
- `DETACH`

The minimum event set is:

- `OUTPUT`
- `VALUE`
- `ERROR`
- `INTERRUPTED`
- `IDLE`

Raw `Ctrl-C` remains the out-of-band emergency interrupt byte in both modes.
The critical simplification is not the codec. It is the exclusivity:
no mixed raw/framed ownership while a control session is active.

Trade-offs:

- Pro: much smaller than the daemon-plus-mux design.
- Pro: easy to debug with a terminal and a tiny host tool.
- Pro: avoids shared ownership and quiesce/resume state.
- Pro: fits Frothy's "the image is the state" model.
- Con: no simultaneous human and tool control of one live port.
- Con: editor tooling must reconnect per action or hold the port briefly.
- Con: `key`-style raw interactive programs stay in raw attach mode, not in the
  structured control session.

### Option C: Replace the daemon with a thinner localhost proxy

Keep a host-resident process that owns the port, but make it smaller and more
specialized than the old Froth daemon.

Trade-offs:

- Pro: preserves a single host-side place for reconnect logic.
- Con: still creates a second stateful system outside the device image.
- Con: still has to answer the hard questions about flashing, ownership, and
  client coordination.
- Con: reduces complexity less than Option B.

## Decision

**Option B.**

Frothy adopts a single-owner direct session model:

- raw text REPL remains the default human surface
- structured host control is an explicit exclusive session, not a concurrent
  side channel
- the host tool opens the port directly and does not depend on a daemon
- `.control` is the REPL-level entry into structured mode
- COBS framing remains acceptable inside the structured session because it is
  simple on serial links, but it is no longer mixed with raw console traffic
- structured session payloads stay minimal and operation-specific; Frothy does
  not adopt JSON-RPC or a general host object protocol

Implementation order:

1. urgent slice 1:
   - add `.control` entry and `DETACH`
   - support `HELLO`, `EVAL`, `WORDS`, and `SEE`
   - support structured `OUTPUT`, `VALUE`, `ERROR`, `INTERRUPTED`, and `IDLE`
   - prove prompt recovery after `DETACH`, interrupt, and disconnect
   - ship one tiny direct host tool that opens the serial port itself
2. slice 2:
   - support `CORE`, `SLOT_INFO`, `SAVE`, `RESTORE`, and `WIPE`
3. build editor actions on top of short-lived CLI invocations or one direct
   extension-owned connection, not a background owner

The deciding factor is that Frothy should become smaller and more legible than
Froth, not rebuild the same transport stack in slightly different clothes.

## Consequences

- Frothy no longer needs a daemon-first story for normal live development.
- Flashing becomes "close the tool, flash, reconnect" instead of a quiesce
  protocol.
- The transport layer becomes easier to test because raw REPL and structured
  control are separate modes.
- The urgent first slice is deliberately broad enough to flush out the hard
  problems early: mode switch, output delivery, interrupt, detach, and prompt
  recovery.
- Host tooling should stay mostly stateless. The device image remains the real
  source of truth.
- If later work needs richer image-loading operations, they should be added as
  new session commands, not by reintroducing concurrent stream ownership.
- Frothy explicitly does not promise concurrent multi-client control of one
  board in `v0.x`.

## References

- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
- `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`
- `tools/frothy/proof_m10_smoke.sh`
- `docs/archive/proofs/m10_esp32_proof_transcript.txt`
