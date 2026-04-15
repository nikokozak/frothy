# Froth VS Code Extension Design

**Date:** 2026-03-16
**Status:** Accepted vision. Workshop skeleton in progress.
**Audience:** Implementers and future contributors.
**Related:** ADR-035 (daemon), ADR-036 (probes), tooling-and-link-architecture-proposal-2026-03.md

## Core Principle

The device is the computer. The extension is the best cockpit for that computer. It is not a code editor with an upload button. It is a live image workbench for a running microcontroller.

Source files are a persistence and organization layer on top of a live system. The editor is a window into a running device, not a build pipeline frontend.

## Two Layers, Two Feedback Loops

Every Froth project has two layers with fundamentally different feedback loops:

**C layer** (platform, board, FFI): compiled, flashed, static. Feedback loop: minutes. Changes require rebuild, reflash, reconnect. Changes rarely.

**Froth layer** (definitions, application logic, experiments): evaluated live on device. Feedback loop: instant. Changes take effect the moment you press Cmd+Enter. Changes constantly.

The extension treats these as distinct worlds with a visible boundary. It never pretends they are the same.

## Project Model

Three modes of use:

### 1. Live mode (no manifest)

No froth.toml, no project directory. Just a device connection. Scratch buffers, exploratory REPL, send selections. The device's snapshot IS the project. This is the workshop default.

### 2. Froth-only project

A directory with froth.toml and .froth files. No custom C code. Uses an existing board package.

```
my-synth/
  froth.toml
  main.froth
  voices/
    saw.froth
    filter.froth
```

.froth files are sequences of top-level forms (definitions, expressions). They are not compiled. They are sent to the device in dependency order. `main.froth` wires to `autorun`.

### 3. Full project (Froth + custom FFI)

Same as above, plus C source for custom FFI bindings.

```
my-hardware/
  froth.toml
  main.froth
  ffi/
    imu.c
    imu.h
  lib/
    gestures.froth
```

froth.toml declares FFI sources. Build system compiles them alongside the kernel.

### Manifest (froth.toml)

```toml
[project]
name = "blink-demo"
board = "esp32-devkit-v1"
platform = "esp-idf"
profile = "interactive"
entry = "src/main.froth"

[layers]
interactive = true
persist = true
string_lite = true

[dependencies]
gpio-utils = { git = "https://github.com/example/froth-gpio-utils", rev = "v0.1.0" }
```

Without a manifest, build/flash are disabled. The extension does not guess.

Dependencies are Git-first plus lockfile. No registry. Local path overrides for development.

Project files can live outside the workspace root by explicit declaration (`path = "../lib/foo"`). No magical directory crawling.

## Dual-Layer Editing

### Editing .froth files

Full Froth experience: instant send, form sync, inline results, drift tracking, gutter badges.

### Editing C files (FFI, board)

The extension adds a thin awareness layer: "C layer. Changes require rebuild." It does not interfere with clangd or IntelliSense.

When a C file is saved, the device panel shows "C layer out of date." The user triggers "Rebuild and Flash" when ready. After reflash, the extension reconnects and re-syncs Froth definitions.

After a successful build, the extension ingests FFI metadata from the built image. This gives hover, stack effects, and completions for C-defined words without parsing C.

The extension surfaces the boundary visually. Every word shows its origin: `primitive`, `stdlib`, or `overlay`.

## Sync Model

### Unit of synchronization

The top-level form, not the file. A form is a colon definition (`: double dup + ;`), a bare expression (`5 double .`), or a comment block.

### Sync engine

All send operations (send selection, send form, send file, sync workspace) compile down to the same engine: parse forms, hash them, send changed forms in dependency order, record results per form and per defined word.

### Sync ledger

Owned by the daemon, not the extension UI. Tracks: file URI, form hash, word name, last send result, device session ID, timestamp.

### Sync states

Five states only:

- **unsent**: exists in file, never sent to device (gray gutter)
- **synced**: sent to device, matches file content (green gutter)
- **failed**: send attempted, eval returned error (red gutter)
- **superseded**: sent previously, file content changed since (yellow gutter)
- **unknown**: device may have diverged (REPL input, another client, reboot, restore, snapshot load)

On reconnect, all states downgrade to **unknown** until the extension can verify. Until INSPECT exists on-device, reconnect cannot confirm sync state. This is honest. Do not lie about sync certainty.

True drift detection needs device-side provenance or form-hash metadata. That requires its own ADR.

## Inline Features

### Inline eval results

Cmd+Enter sends the form at cursor. Ghost text annotation appears at the end of the line: `=> [42]` or `=> error(2) in "perm"`. Definitions show `=> defined`.

