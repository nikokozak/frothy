# ADR-044: Project System, Include Resolution, and CLI Architecture

**Date**: 2026-03-20
**Status**: Accepted
**Spec sections**: Froth_Interactive_Development_v0_5 (deployment model), ADR-037 (host-centric deployment), ADR-043 (transient strings), ADR-035 (daemon architecture), ADR-039 (host tooling UX)

## Context

Froth has a working kernel, host CLI, daemon, and VS Code extension. The deployment model (ADR-037) supports two paths: EVAL via the link protocol (fast iteration) and FROTH_USER_PROGRAM embedded at build time (firmware bake). Both operate on a single flat source string.

As programs grow beyond one file, this model breaks. There is no way to split a program across files, reuse code as libraries, or manage target-specific build configuration without manual CMake flag passing. The CLI currently finds the project root by walking up from CWD looking for `CMakeLists.txt + src/`, which is the kernel repo layout, not a user project layout.

The VS Code extension and CLI both need include resolution to implement "Send File" honestly: a file with includes should resolve and send the merged result, not just the entry file.

Additionally, the path from "install Froth" to "blink an LED" requires cloning the repo, installing three toolchains (C/CMake, Go, ESP-IDF), and knowing undocumented build commands. For a thesis claiming Froth is usable for embedded projects, this must be reduced to a single binary install and a handful of commands.

### Design constraints

1. **The device has no filesystem.** Include resolution, dependency management, and source merging are host-side concerns. The device receives one flat source string via EVAL or one embedded program via FROTH_USER_PROGRAM. This is non-negotiable.
2. **Froth's evaluator is unchanged.** The project system is a build/tool layer. No new syntax enters the language runtime. The evaluator, reader, and executor do not know about projects, manifests, or includes.
3. **The CLI is the distribution mechanism.** Users install one binary. The binary carries everything needed to build for any supported target (kernel source, board definitions, stdlib). Platform toolchains (ESP-IDF, Pico SDK) are third-party and installed on demand.
4. **Two user populations.** Newcomers use the CLI for everything. Experts may clone the kernel repo and build manually. Both workflows must work.

## Decision

A three-part system: **project manifest** (`froth.toml`), **host-side include resolution** (`\ #use` directives), and **CLI commands** that tie them together. The CLI embeds the kernel source and extracts it to a versioned SDK cache on first use.

---

## Part 1: Project Manifest (`froth.toml`)

### Format

```toml
[project]
name = "my-project"
version = "0.1.0"
entry = "src/main.froth"
froth = ">=0.1.0"

[target]
board = "esp32-devkit-v1"
platform = "esp-idf"

[build]
cell_size = 32
heap_size = 4096
slot_table_size = 128
tbuf_size = 1024
tdesc_max = 32

[ffi]
sources = ["src/ffi/my_sensor.c"]
includes = ["src/ffi/"]
defines = { MY_SENSOR_ADDR = "0x48" }

[platform.esp-idf]
sdkconfig_defaults = "sdkconfig.defaults"

[dependencies]
stepper = { path = "lib/stepper.froth" }
servo = { path = "lib/servo.froth" }
```

### Fields

**`[project]`**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | string | required | Project name. Used in build artifacts and device identification. |
| `version` | string | `"0.0.1"` | Semantic version of the project. Informational. |
| `entry` | string | `"src/main.froth"` | Path to the entry file, relative to `froth.toml`. This is the file that `froth send` and `froth build` start resolution from. |
| `froth` | string | none | Minimum CLI/kernel version required. Semver constraint (e.g., `">=0.1.0"`). The CLI checks this on every build/send and errors if incompatible. Optional in v1. |

**`[target]`**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `board` | string | `"posix"` | Board directory name. Must match a directory under `boards/` in the kernel source (embedded SDK or local repo). Examples: `"posix"`, `"esp32-devkit-v1"`. |
| `platform` | string | `"posix"` | Platform directory name. Must match a directory under `platforms/` in the kernel source. Examples: `"posix"`, `"esp-idf"`. |

**`[build]`**

Optional overrides for compile-time configuration. Each field maps directly to a CMake variable. If omitted, the kernel's defaults are used.

