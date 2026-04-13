# ADR-042: Extension UX, Daemon Lifecycle, and Local Target

**Date**: 2026-03-18
**Status**: Accepted
**Spec sections**: Froth_Interactive_Development_v0_6 (Link Mode, host tooling), ADR-035 (daemon architecture), ADR-037 (host-centric deployment), ADR-039 (tooling UX and daemon lifecycle)

## Context

The VS Code extension exists but isn't ready for new users. Several problems:

1. The daemon must be started manually from the terminal. Workshop attendees and new users don't know what a daemon is.
2. There is no way to use Froth without hardware. The POSIX binary exists and accepts COBS frames via stdin/stdout through the same console multiplexer as ESP32, but the extension has no way to talk to it.
3. The sidebar uses TreeView rows as fake action buttons. Icons don't render. The UI doesn't look or feel like native VS Code.
4. There is no build/flash workflow from the editor.
5. The extension auto-connects on activation, locking the serial port before the user has done anything intentional.

Three user personas drive the design:

- **Workshop attendee**: Pre-flashed ESP32. Installs extension. Wants LED blinking in under 60 seconds. Knows nothing about daemons or serial ports.
- **Curious developer**: No hardware. Wants to type `1 2 +` and see `[3]`. Expects it to work like a Python REPL.
- **Experienced user**: Has hardware, wants full build/flash/send/interrupt/save workflow with target selection.

## Options Considered

### Option A: Extension talks directly to serial port and local process

The extension manages serial I/O and process spawning itself, bypassing the daemon entirely.

Trade-offs:
- Pro: simpler architecture (no daemon).
- Con: the extension becomes the serial owner. Other tools (minicom, Processing) can't use the port simultaneously. Contradicts ADR-035's design.
- Con: TypeScript serial I/O is fragile (node-serialport, platform quirks).
- Con: duplicates the frame parsing, console separation, and reconnection logic already in the Go daemon.

### Option B: Daemon as the universal transport layer

The extension always talks to the daemon via JSON-RPC. The daemon manages both serial devices and local POSIX processes as transport backends. The extension doesn't know or care which backend is active.

Trade-offs:
- Pro: extension stays thin (TypeScript, JSON-RPC only).
- Pro: one API for both hardware and local targets.
- Pro: daemon handles reconnection, console broadcast, frame parsing.
- Pro: other tools can still connect to the daemon for console access.
- Con: requires the daemon to support a "local" transport mode.
- Con: adds a process dependency (daemon must be running).

### Option C: Hybrid (daemon for hardware, direct pipe for local)

The extension talks to the daemon for hardware targets but spawns and pipes to a local POSIX process directly for local mode.

Trade-offs:
- Pro: local mode has no daemon dependency.
- Con: two code paths in the extension (JSON-RPC vs direct pipe). Two console models. Two error recovery strategies. Complexity doubles.

## Decision

**Option B.** The daemon is the universal transport layer. The extension always speaks JSON-RPC. Local mode runs through the daemon.

### Daemon lifecycle

The extension starts the daemon lazily on first Froth action, not on activation. The process is:

1. Extension activates (on `.froth` file open or startup).
2. Extension shows neutral empty state. No serial port locked. No daemon started.
3. User triggers a Froth action (Send Selection, Send File, Connect Device, Try Local).
4. Extension checks if daemon is already running (try connecting to socket).
5. If not running, extension spawns `froth daemon start --background`.
6. Extension connects to the daemon via JSON-RPC.

If the extension started the daemon, it is ephemeral: stops when the last client disconnects or on extension deactivation after a short grace period. If the user started the daemon manually (`froth daemon start` in terminal), the extension is a guest and must not stop it.

### First-run flow

On first `.froth` file open with no active connection:

1. Sidebar shows `viewsWelcome` content with four doors:
   - **Connect Device** — probe for USB-serial, auto-connect if exactly one board found, otherwise show quick pick
   - **Try Local** — start local POSIX target, clearly labeled
   - **New Project** — scaffold a `froth.toml` project (future)
   - **Run Doctor** — check prerequisites (Go, cmake, ESP-IDF, serial ports)

