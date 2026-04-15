# Frothy ADR-111: VS Code Extension-Owned Control Session

**Date**: 2026-04-12
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 7.5, 8.1, 8.2, 8.3, 8.4, 9
**Roadmap milestone(s)**: post-M10 follow-on transport simplification, editor loop
**Inherited Froth references**: `docs/adr/035-host-daemon-architecture.md`, `docs/adr/039-host-tooling-ux-and-daemon-lifecycle.md`, `docs/archive/adr/042-extension-ux-and-local-target.md`, `docs/archive/concepts/vscode-extension-design.md`

## Context

Frothy ADR-110 intentionally replaced the inherited daemon-plus-mux direction
with a direct single-owner control session:

- one owner at a time
- raw REPL by default
- explicit `.control` entry for structured tooling
- no concurrent human/tool stream ownership
- no daemon needed to preserve state because the image already is the state

The checked-in VS Code extension and its surrounding docs still reflected the
older Froth editor story:

- daemon-owned serial connection
- Unix socket JSON-RPC from the extension
- background lifecycle and ownership rules
- local POSIX mode as a user-facing editor target
- reconnecting/no-daemon/project-mode UI branches

Those assumptions now conflict with accepted Frothy transport policy.

The editor also needs one more explicit decision: `Send File` must remain
honest. The accepted Froth/Frothy tooling path is whole-file `reset + eval`.
Until the Frothy kernel exposes a control-session `RESET`, the extension must
not pretend that a reset happened.

## Options Considered

### Option A: Keep the daemon-era extension and adapt it slowly

Leave the extension on the daemon/socket path and treat Frothy ADR-110 as a
future cleanup.

Trade-offs:

- Pro: less short-term rewrite work.
- Con: keeps the most fragile part of the inherited host stack in the main
  editor path.
- Con: contradicts Frothy ADR-110's stated direction.
- Con: encourages future work on the wrong ownership model.

### Option B: Extension talks to serial directly in TypeScript

Remove the daemon, but make the extension own serial discovery, prompt
acquisition, `.control`, framing, request sequencing, and interrupt handling.

Trade-offs:

- Pro: no extra helper process.
- Pro: superficially smaller than the daemon path.
- Con: duplicates transport logic already proven in Go.
- Con: makes the extension the protocol owner instead of a thin client.
- Con: increases platform-specific editor risk for no product gain.

### Option C: Extension-owned helper child over stdio

Keep the VS Code extension thin, but replace the daemon/socket boundary with an
extension-owned helper child process:

- VS Code spawns `frothy tooling control-session`
- the helper owns one direct control session
- the extension talks to the helper with newline-delimited JSON over stdio
- the helper exits or is discarded with the editor session
- no shared background ownership exists outside the current VS Code window

Trade-offs:

- Pro: matches Frothy ADR-110's single-owner model.
- Pro: reuses the checked-in `frothycontrol` session logic instead of
  reimplementing transport in TypeScript.
- Pro: keeps the extension small and explicit.
- Pro: keeps a stable place to add a control-session `RESET` for honest
  file-send.
- Con: introduces a small helper protocol that must be versioned carefully.
- Con: user-facing local POSIX mode becomes test-only in the first Frothy cut.

## Decision

**Option C.**

Frothy adopts an extension-owned control helper for the VS Code path.

The accepted editor boundary is:

- the extension owns at most one helper child per window
- the helper owns at most one control session at a time
- there is no daemon, Unix socket, PID file, background reconnect loop, or
  shared-client transport owner in the Frothy editor path
- the helper protocol is newline-delimited JSON over stdio, not JSON-RPC
- the extension stays board-first and live-image-first

The first Frothy VS Code cut is intentionally narrow:

- connect, disconnect, send line, send file, interrupt
- structured `WORDS` and `SEE`
- snapshot commands and simple inspection commands
- no project build/flash workflow
- no user-facing local POSIX mode
- no sync ledger or daemon-era state machine

`Send File` remains conceptually `reset + eval`.

The helper protocol therefore reserves an explicit `reset` request from day
one. Until the Frothy kernel exposes a matching control-session command, the
helper returns a structured `reset_unavailable` error and the extension may
only continue with an explicitly unsafe additive fallback after warning the
user. Until the remaining slice-2 control commands land on-device, helper-level
`save`, `restore`, `wipe`, `core`, and `slotInfo` may be implemented by
issuing normal `EVAL` requests for those built-ins while keeping the helper
protocol stable.

This ADR supersedes the daemon-era editor assumptions in the inherited Froth
docs listed above wherever they conflict with Frothy ADR-110 and this decision.

## Consequences

- Frothy now has one coherent editor story: extension-owned helper over the
  direct single-owner control session.
- The old VS Code extension code can still exist during migration, but the new
  Frothy path must not depend on or extend the daemon.
- `frothycontrol` becomes the shared transport owner for both proofs and the
  editor helper.
- File-send remains honest about current limitations: the protocol shape
  includes `reset`, but the first extension cut must warn before falling back
  to additive send until kernel support lands.
- Extension branding and command/settings names align with `Frothy`, the
  installed CLI is now `frothy`, and VS Code keeps only narrow legacy `froth`
  fallback per Frothy ADR-120.
- User-facing local mode is deferred instead of dragging old daemon/local-target
  complexity into the first Frothy editor cut.
- Future on-device slice-2 commands can replace helper-side `EVAL` wrappers
  without changing the extension/helper boundary.

## References

- `docs/adr/109-repo-control-surface-and-proof-path.md`
- `docs/adr/110-single-owner-control-session-transport.md`
- `tools/cli/internal/frothycontrol/session.go`
- `tools/cli/internal/frothycontrol/control_session.go`
- `tools/vscode/src/control-session-client.ts`