| Field | CMake variable | Default | Description |
|-------|---------------|---------|-------------|
| `cell_size` | `FROTH_CELL_SIZE_BITS` | 32 | Cell width in bits (8, 16, 32, 64) |
| `heap_size` | `FROTH_HEAP_SIZE` | 4096 | Heap size in bytes |
| `slot_table_size` | `FROTH_SLOT_TABLE_SIZE` | 128 | Maximum number of slots |
| `line_buffer_size` | `FROTH_LINE_BUFFER_SIZE` | 1024 | REPL input buffer in bytes |
| `tbuf_size` | `FROTH_TBUF_SIZE` | 1024 | Transient string ring buffer in bytes |
| `tdesc_max` | `FROTH_TDESC_MAX` | 32 | Maximum transient string descriptors |
| `ffi_max_tables` | `FROTH_FFI_MAX_TABLES` | 8 | Maximum FFI binding tables |

**`[ffi]`** (reserved, v1 optional)

Project-owned C FFI source files. These are compiled alongside the kernel and board FFI when building firmware. Not used by `froth send` (EVAL path has no C compilation).

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sources` | array of strings | `[]` | C source files, relative to `froth.toml`. Added to `target_sources` in CMake. |
| `includes` | array of strings | `[]` | Include directories, relative to `froth.toml`. Added to `target_include_directories`. |
| `defines` | table | `{}` | Compile definitions. Each key-value pair becomes `-Dkey=value`. |

V1 implementation: the CLI passes these to CMake. The schema is defined now so projects can declare FFI sources from the start; full tooling support may lag.

**`[platform.<name>]`** (reserved, v1 optional)

Platform-specific build configuration. The `<name>` matches the `[target] platform` value.

| Platform | Field | Description |
|----------|-------|-------------|
| `esp-idf` | `sdkconfig_defaults` | Path to `sdkconfig.defaults` file |
| `esp-idf` | `partition_table` | Path to custom partition table CSV |
| `esp-idf` | `idf_target` | ESP-IDF target chip (e.g., `esp32`, `esp32s3`) |
| `pico-sdk` | `board` | Pico board identifier (future) |

V1 implementation: `[platform.esp-idf] sdkconfig_defaults` is passed to `idf.py`. Other fields are reserved for forward compatibility.

**`[dependencies]`**

Named dependencies with source paths. V1 supports local paths only.

| Source type | Syntax | Resolution |
|-------------|--------|------------|
| Local file | `{ path = "lib/stepper.froth" }` | Relative to `froth.toml` directory |
| Local directory | `{ path = "lib/stepper/" }` | Looks for `lib/stepper/init.froth` as entry |
| Git (future) | `{ git = "https://...", tag = "v1.0" }` | Cloned to `~/.froth/deps/`, locked by `froth.lock` |

Dependencies are available for `\ #use` by name. `\ #use "stepper"` resolves by looking up `stepper` in `[dependencies]` and following the path.

### Location

`froth.toml` lives at the project root. The CLI finds the project by walking up from CWD looking for `froth.toml` (replacing the current `CMakeLists.txt + src/` heuristic). If no `froth.toml` is found, the CLI operates in "bare" mode (no manifest, no include resolution, commands operate directly on files or the device).

---

## Part 2: Include Resolution (`\ #use`)

### Directive syntax

```froth
\ #use "stepper"
\ #use "./helpers/math.froth"
```