2. One-time non-modal notification: "Froth: Open a .froth file and press Cmd+Enter to evaluate."

If Connect Device finds no board: show "Try Local", "Run Doctor", "Retry".

### Local mode

The daemon gains a "local" transport backend that spawns a POSIX Froth binary as a child process and communicates via stdin/stdout (COBS frames + console text, same wire format as serial). The daemon's serial read loop, frame parser, and console broadcaster work identically.

The extension sees `Target: Local POSIX` in the status bar and device view. The status bar says `Froth: Local POSIX`, not `Froth: Connected`. The user always knows whether they're talking to real hardware or a local process.

Local mode is never entered silently. It requires an explicit user action ("Try Local" button or target selector). The only exception: if the user has never connected to anything, has no saved target, no USB-serial candidates are detected, and they hit Send, then start local with a sticky notification: "No device found. Running locally. [Connect Device]".

### Sidebar and action placement

The TreeView body shows status and metadata only:
- Target (device name or "Local POSIX")
- Port (or "stdin/stdout" for local)
- Heap usage (with overlay)
- Slot count (with overlay)
- Snapshot state (if applicable)
- Connection/reconnect state

Actions go in native VS Code command surfaces:

**View title (icon buttons):**
- Connect / Select Target
- Interrupt
- Refresh

**View title (overflow menu):**
- Reset
- Save
- Wipe
- Run Doctor

**Editor title (on `.froth` files):**
- Send Line/Selection (Cmd+Enter)
- Send File (Cmd+Shift+Enter)

**Status bar:**
- Connection state indicator (left-aligned, always visible)
- Interrupt button (red, appears only in "running" state)

No WebView. TreeView + native menus + status bar is sufficient and stays native.

### Build and flash

Build/Flash shells out to `froth build` and `froth flash`. Not available in live/scratch mode, only in project mode (gated by `froth.toml`).

Target selection splits into two concepts:
- **Runtime target**: what the extension talks to right now (ESP32 device, Local POSIX). Shown in UI.
- **Build target**: what `froth build` compiles for. Defined in `froth.toml` (platform, board, profile).

Build runs as a visible VS Code task. Flash runs `froth flash`, which internally quiesces the daemon connection, flashes, then reconnects.

### Workshop path

For 15 pre-flashed ESP32 boards, the user experience:

1. Plug in the board.
2. Open VS Code with Froth extension installed.
3. Open the provided `blink.froth` file.
4. Press Cmd+Shift+Enter.
5. LED blinks.

Behind the scenes: extension lazily starts daemon, daemon auto-discovers the single USB-serial device, HELLO handshake succeeds, Send File does reset + eval, LED blinks. No port picker (single board), no daemon terminal, no build step.

Primary visible chrome for workshop: Send, Interrupt, maybe Reset. Build, Flash, Wipe are hidden in overflow or absent.

## Consequences

- The daemon gains a "local" transport backend. This requires a new daemon subcommand or config option (`froth daemon start --local` or `froth daemon start --target local`).
- The extension is rewritten to use native VS Code command surfaces instead of TreeView action items.
- The extension gains `viewsWelcome` content for the first-run flow.
- The extension manages daemon lifecycle (lazy start, ephemeral stop).
- `froth.toml` becomes the gate for build/flash features (project mode). Live/scratch mode has no build/flash.
- The "device is the computer" principle is preserved: local mode is a development convenience, not a replacement for the device. The UI makes the distinction clear.

## Deferred

- `froth.toml` manifest format and project scaffolding.
- Multi-device support (multiple boards connected simultaneously).
- Watch mode (auto-send on file save).
- Remote daemon (daemon on a different machine, accessed over network).

## References

- ADR-035: Host daemon architecture (daemon design, Phase 1 RPC, Phase 2 PTY)
- ADR-037: Host-centric deployment (Send File = reset + eval, boot sequence)
- ADR-039: Host tooling UX and daemon lifecycle (daemon start/stop, auto-start)
- Froth_Interactive_Development_v0_6.md: Link Mode (COBS binary framing)
- docs/archive/concepts/vscode-extension-design.md: original extension design vision
- docs/archive/concepts/host-tooling-roadmap.md: phased implementation plan
