# Froth Host Tooling Roadmap

**Date:** 2026-03-16
**Purpose:** Track all host tooling work items, decisions, and dependencies. Contributors should read this file to understand what's built, what's next, and what's deferred.

## Code Standards

This is non-negotiable. All code in this project must hold up to open source release standards. The bar is set by projects like the RP2040 SDK, SQLite, and Redis: code that strangers will read, judge, and build on for years.

**Style:**
- Write like the RP2040 datasheet. Informal, expressive, technically precise.
- No em-dashes. No filler phrasing ("straightforward", "leverage", "robust", "comprehensive", "it's important to note"). Just say the thing.
- Comments explain why, not what. If the code is clear, no comment is needed.
- Short declarative sentences. One idea per sentence.

**Code quality:**
- Extremely readable. A contributor should understand any function in under 30 seconds.
- Ergonomic APIs. Callers should not need to read the implementation to use a function correctly.
- Well-architected. Clean module boundaries, minimal coupling, no circular dependencies.
- No technical debt. Do not ship code that needs a follow-up cleanup pass. If it's not right, fix it now or don't merge it.
- Error handling is explicit and complete. No silent failures, no swallowed errors.
- Resource management is airtight. Every open gets a close. Every alloc has a free path.

**Go code:** standard gofmt, error wrapping with `%w`, no third-party deps beyond `go.bug.st/serial`.
**TypeScript code:** strict mode, no `any` types, explicit error handling, minimal dependencies.
**C code:** C11, `froth_` prefix, snake_case, clang-format (project .clang-format when established).

**Review discipline:** Every tranche of work gets a self-review and a focused review before commit. Fix everything flagged. No exceptions.

## Status Key

- [x] Done and committed
- [~] In progress
- [ ] Not started
- [D] Deferred (post-workshop or later)

## ADR Index (host tooling)

| ADR | Title | Status |
|-----|-------|--------|
| 033 | FROTH-LINK/1 binary transport | Accepted, implemented |
| 034 | Console multiplexer | Accepted, implemented |
| 035 | Host daemon architecture | Accepted, Phase 1 implemented |
| 036 | Protocol sideband probes | Accepted, not yet implemented |

## Phase 1: CLI Commands (DONE)

All committed in `668187b`.

- [x] `froth doctor` — Go version, cmake, make, serial ports, ESP-IDF, device probe
- [x] `froth build` — POSIX (cmake+make), ESP-IDF (idf.py build), project root auto-detection
- [x] `froth flash` — ESP-IDF with port detection, POSIX prints binary path
- [x] `--port`, `--target` flags
- [x] `internal/serial`: `IsCandidate`, `ListCandidates`, `Read`, `SetReadTimeout`, `Path`

## Phase 2: Daemon (DONE)

All committed in `668187b`. ADR-035.

- [x] `internal/daemon/daemon.go` — lifecycle, serial ownership, reconnect, device transactions
- [x] `internal/daemon/rpc.go` — JSON-RPC 2.0 server, per-client connection, method dispatch
- [x] `internal/daemon/client.go` — client dialer, async readLoop, convenience methods
- [x] `froth daemon start|stop|status`
- [x] CLI routing: `--serial`, `--daemon` flags, auto-detect socket
- [x] `info` and `send` work through daemon with serial fallback
- [x] Events: console, connected, disconnected, reconnecting
- [x] Concurrency review: 6 issues found and fixed

### Daemon Phase 2 (post-workshop, first priority)

- [ ] Virtual PTY for raw REPL access (`~/.froth/pty`)
- [ ] Write serializer: pause PTY during RPC transactions
- [ ] Ctrl-C bypass during paused PTY (send 0x03 immediately)
- [ ] PTY announced in daemon status output

## Phase 3: VS Code Extension Skeleton (DONE)

Design doc: `docs/archive/concepts/vscode-extension-design.md`

### Skeleton scope (Mar 21 workshop)

- [x] `tools/vscode/package.json` — extension manifest
- [x] `tools/vscode/tsconfig.json` — TypeScript config
- [x] `tools/vscode/src/extension.ts` — activate/deactivate, commands, UI
- [x] `tools/vscode/src/daemon-client.ts` — JSON-RPC client over Unix socket
- [x] Connect to daemon on activation (auto-detect `~/.froth/daemon.sock`)
- [x] Status bar item: connection state (connected/disconnected/no daemon)
- [x] Command: `froth.sendSelection` (Cmd+Enter) — send selection or current line
- [x] Command: `froth.sendFile` — send entire file
- [x] Output channel: console events + eval results as transcript
- [x] Disconnect/reconnect handling (daemon events)
- [x] `npm run compile` passes with zero errors

### Build/dev workflow

