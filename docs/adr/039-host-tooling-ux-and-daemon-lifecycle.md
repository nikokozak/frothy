# ADR-039: Host Tooling UX and Daemon Lifecycle

Historical note: this Froth-era ADR is retained as substrate reference. The maintained Frothy CLI now uses direct control sessions instead of the daemon lifecycle described here.

**Date**: 2026-03-17
**Status**: Accepted
**Spec sections**: ADR-035 (daemon architecture), ADR-037 (host-centric deployment), `docs/archive/concepts/vscode-extension-design.md`

## Context

The host tooling chain is built: Go CLI, daemon, VS Code extension skeleton. Several UX decisions were deferred during the buildout sprint. They need to be resolved before the extension goes in front of users.

Six coupled questions:

1. **Daemon lifecycle.** Who starts the daemon? Who stops it? What happens when VS Code closes? The extension currently retries a connection every 3 seconds but never spawns the daemon itself.

2. **Auto-start trigger.** The extension activates on `onStartupFinished`. Should it immediately try to connect and lock a serial port, or wait for an explicit user action?

3. **Command gating.** The extension design doc defines two modes: live (no manifest, scratch REPL) and project (`froth.toml`). Build and flash are meaningless in live mode. Should those commands be visible?

4. **Send File semantics.** ADR-037 established that the correct file-send is `reset` + eval whole file. Without `reset`, deleted definitions persist on-device. "Send File" currently just evals, which lies about device state.

5. **Flash and serial port ownership.** The daemon owns the serial port. Flashing requires the port. These conflict unless the daemon explicitly yields.

6. **`froth` no-args and packaging.** What does the bare `froth` command do? How do users install the toolchain?

## Decisions

### 1. Daemon lifecycle: ownership-based

Three options were considered:

**A) Always auto-start from VS Code.** Extension spawns `froth daemon start` on activation. Simple for novices.
Problem: silently spawns a background process that locks a serial port. Surprising. Fights the principle that VS Code should not have side effects on activation.

**B) Ownership-based lifetime.** If the extension started the daemon, the daemon is ephemeral (stops when VS Code deactivates). If the user started it manually (`froth daemon start`), it persists. The extension can offer to promote an ephemeral daemon to persistent via a "Keep Running" action.
Problem: more complex. Two daemon states to track.

**C) Always manual.** Users must run `froth daemon start` themselves. Extension only connects.
Problem: too much friction for novice users. "Why doesn't it work?" Workshop failure mode.

**Decision: B, ownership-based, but with a pragmatic simplification.**

The extension spawns the daemon on first Froth action (not on activation). It records that it owns the daemon (via PID tracking). On deactivation, it sends a stop signal. If the user started the daemon before VS Code, the extension connects to it and does not stop it on deactivation.

Implementation: the extension checks for `~/.froth/daemon.pid` before spawning. If the PID file exists and the process is alive, it connects to the existing daemon and marks itself as a guest (no stop on deactivate). If not, it spawns `froth daemon start --background` and marks itself as the owner.

This keeps the daemon visible to power users (`froth daemon status` always works) while invisible to novices (VS Code just works on first action).

### 2. Auto-start trigger: first Froth action, not activation

The extension activates on `onStartupFinished` (to show the status bar), but does NOT attempt daemon connection or spawn until the user performs a Froth action:

- Runs any Froth command (send selection, send file, etc.)
- Clicks the status bar item
- Explicitly runs `froth.connect` (for users who want to monitor device output without editing)
- Opens a `.froth` file (if language ID is registered)

`froth.connect` is a standalone command so users can connect just to read board output via the console, without needing to send code first.

Until first action, the status bar shows "Froth: Idle" (neutral, not an error state). After first action, the extension spawns or connects to the daemon and transitions to connected/disconnected/no-daemon.

This prevents the "opened the wrong workspace, now a serial port is locked" problem.

### 3. Command gating: live mode vs project mode

Commands are partitioned by mode. Mode is determined by the presence of `froth.toml` in the workspace root.

**Always available (live mode):**
- `froth.connect` (spawn/connect to daemon, detect device)
- `froth.sendSelection` (Cmd+Enter, send selection or current line)
- `froth.sendFile` (reset + eval whole file, see decision 4)
- `froth.reset` (clear device to stdlib baseline)
- `froth.interrupt` (send 0x03 to stop runaway execution)
- `froth.wipeSnapshot` (reset + erase snapshot storage)
- `froth.doctor` (run diagnostics, show actionable panel)
- `froth.showConsole` (reveal output channel)
- `froth.makeProject` (scaffold `froth.toml` from current file or workspace, transition to project mode)

`froth.makeProject` provides a zero-friction transition from live to project mode. User has an orphan `.froth` file open, runs "Froth: Make Project", and gets a `froth.toml` in the file's parent directory (or workspace root) with defaults derived from the connected device (board, platform) if available. Build/flash commands become visible immediately. The manifest schema is Phase 6 work, but the command can ship early with a minimal template.

**Project mode only (requires `froth.toml`):**
- `froth.build` (run `froth build`)
- `froth.flash` (quiesce daemon, run `froth flash`, reconnect)
- `froth.sync` (future: form-level sync engine)

Project-mode commands are registered but hidden from the command palette when no manifest exists. They are NOT absent (power users can still invoke them programmatically).

**Recovery commands (always available, prominently placed):**
- `froth.wipeSnapshot` also available via status bar context menu
- `froth.doctor` linked from any error notification
- If autorun bricks the board: the extension detects repeated reconnect failures and suggests safe boot + wipe