The directive is a Froth line comment (`\` followed by a space). The Froth reader ignores it entirely. The CLI's include resolver scans source files for lines matching this pattern before sending them to the device.

**Named includes** (`\ #use "stepper"`): the argument does not start with `./` or `../`. Looked up in `[dependencies]` in `froth.toml`. Error if not found in the manifest.

**Relative path includes** (`\ #use "./helpers/math.froth"`): the argument starts with `./` or `../`. Resolved relative to the file containing the directive. No manifest entry needed. Useful for project-internal file splitting without declaring every file as a dependency.

### Resolution algorithm

The resolver takes an entry file path and produces a single merged source string. The algorithm is depth-first postorder with deduplication and cycle detection.

**Input:** entry file path (from `froth.toml` `entry` field or CLI argument).

**State:**
- `resolved`: set of canonical file paths already included (for dedup)
- `in_progress`: set of canonical file paths currently being processed (for cycle detection)
- `project_root`: directory containing `froth.toml` (or CWD in bare mode)

**Path canonicalization:** All file paths are canonicalized before use: `filepath.Clean` → `filepath.Abs` → `filepath.EvalSymlinks`. The `resolved` and `in_progress` sets use canonical paths as keys. This prevents diamond-dependency aliasing (same file reached via different relative paths) and symlink confusion.

**Root escape detection:** After canonicalization, any resolved path that does not have `project_root` as a prefix is rejected: `error: include "./../../etc/passwd" escapes project root`. This prevents relative path includes from reaching outside the project.

**Case sensitivity:** On case-insensitive filesystems (macOS HFS+/APFS), the resolver checks that the on-disk filename case matches the include argument exactly. `\ #use "./Helpers.froth"` when the file is `helpers.froth` is an error: `error: case mismatch: include says "Helpers.froth" but file is "helpers.froth"`. This prevents "works on macOS, breaks on Linux" bugs.

**Procedure: `resolve(file_path)`**

1. Canonicalize `file_path` (clean, abs, eval symlinks).
2. Verify the canonical path is within `project_root`. Error if not.
3. If the canonical path is in `resolved`, return empty string (already included).
4. If the canonical path is in `in_progress`, error: circular include detected. If the path equals the current file, use a specific message: `error: file includes itself: <path>`. Otherwise: `error: circular include: <path_a> → ... → <path_b> → <path_a>`.
5. Add the canonical path to `in_progress`.
6. Read the file contents.
7. Extract `\ #use "..."` directives using a **context-aware scanner** (see below). For each:
   a. Resolve the argument to a canonical file path (via manifest lookup or relative path resolution).
   b. Verify the file exists. Error if not: `dependency "stepper" not found at lib/stepper.froth`.
   c. Verify case matches on disk (case-insensitive FS only).
   d. Recursively call `resolve(resolved_path)`. Append the result to the output.
8. Strip `\ #use` lines from the current file's contents.
9. Append a file boundary marker: `\ --- file_path ---`
10. Append the current file's contents (with `\ #use` lines stripped).
11. Remove the canonical path from `in_progress`.
12. Add the canonical path to `resolved`.
13. Return the accumulated output.

**Context-aware scanner:** The resolver does not use a simple line-by-line regex to find `\ #use` directives. A `\ #use` that appears inside a paren comment `( ... )` or inside a string literal `"..."` must be ignored. The scanner tracks minimal lexical state:

- **Normal:** `\ ` at line start followed by `#use "..."` is a directive.
- **Paren comment:** between `(` and `)` (with nesting), all content is ignored.
- **String literal:** between `"` and `"` (with escape sequences), all content is ignored.

This mirrors the Froth reader's comment and string rules closely enough to avoid false triggers. The scanner does not need to understand any other Froth syntax.

**Output:** a single string containing all resolved files in dependency-first order, with file boundary markers as comments.

**Example:**

`src/main.froth`:
```froth
\ #use "stepper"
\ #use "./helpers.froth"

: main  step.init 100 step.move ;
main
```

`lib/stepper.froth`:
```froth
: step.init  "stepper ready" s.emit cr ;
: step.move  "moving" s.emit cr ;
```

`src/helpers.froth`:
```froth
: double  dup + ;
```

Resolved output:
```froth
\ --- lib/stepper.froth ---
: step.init  "stepper ready" s.emit cr ;
: step.move  "moving" s.emit cr ;
\ --- src/helpers.froth ---
: double  dup + ;
\ --- src/main.froth ---

: main  step.init 100 step.move ;
main
```

### Library discipline

**Non-entry files (libraries) must contain boot-safe top-level forms only.** Boot-safe forms are:

- **Colon definitions:** `: name ... ;` — always safe. The string literals inside are permanent (quotation-body path).
- **Tick-def definitions:** `'name value def` — safe. Pure binding. `def` promotes transient strings (ADR-043).
- **`\ #use` directives** — safe. Stripped by the resolver before evaluation.
- **Line and paren comments** — safe. Ignored by the reader.

**Unsafe forms** in libraries are:

- **Bare word execution** (e.g., `ledc.init`, `main`, `greet`) — causes side effects during loading. Order-dependent. Breaks ADR-037's boot assumptions (safe boot skips `autorun`, not the user program, so side effects in libraries run even during safe boot).
- **Top-level string literals** (e.g., `"hello" s.emit`) — transient per ADR-043, potentially stale by the time later code runs.
- **Top-level I/O or hardware calls** — order-dependent, not idempotent.

The CLI enforces this in v1 as a **warning** for `froth send` and an **error** for `froth build`/`froth flash`. The rationale: `froth send` is the interactive iteration loop where the user is experimenting and may intentionally include initialization code. `froth build`/`froth flash` produce firmware that must boot cleanly, so library safety is strict.

Libraries that intentionally need load-time side effects (e.g., initialization routines) must declare this with `\ #allow-toplevel` at the top of the file. This suppresses the warning/error for that file.

The detection heuristic: after stripping `\ #use` lines, comments, and string literals, if any non-whitespace content remains outside `: ... ;` blocks, the file has top-level executable forms. This is approximate (it won't catch `'name [ side-effect ] def` where the quotation body has effects), but it catches the common cases.

### Duplicate definition detection

When the resolver merges files, it scans the merged output for duplicate `: name` definitions across different source files. If the same name appears in `: name ... ;` form in two different files, the CLI emits a **warning** naming both files and the duplicated word.

Duplicates within the same file are allowed (Forth tradition: redefinition is intentional).

Duplicates between a library and the entry file are allowed (the entry file intentionally overrides a library definition).

Duplicates between two library files are warned (likely accidental, both trying to define a helper with the same name).

The entry file always appears last in the merged output, so its definitions naturally override library definitions. This is correct: the entry file is the user's code, libraries are support code.

### Interaction with transient strings (ADR-043)

The merged source string is evaluated as one `froth_evaluate_input` call (either via EVAL or as FROTH_USER_PROGRAM). String literals inside `: ;` definitions throughout the merged source are permanent (quotation-body path in the evaluator). String literals at top level in the entry file are transient. This is correct behavior — library definitions are permanent, the entry file's interactive/startup code is transient.

If a library file contains a top-level string literal (violating the definition-only discipline), it will be transient. The library discipline warning catches this.

---

## Part 3: CLI Architecture

### SDK embedding

The CLI binary (Go) embeds the following from the kernel repo using Go's `embed` package:

- `src/` — kernel C source
- `boards/` — board FFI source and board libs
- `platforms/` — platform C source
- `cmake/` — CMake scripts (`embed_froth.cmake`)
- `CMakeLists.txt` — kernel build system
- `src/lib/core.froth` — stdlib

On first use of `froth build`, the CLI extracts the embedded files to a versioned, immutable SDK cache:

```
$FROTH_HOME/sdk/froth-0.1.0/
  src/
  boards/
  platforms/
  cmake/
  CMakeLists.txt
```

**`FROTH_HOME` resolution:** The CLI determines the Froth home directory in this order:
1. `FROTH_HOME` environment variable, if set.
2. `~/.froth` on Unix (macOS, Linux).
3. `%APPDATA%\froth` on Windows (if supported in future).

All CLI-managed state lives under `FROTH_HOME`: SDK cache, platform toolchains (ESP-IDF, Pico SDK), daemon socket, pid file. The `FROTH_HOME` override enables CI/CD environments where `HOME` may not be writable, containerized builds, and multi-user machines.

The version string is the embedded `FROTH_VERSION`. If the directory already exists and the version matches, extraction is skipped. If the CLI is upgraded and the version changes, a new directory is created. Old versions are not automatically deleted (the user can clean up `$FROTH_HOME/sdk/` manually).

The `--local` flag bypasses the SDK cache and uses the current repo's source. The CLI detects it's inside a kernel repo by looking for `src/froth_vm.h` at the project root. This is the kernel developer workflow.

**Windows note:** The daemon (Unix domain socket) and raw terminal handling are Unix-specific. Windows support for the daemon and `froth connect` is out of thesis scope. `froth build` and `froth flash` may work on Windows via WSL; this is not tested or guaranteed.

### Build artifacts

Project builds produce artifacts in `.froth-build/` at the project root:

```
my-project/
  .froth-build/
    resolved.froth    # merged source after include resolution
    firmware/         # CMake build directory
      Froth           # POSIX binary (if target is posix)
```

`.froth-build/` should be in `.gitignore`.

### Commands

**`froth new <name> [--target <board>]`**

Scaffolds a new project directory:

```
<name>/
  froth.toml
  src/
    main.froth
  lib/
    .gitkeep
  .gitignore
```

If `--target` is omitted, defaults to `posix`. Available targets are discovered from the embedded SDK's `boards/` directory. `froth new --list-targets` lists them.

Generated `src/main.froth`:
```froth
\ <name>

: autorun
  "Hello from Froth!" s.emit cr
;
```

The entry point is `autorun`, not a bare call. On firmware boot, the kernel's existing `[ 'autorun call ] catch drop` pattern invokes it (ADR-037). On `froth send`, the CLI appends `[ 'autorun call ] catch drop` after the merged source to match boot behavior. This means the same source file works identically whether flashed or sent.

Generated `.gitignore`:
```
.froth-build/
froth_a.snap
froth_b.snap
```

**`froth build`**

1. Find `froth.toml` (walk up from CWD).
2. Read manifest. Validate target board and platform exist in the SDK.
3. Check platform toolchain is installed (ESP-IDF, Pico SDK). If missing, prompt: `ESP-IDF v5.4 not found. Install now? [Y/n]`. If yes, run the setup script. If no, error with instructions.
4. Resolve includes starting from the `entry` file. Write merged source to `.froth-build/resolved.froth`.
5. Run CMake with the SDK source directory, the board/platform from the manifest, build overrides from `[build]`, and `FROTH_USER_PROGRAM=.froth-build/resolved.froth`.
6. Build artifacts go to `.froth-build/firmware/`.

```
$ froth build
Resolving src/main.froth (2 dependencies)... done
Building for esp32-devkit-v1...
[████████████████████] 100%
Firmware ready: .froth-build/firmware/Froth.bin
```

Flags:
- `--local`: use local kernel source instead of embedded SDK
- `--verbose`: show CMake output
- `--clean`: delete `.froth-build/` before building

**`froth flash`**

1. Run `froth build` if `.froth-build/firmware/` is stale or missing.
2. Detect serial port (auto-discover or `--port`).
3. Flash using the platform's flash tool (`idf.py flash` for ESP-IDF).
4. Reset the device.

```
$ froth flash
Resolving src/main.froth (2 dependencies)... done
Building for esp32-devkit-v1... done
Detected: /dev/cu.usbserial-0001 (ESP32)
Flashing... done
Device ready.
```

**`froth send [file]`**

1. If `file` is specified, use it as the entry file. Otherwise, read `entry` from `froth.toml`.
2. Resolve includes. Write merged source to `.froth-build/resolved.froth`.
3. Append `[ 'autorun call ] catch drop` to the merged source (matches boot behavior per ADR-037).
4. Connect to the device (daemon or direct serial).
5. Reset the device.
6. Send the merged source via EVAL (chunked).
7. Print the result (stack state or error).

```
$ froth send
Resolving src/main.froth (2 dependencies)... done
Resetting device... done
Sending (1247 bytes, 3 chunks)... done
[ok]
```

This is the fast iteration loop. No rebuild, no reflash. Edit source, `froth send`, see results.

Flags:
- `--no-reset`: send without resetting first (append to current device state)
- `--file <path>`: override entry file (same as positional argument)

**`froth connect [--local]`**

Opens an interactive session with a Froth runtime.

`froth connect --local`:
1. Build the POSIX target if not already built (using the SDK or local source). If CMake is not installed, error with remediation: `error: cmake not found. Install with: brew install cmake`.
2. Execute the POSIX binary directly, with stdin/stdout attached to the user's terminal.
3. No daemon needed. Instant REPL. Full `key` support, Ctrl-C interrupts, Ctrl-D exits.

`froth connect` (serial device, v1):
1. Start the daemon if not running.
2. Subscribe to the daemon's console event stream. Device output is printed to the user's terminal.
3. Accept single-line input from the user. Each line is sent via the daemon's `eval` RPC.
4. Ctrl-C sends interrupt via the daemon.
5. Ctrl-D or `\ quit` detaches.

This is **not** a raw terminal attach. `key` does not work interactively (device-side `key` blocks on the link mux, not on this console stream). Long-running output streams via console events. This is an RPC-backed interactive session, sufficient for definition and testing but not for programs that use `key` interactively.

**Future (post-thesis, requires ADR-035 Phase 2 PTY passthrough):** `froth connect` for serial devices becomes a raw terminal attach. Keystrokes are forwarded byte-by-byte to the device. `key` works. `~.` detaches. This requires the daemon to support PTY/raw passthrough alongside its JSON-RPC surface, which is not built.

This is the "I just want to type Froth" command. No project required. No manifest required.

**`froth doctor`**

Checks everything relevant to the current context:

```
$ froth doctor
Froth CLI v0.2.0                       ✓
Froth kernel v0.1.0                    ✓ (~/.froth/sdk/froth-0.1.0/)
CMake 3.23+                            ✓ (/opt/homebrew/bin/cmake)
Project: my-project                    ✓ (froth.toml found)
  Target: esp32-devkit-v1 (esp-idf)
  Entry: src/main.froth                ✓
  Dependencies:
    stepper (lib/stepper.froth)        ✓
    servo (lib/servo.froth)            ✗ file not found
      → Create lib/servo.froth or remove from [dependencies]
ESP-IDF v5.4                           ✗ not installed
  → Run: froth flash (will prompt to install)
Serial ports:
  /dev/cu.usbserial-0001               Silicon Labs CP210x
Device:                                ✗ not connected
  → Connect an ESP32 and retry
VS Code extension:                     not installed
  → Install from marketplace: ext install froth.froth
```

When run outside a project (no `froth.toml`), it skips project-specific checks and shows only system-level status.

Every failure line includes one exact command or action to fix it.

### `froth new` and the two onboarding lanes

Not every user starts with `froth new`. The first-use experience has two lanes:

**Lane 1: "Show me Froth right now" (no hardware)**
```
brew install froth
froth connect --local
froth> 1 2 + .
3
```

Three commands, zero project setup. The CLI builds the POSIX target from the embedded SDK, runs it, and attaches. The user is typing Froth in under a minute.

**Lane 2: "I have a pre-flashed board" (workshop)**
```
brew install froth
froth connect
froth> 2 1 gpio.mode
froth> 2 1 gpio.write
```

Two commands. The CLI discovers the serial port, starts the daemon, attaches. The board was pre-flashed by the workshop organizer.

**Lane 3: "I want to build a project" (real use)**
```
brew install froth
froth new my-project --target esp32
cd my-project
froth flash
froth send
```

This is the `froth.toml` workflow. This is step three, not step one.

---

## Part 4: VS Code Extension Integration

The extension's "Send File" command currently sends the active file's contents via the daemon's `eval` RPC. With the project system, it needs to resolve includes first.

Two options:

**Option A: Extension calls `froth send` CLI command.** The extension invokes the CLI in a terminal, which handles resolution, reset, and EVAL. The extension doesn't need its own resolver.

**Option B: Extension implements resolution in TypeScript.** The extension reads `froth.toml`, resolves `\ #use` directives, merges, and sends the merged string via the daemon's `eval` RPC.

**Decision: Option A for v1.** The CLI is the single source of truth for resolution. The extension delegates to it. This avoids duplicating the resolver in TypeScript and ensures the CLI and extension always agree on how includes are resolved. The extension's "Send File" button runs `froth send <active-file>` in an integrated terminal, showing the output in the console panel.

Future versions may implement Option B for tighter integration (inline error display, go-to-definition across includes, etc.), but that requires a language server, which is post-thesis.

---

## Part 5: Interaction with Existing Systems

### Board lib (`boards/<board>/lib/board.froth`)

The board lib is embedded at firmware build time and evaluated at boot (after stdlib, before `boot_complete`). It is not part of the project's include tree. It provides the board's convenience words (e.g., `ledc.setup`, `ledc.duty`). The user's project code depends on these words being available at runtime, but doesn't `\ #use` them — they're always present on the target board.

### User programs (`FROTH_USER_PROGRAM`)

`froth build` passes the merged resolved source as `FROTH_USER_PROGRAM` to CMake. This is the connection between the project system and the existing user program mechanism. The merge happens at build time, not at runtime.

### `froth send` and EVAL

`froth send` passes the merged resolved source to the daemon's `eval` RPC (after a `reset`). The device evaluates it as one flat string. The device doesn't know about files, includes, or projects.

### Snapshots

A user who sends a program via `froth send`, then calls `save`, persists the evaluated state (including all definitions from all included files). On reboot, `restore` loads the snapshot, and the user program is not re-evaluated (snapshot takes priority, per ADR-037). This is correct: the snapshot is a complete image.

If the user reflashes with a changed program (`froth flash` after editing source), the snapshot's source hash won't match, and `restore` will skip the stale snapshot. The new user program runs on cold boot.

### Transient strings (ADR-043)

The resolved source is one big string passed to `froth_evaluate_input`. The evaluator's transient/permanent decision is based on quotation depth, not file boundaries. This means:

- `: name "hello" s.emit ;` anywhere in the merged source → `"hello"` is permanent (inside a quotation body). Correct.
- `"hello" s.emit` at top level in the entry file → `"hello"` is transient. Correct.
- `"hello" s.emit` at top level in a library file → `"hello"` is transient. The library discipline warning catches this.

---

## Consequences

- Projects are defined by `froth.toml`, not by repo structure. The CLI finds the project root by looking for `froth.toml`.
- Include resolution is host-side only. The device evaluator is unchanged. No new language syntax.
- The `\ #use` directive is invisible to the Froth reader (it's a comment). No reader changes. The `\ #` prefix is reserved for future host pragmas.
- The CLI embeds the kernel source, making `brew install froth` the only prerequisite for newcomers. Platform toolchains are installed on demand. `FROTH_HOME` enables CI/CD and non-standard environments.
- The embedded SDK is versioned and immutable. Version skew between CLI and kernel source is prevented.
- Library files must be boot-safe (definitions only). Violations are warned on `froth send`, errored on `froth build`/`froth flash`. `\ #allow-toplevel` opts out.
- Duplicate definitions across library files are warned. The entry file can redefine anything.
- The VS Code extension delegates to the CLI for resolution (v1). No TypeScript resolver needed.
- `froth connect --local` provides an instant Froth experience with zero setup.
- `froth connect` for serial devices is RPC-backed in v1 (console monitor + eval), not raw attach. Raw PTY attach requires ADR-035 Phase 2.
- Three onboarding lanes (instant REPL, pre-flashed board, new project) cover three user profiles without forcing everyone through the same path.
- All error messages include a specific remediation command or action. No error should leave the user wondering what to do next.
- The scaffold uses `autorun` as the entry point, matching ADR-037's boot convention. `froth send` appends the autorun invocation to match boot behavior.
- The resolver canonicalizes all paths (symlinks, case), rejects escapes outside the project root, and uses a context-aware scanner (not raw regex) to avoid false-triggering on `\ #use` inside comments or strings.
- The `froth.toml` schema reserves `[ffi]`, `[platform.<name>]`, and dependency version fields for forward compatibility without v1 implementation.

## Deferred

- **Git/remote dependencies.** `{ git = "..." }` in `[dependencies]` and a `froth.lock` lockfile for reproducibility. Not needed until the ecosystem has published libraries. The dependency schema in v1 is shaped to grow without breaking (add `git`, `tag`, `version` fields alongside `path`).
- **Module scoping.** Everything is global (flat slot namespace). Naming prefixes are the convention. A future module system would add namespaces, but that's a language-level change far beyond the project system.
- **Language server (LSP).** Go-to-definition, hover, error squiggles across includes. Requires the resolver to run incrementally and report source locations. Post-thesis.
- **`froth publish`** and a package registry. Way post-thesis.
- **`froth.lock`** for pinning dependency versions once remote dependencies exist.
- **Raw `froth connect` over serial (PTY passthrough).** Requires ADR-035 Phase 2 daemon work. V1 provides RPC-backed console session, which is sufficient for definitions and testing but not for interactive `key`-based programs.
- **Named target profiles.** `[target.release]`, `[target.debug]` with different build settings. V1 has one `[target]` section. Multiple profiles add complexity for no immediate benefit.
- **Per-dependency version constraints.** `stepper = { path = "...", version = ">=1.0" }`. Not meaningful until remote dependencies and a version solver exist.
- **Windows daemon and `froth connect`.** Unix-specific (Unix domain socket, raw terminal). Out of thesis scope.

## References

- ADR-037: Host-centric deployment (user programs, reset, editor workflow)
- ADR-043: Transient string buffer (top-level vs quotation-body string allocation)
- ADR-035: Daemon architecture (serial connection management, RPC)
- ADR-039: Host tooling UX and daemon lifecycle
- ADR-042: Extension UX and local target
- ADR-041: Strict bare identifiers (no implicit slot creation)
- UX architecture review (Mar 20): SDK embedding, form graph, library discipline, connect semantics
- Go `embed` package: https://pkg.go.dev/embed
- MicroPython workflow: instant REPL, low ceremony (influence on onboarding lanes)
- ESP-IDF workflow: explicit, reproducible, hostile first hour (influence on CLI absorbing complexity)
- Arduino workflow: board + library + flash ergonomics (influence on `froth new` + `froth flash`)