### Gutter badges

Per-definition icons: unsent (gray), synced (green), failed (red), superseded (yellow), unknown (gray outline).

### Hover

Word hover shows: stack effect, origin (primitive/stdlib/overlay), current device definition, and whether device version matches buffer.

### Live probes (ADR-036)

The user pins a slot name. The extension subscribes via WATCH_REQ. During eval, the device emits EVENT(PROBE) at safe points. The extension shows current values inline or in the Probes panel.

Between evals, the daemon can evaluate arbitrary probe expressions via normal EVAL_REQ.

Probes are read-only during eval: slot values, DS snapshot, VM metadata. No expression evaluation mid-eval.

### Structured editing

Form selection, bracket-aware movement, quotation folding, visual preview for `p[...]/perm` stack rewrites. Not a full structural editor.

### Scratchpad

Exists for exploratory work. Jupyter-style notebooks are optional, useful for workshops. Wrong default for real embedded work.

## Panels and UI

### Console (bottom panel)

Raw device output, REPL transcript, interrupt messages, reconnect events. In live mode, the console IS the REPL (once daemon PTY lands).

### Sidebar panels

- **Device**: board, port, profile, layers, firmware identity, heap, overlay bytes, snapshot status, reconnect state, safe-boot actions.
- **Sync**: form ledger and drift display, not just a green checkmark.
- **Words**: searchable inspector. Origin, stack effect, definition body, file origin, device/workspace mismatch.
- **Probes**: pinned values with live updates.

### Status bar

Always shows: connection state, mode (live/project), snapshot dirty indicator. One-click Interrupt button.

### No webview zoo

Use tree views, editors, and inline decorations. Webviews only for one or two rich inspectors.

## First-Run and Provisioning

### Welcome flow

Two doors: **Open Device** (live mode, scratch workspace, no manifest) and **New Project** (board, profile, layers, template, destination).

### Doctor

Actionable. Each problem gets a fix button: missing SDK, broken daemon, no serial permission, board not found, wrong profile, stale build.

### Recovery

Safe boot and snapshot rescue are one click. If autorun bricks the board, the extension guides the user into safe boot and offers Wipe Snapshot immediately.

## Code Standards

All extension code must hold up to open source release standards. The bar is projects like the RP2040 SDK, SQLite, and Redis: code that strangers will read, judge, and build on.

- TypeScript strict mode. No `any` types. Explicit error handling.
- Minimal dependencies. `@types/vscode` and `@types/node` only. No frameworks.
- Comments explain why, not what. If the code is clear, no comment.
- No em-dashes. No filler voice ("straightforward", "leverage", "robust"). Short declarative sentences.
- Every function readable in under 30 seconds. Clean module boundaries.
- No technical debt. If it's not right, fix it now.
- Review every tranche with self-review and focused review before commit.

See `docs/archive/concepts/host-tooling-roadmap.md` for the full code standards section.

## Architecture

### The extension is thin

TypeScript. Talks only to the daemon via JSON-RPC. Does not own serial logic, protocol definitions, build system knowledge, or parsing rules. Those belong in the daemon and CLI.

### Intelligence lives in the daemon

The daemon owns: serial connection, sync ledger, probe subscriptions, build orchestration, event fanout. The extension renders state and sends commands.

### Language server (future)

Not part of the skeleton. When it exists, it handles: diagnostics, completions, hover, go-to-definition, form boundary detection. Lives in the daemon initially, may split to a separate process later.

## Workshop Skeleton Scope

The minimal VS Code extension for Mar 21:

1. Connect to daemon on activation (auto-detect socket)
2. Status bar item showing connection state
3. Commands: Send Selection/Line (Cmd+Enter), Send File
4. Output channel: console events + eval results as running transcript
5. Disconnect/reconnect handling

Everything else (sync model, gutter badges, inline eval, probes, panels, project scaffolding) is post-workshop iteration on this foundation.

## What This Does NOT Include

- No visual block editor or node graph. Froth is text.
- No debugger (step/breakpoints). Froth's model is eval + inspect + catch/throw.
- No AI code completion. The word list and stack effects are the completion source.
- No syntax highlighting in the skeleton (TextMate grammar is separate work).
- No multi-device support (daemon is single-device per ADR-035).

## Known Gaps Requiring Future ADRs

- **Sync provenance**: how to detect device/workspace drift after reconnect. Needs device-side form-hash or inspect support.
- **Project manifest schema**: full froth.toml spec (board catalog, layer validation, dependency resolution).
- **FFI metadata bridge**: format for extracting stack effects and word names from C builds.