### 4. Send File = reset + eval

`froth.sendFile` implements the ADR-037 workflow:

1. Send `RESET_REQ` to daemon (which forwards to device as a link frame).
2. Wait for `RESET_RES` (device confirms reset to stdlib baseline).
3. Send `EVAL_REQ` with the full file contents.
4. Display result in console.

This compound operation is atomic from the user's perspective. The console shows:
```
[froth] reset
> (file contents preview)
[3]
```

**Until `reset` is implemented on the device**, `froth.sendFile` falls back to eval-only and the console emits a warning: `[froth] warning: reset not available, definitions are additive`. This is honest. Once `reset` lands (next firmware priority), the warning disappears and Send File becomes correct by construction.

`froth.sendSelection` does NOT reset. It is an additive REPL operation, consistent with typing at a prompt.

### 5. Flash quiesce protocol

Flashing requires the serial port, which the daemon owns. Two options:

**A) Daemon orchestrates flash.** New RPC method `flash` that takes build output path. Daemon closes serial, runs `esptool.py` or `idf.py flash`, reopens serial, reconnects.
Problem: the daemon must understand build system internals. Fragile. Ties Go code to ESP-IDF toolchain details.

**B) Daemon yields port on request.** New RPC method `quiesce`. Daemon closes serial port, enters quiesced state (rejects eval/info/hello, responds to status with `quiesced: true`). After flash, the extension sends `resume` RPC. Daemon re-opens serial, reconnects, broadcasts `connected`.
Problem: two-phase protocol. Extension must handle the "flash failed, daemon stuck in quiesced state" case.

**Considered and rejected: flash via daemon PTY.** In Phase 2 the daemon exposes `~/.froth/pty`. Could the flasher use the PTY as its serial port? No. Flashing requires raw access to the physical port: DTR/RTS toggling for boot ROM entry, timing-sensitive reset sequences, baud rate changes, binary payloads. The daemon's PTY is a filtered byte pipe, not a hardware serial port. esptool cannot flash through it.

**Decision: B, with timeout safety net.** Quiesce has a configurable timeout (default 120s). If no `resume` arrives within the timeout, the daemon auto-resumes and attempts reconnection. This handles the "flash failed and the user walked away" case.

Extension implements `froth.flash` as:
1. `quiesce` RPC
2. Run `froth flash` as a terminal task (visible to user)
3. On task completion (success or failure): `resume` RPC
4. Daemon reconnects, extension updates status bar

### 6. `froth` no-args and packaging

**`froth` with no args: help + status.**

```
$ froth
Froth 0.1.0

Commands:
  send        Send Froth source to device
  info        Show device info
  doctor      Check system setup
  build       Build firmware (requires froth.toml)
  flash       Flash firmware to device
  daemon      Manage background service

Daemon: running (pid 12345)
Device: ESP32 DevKit V1 on /dev/cu.usbserial-0001
```

If the daemon is running, it shows device status. If not, it shows nothing extra. The POSIX REPL is available via `froth repl` (or a future subcommand), not the default.

**Packaging:**

- `brew install froth` installs the Go CLI binary. Single binary, no dependencies.
- VS Code extension is installed from the marketplace. It discovers `froth` on PATH.
- If the extension can't find `froth`, the status bar shows "Froth: CLI not found" with a link to installation instructions.
- Workshop: boards are pre-flashed, `froth` and the extension are pre-installed on lab machines. No install step during the workshop itself.

**PATH discovery:** The extension searches for `froth` in: (1) `PATH`, (2) `/opt/homebrew/bin/froth`, (3) `/usr/local/bin/froth`, (4) a user-configurable `froth.cliPath` setting. This handles the macOS GUI PATH vs shell PATH discrepancy.

## Consequences

- `reset` becomes the immediate next firmware priority, before the streaming serializer (ADR-038). Without it, Send File is dishonest.
- ADR-035 is not superseded but gains a lifecycle addendum (ownership-based start/stop).
- The extension gains several new commands beyond the current skeleton. These are Phase 5 work except for `reset` integration, which is near-term.
- The `quiesce`/`resume` RPC methods are new daemon protocol additions. They do not change the existing methods.
- The daemon gains `--background` flag support (daemonize, write PID file, detach from terminal). The current `froth daemon start` runs in foreground; `--background` is additive.
- `froth.toml` detection logic must exist in the extension for mode gating. The manifest schema is deferred (Phase 6).
- Workshop logistics simplify: pre-install everything, pre-flash boards, users open VS Code and press Cmd+Enter.
- The POSIX REPL remains a developer tool under `froth repl`, not the main entry point. This aligns with "the device is the computer."

## Implementation priority

1. `reset` primitive + `RESET_REQ`/`RESET_RES` protocol messages (firmware, next)
2. `reset` RPC method in daemon (Go)
3. `froth.sendFile` updated to reset + eval (TypeScript)
4. `froth.reset`, `froth.interrupt`, `froth.wipeSnapshot` commands (TypeScript)
5. Lazy daemon spawn from extension (TypeScript)
6. `quiesce`/`resume` RPC + `froth.flash` command (Go + TypeScript, project mode only)
7. `froth` no-args help + status (Go)
8. `--background` flag for daemon start (Go)

## References

- ADR-035: Host daemon architecture
- ADR-037: Host-centric deployment with overlay user program
- ADR-038: Streaming snapshot serializer
- docs/archive/concepts/vscode-extension-design.md: full extension vision, two modes, workshop scope
- docs/archive/concepts/host-tooling-roadmap.md: phased plan