- `cd tools/vscode && npm install && npm run compile`
- F5 in VS Code for Extension Development Host
- No third-party runtime dependencies (just `@types/vscode`, `@types/node`)

## Phase 4: Protocol Sideband Probes (DEFERRED — after persistence validation)

ADR-036. Depends on device-side kernel work.

### Prerequisite: ESP32 0x03 bug fix

- [ ] Fix `platform_key()` on ESP-IDF: must not treat 0x03 as interrupt before mux classifies it
- [ ] Mux owns byte classification, not platform layer

### Device-side probe implementation

- [ ] WATCH_REQ/WATCH_RES message types (0x09/0x0A)
- [ ] EVENT(PROBE) emission at safe points
- [ ] Mux refactor: persistent frame state, split parse from dispatch
- [ ] Safe-point probe check in mux (not in `platform_check_interrupt`)
- [ ] Rate limiting (configurable, default every 100 safe-point checks)
- [ ] Read-only constraint enforced: slot values, DS snapshot, VM metadata only

### Host-side probe support

- [ ] Daemon: WATCH_REQ on client subscribe, coalesce EVENT(PROBE), broadcast to clients
- [ ] CLI client: probe subscription support
- [ ] VS Code: Probes panel, pinned values with live updates

## Phase 5: Full VS Code Features (DEFERRED — iterative post-workshop)

- [D] Inline eval results (ghost text annotations)
- [D] Gutter badges (unsent/synced/failed/superseded/unknown)
- [D] Form-level sync engine (parse forms, hash, send changed, record results)
- [D] Sync ledger in daemon (file URI, form hash, word name, result, session ID)
- [D] Hover: stack effect, origin, definition match
- [D] Sidebar panels: Device, Sync, Words, Probes
- [D] Structured editing: form selection, bracket-aware movement, quotation folding
- [D] Syntax highlighting (TextMate grammar for .froth files)
- [D] First-run flow: Open Device / New Project
- [D] Actionable Doctor panel
- [D] Safe boot / snapshot rescue (one-click)
- [D] FFI metadata bridge (extract stack effects from C builds)
- [D] Language server (diagnostics, completions, hover, go-to-def)

## Phase 6: Project Infrastructure (DEFERRED)

- [D] froth.toml manifest spec (ADR needed)
- [D] `froth new` project scaffolding
- [D] Dependency management (Git-first, lockfile)
- [D] Board catalog, profile/layer validation
- [D] `froth sync` workspace-to-device synchronization

## Dependencies

```
Phase 3 (VS Code skeleton)
  └─ depends on: Phase 2 (daemon) ✓

Phase 4 (probes)
  ├─ depends on: ESP32 0x03 bug fix (kernel work)
  ├─ depends on: mux refactor (kernel work)
  └─ depends on: persistence validation on ESP32 (kernel work, higher priority)

Phase 5 (full VS Code)
  ├─ depends on: Phase 3 (skeleton)
  ├─ depends on: INSPECT_REQ on device (for sync certainty)
  └─ some features depend on: Phase 4 (probes)

Phase 6 (project infra)
  └─ depends on: Phase 5 (at least partial)
```

## Decisions Log

Key decisions made during Mar 16 session:

1. **Daemon is dual-mode**: CLI subcommand (`froth daemon start`) AND callable from scripts. Logic in `internal/daemon/`.
2. **Daemon Phase 1 is RPC-only**: No PTY. Architecture designed for PTY addition. PTY is first post-workshop priority.
3. **PTY rationale**: Not a nice-to-have. Opens the door to non-Froth-ecosystem serial consumers (Processing, Max/MSP, web apps, sensor hubs).
4. **IPC format**: JSON-RPC 2.0 over Unix domain socket, newline-delimited.
5. **CLI routing**: auto-detect daemon socket, `--serial` forces bypass, `--daemon` forces routing.
6. **Eval stays stop-and-wait**: General queue model rejected (UART overflow, reentrancy, byte ownership).
7. **Probes use subscription model**: WATCH_REQ installs watch list, EVENT(PROBE) emitted at safe points. Read-only. No eval mid-eval.
8. **VS Code extension is thin**: TypeScript, daemon-owned intelligence, no serial/protocol/build logic in extension.
9. **Sync is form-level**: Top-level form is the unit, not the file. Five honest sync states including "unknown."
10. **Sync ledger in daemon**: Not in the extension. Daemon tracks form hash, word name, send result, session ID.
11. **Two project modes**: Live (no manifest) and Project (froth.toml). Build/flash disabled without manifest.
12. **0x03 bug must be fixed**: platform_key on ESP-IDF intercepts 0x03 before mux. Blocks all mid-eval frame work.
