# ADR-035: Host Daemon Architecture

Historical note: this Froth-era ADR is retained as substrate reference. The maintained Frothy host-control path no longer uses `internal/daemon/`.

**Date**: 2026-03-16
**Status**: Accepted
**Spec sections**: Froth Interactive Development v0.5 (Link Mode), ADR-033 (FROTH-LINK/1), ADR-034 (console multiplexer)

## Context

The host CLI skeleton is proven (info, send over FROTH-LINK/1). The next step is a daemon process that owns the serial connection, multiplexes access for multiple clients (CLI, VS Code, future tools), and handles device reconnection.

Several design tensions needed resolution:

1. Should the daemon expose only structured RPC, or also raw REPL passthrough?
2. If raw passthrough, should it use a custom protocol or a virtual PTY?
3. How do we prevent byte interleaving corruption when COBS frames and raw REPL bytes share the same serial wire?
4. How does this serve creative technologists who need raw serial access for sensor data, Processing sketches, Max/MSP patches, web apps, etc.?

## Options Considered

### Option A: RPC-only daemon

The daemon speaks JSON-RPC over a Unix domain socket. All device interaction goes through structured RPC methods (eval, info, hello). No raw REPL access through the daemon. Users who want a raw REPL either run the POSIX build locally or connect a serial monitor directly (without the daemon running).

Trade-offs: simplest to implement. Sufficient for CLI and VS Code "send selection" workflows. Does not serve any tool outside the Froth ecosystem. Does not support raw serial consumers (sensor hubs, creative coding tools).

### Option B: RPC + JSON-RPC attach

Same as A, plus a JSON-RPC "attach" method that gives one client raw byte passthrough. The client sends keystrokes as RPC calls, receives raw device output as RPC events.

Trade-offs: only Froth ecosystem tools can use it (they must speak JSON-RPC). Minicom, Processing, Max/MSP, web serial readers cannot attach. Adds protocol complexity for no interoperability gain. Worst of both worlds.

### Option C: RPC + virtual PTY

The daemon creates a PTY pair. The slave end appears as a pseudo-serial-port path (e.g. ~/.froth/pty). Any standard serial tool (minicom, picocom, screen, cat, Processing, Max/MSP, a web app reading serial) can open the slave PTY for raw REPL access. COBS-framed RPC traffic from CLI/VS Code clients goes through the Unix socket. The daemon is the sole writer to the real serial port and serializes access: PTY bytes flow through normally, but are paused during RPC frame transactions to prevent wire interleaving.

Trade-offs: most useful long-term. Serves the creative tech use case (microcontroller as sensor hub, raw serial consumers). More complex to implement (PTY creation is POSIX-specific). Requires write serialization to prevent frame corruption.

## Decision

**Option C**, implemented in two phases.

### Phase 1 (workshop, Mar 21)

RPC-only. No PTY. The daemon's internal architecture (single-writer serial access, read-side demuxer that separates COBS frames from raw console bytes, event broadcast) is designed and built with the PTY in mind, but the PTY slot is empty. All clients use JSON-RPC over the Unix socket.

This is sufficient for the workshop: `froth send`, `froth info`, VS Code "send selection" and "evaluate line" all use eval RPCs. A REPL-like VS Code panel sends one eval per input line. It looks like a REPL but is request/response underneath.

### Phase 2 (post-workshop, next priority)

Add the virtual PTY. The daemon creates a PTY pair on startup, announces the slave path. Raw bytes from the PTY slave are forwarded to the device serial. Raw device output (non-frame bytes) is forwarded to the PTY master. The write serializer pauses PTY forwarding during RPC frame transactions.

This is not a nice-to-have. It is the feature that makes the daemon useful beyond Froth's own tooling. It opens the door to the sensor-hub and creative-coding workflows that are central to the project's audience.

### Interleaving prevention

The daemon is the sole writer to the real serial port. This is the invariant that prevents the corruption scenario where raw REPL bytes interleave with COBS frame bytes on the wire.

Without the PTY (phase 1), there is only one byte source (RPC frames). No interleaving possible.

With the PTY (phase 2), two byte sources exist: PTY raw bytes and RPC COBS frames. The daemon serializes writes:

1. Normally, PTY bytes flow through to device serial immediately.
2. When an RPC transaction starts, the daemon pauses PTY byte forwarding (buffers incoming PTY bytes).
3. The daemon sends the COBS frame (0x00 + encoded + 0x00).
4. The daemon waits for the response frame.
5. The daemon resumes PTY forwarding (flushes buffered bytes).

The device console mux never sees interleaved data. Frames arrive intact. REPL bytes arrive intact. CRC checks pass.

If the daemon pauses PTY forwarding during a long eval (e.g. an infinite loop caught by interrupt), the PTY user experiences a brief input freeze. Ctrl-C (0x03) should bypass the pause and be sent immediately, since the device's interrupt handler checks for 0x03 outside frame context.

## Daemon Specification

### Lifecycle

- `froth daemon start` runs the daemon in the foreground. Background with shell `&` or system service.
- `froth daemon stop` signals the running daemon to shut down (via Unix socket RPC or signal).
- `froth daemon status` reports whether the daemon is running and what device is connected.
- PID file: `~/.froth/daemon.pid`
- Unix socket: `~/.froth/daemon.sock`
- PTY slave (phase 2): `~/.froth/pty`

The daemon is both a CLI subcommand and can be invoked directly from scripts. The original daemon logic lived in `internal/daemon/`, imported by both entry points.

### IPC protocol

JSON-RPC 2.0 over the Unix domain socket, newline-delimited. Events use JSON-RPC notifications (no request ID).

### RPC methods (phase 1)

- `hello`: send HELLO_REQ, return device info.
- `eval`: send EVAL_REQ with source text, return structured result (status, error code, fault word, stack repr).
- `info`: send INFO_REQ, return heap/slot/version data.
- `status`: return daemon state (connected, device info, PTY path if available).

### CLI integration

- `froth send "1 2 +"` auto-detects the daemon socket. If the socket exists and the daemon responds, the command routes through the daemon. If not, falls back to direct serial.
- `--serial` flag forces direct serial access, bypassing the daemon. Useful when the daemon is running but you want an independent connection for testing.
- `--daemon` flag forces daemon routing, fails if daemon is not running.
- Default (no flag): try daemon, fall back to serial.

### Event stream

Connected clients can subscribe to events. Event types:

- `console`: raw device text (non-frame bytes). Payload is a string.
- `connected`: device connection established. Payload includes device info.
- `disconnected`: device connection lost.
- `reconnecting`: daemon is attempting to reconnect.

Events are broadcast to all connected RPC clients. In phase 2, the same console text also goes to the PTY.

### Reconnect behavior

When the device disconnects (serial read error, USB unplug):

1. Daemon broadcasts `disconnected` event.
2. Daemon enters reconnect loop: probe candidate serial ports every 2 seconds.
3. Broadcasts `reconnecting` on each attempt.
4. On successful HELLO handshake, broadcasts `connected` with new device info.
5. RPC calls during disconnection return a clear error ("device not connected").

### Single device

The daemon connects to one serial port (auto-detected or specified via `froth daemon start --port <path>`). Multi-device support is a future concern and out of scope.

## Consequences

- The CLI gains three connection modes: daemon (default), direct serial (`--serial`), and explicit daemon (`--daemon`).
- VS Code extension talks only to the daemon via JSON-RPC. Does not own serial logic.
- Phase 1 is sufficient for the workshop. Phase 2 (PTY) is the first post-workshop priority.
- The PTY in phase 2 makes Froth accessible to the broader creative tech ecosystem (Processing, Max/MSP, web serial, sensor hubs) without those tools knowing anything about Froth's protocol.
- The write serializer design must be correct before PTY is enabled. A bug here causes frame corruption. This is the highest-risk component.
- Ctrl-C forwarding during paused PTY must be handled as a special case (bypass the pause, send 0x03 immediately).

## References

- ADR-033: FROTH-LINK/1 binary transport
- ADR-034: Console multiplexer architecture
- `docs/archive/concepts/tooling-and-link-architecture-proposal-2026-03.md` sections 17-24
- archived `src/froth_console_mux.c`: device-side byte routing (0x00 = frame start, else REPL)
- `src/froth_transport.c`: COBS encode/decode, frame build/parse
- `src/froth_link.h`: HELLO, EVAL, INFO handler surface
