# Froth Tooling and Link Architecture Proposal

**Date:** 2026-03-14  
**Status:** Proposal  
**Audience:** Froth maintainers, future implementers, reviewers, and LLMs used for implementation support  
**Purpose:** Define a durable technical direction for Froth's host tooling and device-side link workflow without changing current code.  
**Related docs:** [Froth_Interactive_Development_v0_5.md](../spec/Froth_Interactive_Development_v0_5.md), [Froth_Language_Spec_v1_1.md](../spec/Froth_Language_Spec_v1_1.md), [Froth_Snapshot_Overlay_Spec_v0_5.md](../spec/Froth_Snapshot_Overlay_Spec_v0_5.md), [ADR-028](../adr/028-board-platform-architecture.md), [ADR-029](../adr/029-build-targets-and-toolchain-management.md), [ADR-030](../adr/030-platform-check-interrupt-and-safe-boot.md), [ADR-032](../adr/032-mark-release-heap-watermark.md)

## 1. Why This Document Exists

Froth has reached the point where the kernel is strong enough that ecosystem work is no longer hypothetical. The current docs already commit the project to several long-lived ideas:

- the device is the computer
- host tooling is optional but desirable
- Link Mode exists as a concept
- board metadata is intended to drive tooling
- build targets and SDK discovery should eventually feed automation

Those ideas are good, but they do not yet form a coherent implementation plan.

This document supplies that missing plan in two parts:

1. **Froth-side decisions** required to support modern interactive tooling without breaking the "serial terminal alone is enough" principle.
2. **Host tooling architecture** for a CLI, local daemon, and VS Code integration that can scale across multiple targets over a long project lifetime.

This is a proposal document, not an accepted ADR. It deliberately goes deeper than an ADR because the next implementation phase will span specs, host tooling, protocol work, and UI.

## 2. Project Constraints

The solution proposed here is guided by the following constraints:

- **Device-first is non-negotiable.** A serial terminal alone must remain sufficient for normal use.
- **Froth is a long-running project.** Short-term convenience must not lock the project into brittle transport or tooling choices.
- **Host tooling must be optional.** This is already normative in the interactive spec.
- **The transport must survive real serial links.** Noise, reconnects, partial frames, and monitor quirks are normal.
- **The system must support multiple targets.** ESP-IDF is first, but the architecture must survive RP2040 and future ARM targets.
- **Tooling must not depend on scraping human text.** Structured data needs a structured channel.
- **Embedded implementation cost matters.** Device-side parsing and serialization must stay small and explicit.
- **Open source sustainability matters.** The host stack should maximize code reuse, minimize duplication, and keep contributor friction reasonable.

## 3. Current State and Known Tensions

### 3.1 What already exists

The current repo already has the prerequisites for serious tooling:

- direct-mode REPL and recovery
- interrupt handling
- safe boot
- snapshot persistence model
- board metadata via `board.json`
- target SDK conventions under `~/.froth/sdk/`
- quotation introspection (`q.len`, `q@`)
- heap watermarking (`mark`, `release`)

### 3.2 What is not coherent yet

There are several design tensions that must be resolved before tooling work starts.

#### Tension A: current Link Mode framing conflicts with current interrupt semantics

The interactive spec currently describes Link Mode using `STX ... ETX` framing. ADR-030 standardizes `ETX` (`0x03`, Ctrl-C) as the interrupt and safe-boot byte. Those two choices are in direct conflict for any serious link implementation.

This is not a cosmetic issue. It affects:

- runtime interrupt behavior
- safe boot
- host framing
- future web tooling
- implementation complexity on every platform

#### Tension B: `mark/release` is not a code transaction mechanism

The language spec discusses region marks abstractly, but ADR-032 and the current implementation define a **single implicit heap watermark**. That is useful for scratch heap recovery, but it is not a safe or composable way to "undo a batch of definitions."

There is also a documentation mismatch to resolve: the language spec still describes `mark`/`release` in a more abstract region style, while ADR-032 and the current implementation use a single VM-held mark rather than first-class mark values. Tooling should not depend on the abstract form until that mismatch is cleaned up.

Tooling must not assume that `mark/release` can roll back:

- slot bindings
- overlay ownership
- changed definitions
- inspector-visible program state

If the editor wants reversible sync, that requires a separate checkpoint model.

#### Tension C: machine-readable tooling is underspecified

The current specs reserve `#` lines for tooling and describe `#ACK/#NAK`, but there is no complete model for:

- structured stack data
- slot inspection
- word origin/base-vs-overlay reporting
- device capability discovery
- build/profile metadata exchange

#### Tension D: host architecture is still implied, not designed

ADR-028 and ADR-029 imply tooling, but there is not yet a concrete decision on:

- whether the CLI and daemon are separate
- whether editor logic lives in the extension or a backend
- how libraries are described
- how projects declare profile/layer choices

## 4. Proposal Summary

This document proposes the following overall direction:

1. **Replace the current `STX/ETX` Link Mode proposal** with a versioned, binary-framed transport that does not collide with Ctrl-C.
2. **Keep Direct Mode text-first** and leave manual serial workflows untouched.
3. **Define a structured device-side tooling surface** for evaluation, inspection, and capability discovery.
4. **Do not use `mark/release` as editor rollback.** Treat it as heap-only scratch management.
5. **Build a host core first**: a reusable protocol/session/build layer used by CLI, daemon, VS Code, and later web UI.
6. **Use a binary-first host backend**: Go for the CLI and daemon, TypeScript for VS Code and web clients, and language-neutral protocol/manifests as the shared contract.
7. **Use Git-based libraries first**, not a centralized package registry.
8. **Add a project manifest** that declares board, platform, profile/layers, and dependencies.

The rest of this document expands those decisions in detail.

---

# Part I. Froth-Side Proposal

## 5. Design Goals for the Device Side

The device-side link/tooling design must satisfy all of the following:

- preserve the existing Direct Mode REPL as the default human interface
- preserve raw `Ctrl-C` interrupt behavior outside link frames
- support multi-line expression send without line-heuristic hacks
- survive malformed host messages without wedging the REPL
- allow tools to get structured results without scraping human output
- keep the C implementation small enough for embedded targets
- keep protocol parsing independent from language evaluation

## 6. Proposed Device-Side Architecture

The device side should be treated as four layers:

1. **Console transport byte stream**
2. **Console multiplexer**
3. **Froth Link Transport**
4. **Froth tooling dispatcher**

### 6.1 Console transport byte stream

This is the current UART/stdio/platform layer. It should remain platform-specific and dumb. It knows how to read and write bytes. It does not know Froth syntax or link messages.

### 6.2 Console multiplexer

This is a new logical layer above `platform_key`/`platform_emit`. Its job is to split the incoming stream into:

- ordinary Direct Mode bytes
- framed Link Transport messages
- raw interrupt bytes outside frames

This layer should not live inside the REPL and should not be buried in platform code. It is protocol logic, not board logic.

### 6.3 Froth Link Transport

This is the structured framing layer. It is responsible for:

- frame delimiting
- version detection
- CRC validation
- request IDs
- request/response matching
- rejecting malformed frames safely

It is not responsible for parsing Froth expressions.

### 6.4 Froth tooling dispatcher

This is the request handler layer. It is responsible for:

- evaluating a source payload
- returning structured success/error data
- exposing inspection requests
- advertising device capabilities

It is not the REPL. It should call the same underlying evaluator and inspector functions the REPL uses.

## 7. Proposed Replacement for Current Link Mode

### 7.1 Decision

Replace the current `STX ... ETX` Link Mode framing with a framed binary transport:

- **frame format:** `0x00 <COBS-encoded frame bytes> 0x00`
- **payload encoding:** binary header + raw payload
- **request model:** stop-and-wait in phase 1
- **transport name:** `FROTH-LINK/1`

### 7.2 Why this transport

This choice is recommended because it:

- avoids collision with `Ctrl-C` (`0x03`)
- avoids collision with ordinary text REPL traffic
- is easy to implement in C
- is easy to implement in Go and TypeScript
- keeps the device free of a general-purpose JSON parser
- allows future transport reuse over serial, TCP, WebSocket, or test harnesses

### 7.3 Why not keep `STX/ETX`

Do not try to save the existing `STX/ETX` design with ad hoc escaping. It is the wrong long-term foundation because:

- `ETX` is already the interrupt byte
- tools would need awkward escaping rules
- human terminal mixing gets harder to reason about
- the spec becomes harder to teach and implement correctly

### 7.4 Why not line-delimited text frames

Pure line-delimited text is attractive, but it is the wrong primitive for a decade-scale transport because:

- multi-line expressions need second-order framing
- arbitrary tool payloads become awkward
- partial or noisy line states are harder to recover from cleanly

Text should remain the human layer. Framing should be binary and explicit.

## 8. Froth Link Transport v1

### 8.1 Framing

Each transport frame on the wire is:

```text
0x00 <cobs-encoded bytes> 0x00
```

Interpretation rules:

- A `0x00` byte outside an active frame starts frame capture.
- Bytes are accumulated until the next `0x00`.
- The captured byte sequence is COBS-decoded.
- If decode fails, the frame is dropped and a transport error may be emitted.
- Bytes outside framed regions are treated as normal console input.
- Raw `0x03` outside frames remains an interrupt request.
- Bytes inside frames are data, not console control.

This is the key property the current `STX/ETX` design does not have.

### 8.2 Decoded frame layout

All multi-byte integers are little-endian.

```text
offset  size  field
0       2     magic = "FL"
2       1     protocol_version = 1
3       1     message_type
4       1     flags
5       1     reserved0 = 0
6       2     reserved1 = 0
8       4     request_id
12      4     payload_length
16      4     payload_crc32
20      N     payload bytes
```

Validation rules:

- `magic` must equal `"FL"`
- `protocol_version` must equal `1`
- `payload_length` must not exceed `FROTH_LINK_MAX_PAYLOAD`
- `payload_crc32` must match the payload
- reserved fields must be ignored on read and written as zero

### 8.3 Message types

Phase 1 should keep the set intentionally small.

```text
0x01 HELLO_REQ
0x02 HELLO_RES
0x03 CLOSE_REQ
0x04 CLOSE_RES
0x10 EVAL_REQ
0x11 EVAL_RES
0x12 INSPECT_REQ
0x13 INSPECT_RES
0x14 LIST_WORDS_REQ
0x15 LIST_WORDS_RES
0x16 INFO_REQ
0x17 INFO_RES
0x7E EVENT
0x7F ERROR
```

Phase 1 deliberately omits:

- streaming stdout multiplexing beyond normal console text
- concurrent requests
- binary blob transfer
- snapshot upload/download
- debugger single-stepping

Those can come later without changing the framing layer.

### 8.4 Request model

Phase 1 uses **stop-and-wait**:

- host sends one request
- device processes it to completion
- device returns exactly one response frame with the same `request_id`
- host does not send the next request until the current one resolves or times out

This keeps the device implementation small and predictable.

### 8.5 Payload formats

The transport header is binary. Payloads are message-specific.

#### `HELLO_REQ`

Payload: empty

#### `HELLO_RES`

Payload: UTF-8 JSON object with:

- `protocol`: `"FROTH-LINK/1"`
- `froth_version`
- `board`
- `platform`
- `capabilities`: array of strings
- `max_payload`

The device only needs JSON serialization, not JSON parsing.

#### `EVAL_REQ`

Payload: raw UTF-8 Froth source text

Flags:

- bit 0: `want_stack`
- bit 1: `want_output_capture`
- bit 2: `quiet_success`

Phase 1 requirement: the device must still evaluate with the same safety rules as Direct Mode:

- implicit top-level `catch`
- stack restoration on error
- prompt remains alive

#### `EVAL_RES`

Payload: UTF-8 JSON object with fields:

- `ok`: boolean
- `error_code`: integer or `null`
- `error_name`: string or `null`
- `fault_word`: string or `null`
- `stack`: string or `null`
- `output`: string or `null`

`stack` and `output` are textual in phase 1. Later versions may add a structured stack encoding.

#### `INSPECT_REQ`

Payload: UTF-8 slot name

#### `INSPECT_RES`

Payload: UTF-8 JSON object with:

- `name`
- `kind`: `"primitive" | "quotation" | "value" | "undefined"`
- `origin`: `"base" | "overlay"`
- `stack_effect`: string or `null`
- `display`: human-readable one-line summary

This request exists specifically so tools do not need to scrape `see`.

#### `LIST_WORDS_REQ`

Payload: empty

#### `LIST_WORDS_RES`

Payload: UTF-8 JSON array of word summaries. Phase 1 may page or cap this for small targets.

#### `INFO_REQ`

Payload: empty

#### `INFO_RES`

Payload: UTF-8 JSON object with:

- `froth_version`
- `heap_used`
- `heap_free`
- `slot_count`
- `overlay_slot_count`
- `snapshots_enabled`
- `snapshot_present`
- `safe_boot_supported`

#### `ERROR`

Payload: UTF-8 JSON object with transport-layer details:

- `category`: `"decode" | "crc" | "unsupported" | "busy" | "internal"`
- `detail`

### 8.6 Capability strings

The `capabilities` array in `HELLO_RES` should be plain strings so the host can feature-detect instead of hardcoding version assumptions.

Recommended initial capability strings:

- `eval`
- `inspect`
- `list-words`
- `info`
- `interrupt`
- `safe-boot`
- `snapshots`

Future capabilities may include:

- `structured-stack`
- `slot-info`
- `checkpoint`
- `trace`
- `stream-events`

## 9. Direct Mode and Link Mode Interaction

The core rule should be:

- **Direct Mode remains the default human interface.**
- **Link frames are a side channel on the same byte stream, not a replacement REPL.**

Operationally:

- ordinary bytes still feed the REPL
- framed messages are intercepted and handled separately
- ordinary human output still prints as text
- tool responses use framed messages

This means the device stays usable even if the host daemon crashes or the editor is closed.

## 10. Tooling Surface Required from Froth

The transport alone is not enough. The language/runtime must expose a small, stable tooling surface.

### 10.1 Required phase-1 capabilities

These should exist either as direct internal calls or as well-defined helper functions used by the link dispatcher:

- evaluate source text with top-level `catch`
- capture error code and faulting word
- format current stack in the standard stack display form
- inspect a slot by name without scraping human REPL output
- report origin (`base` or `overlay`) for a slot
- report a slot's stack effect if available
- report heap and snapshot info

### 10.2 Strongly recommended Froth/runtime additions

These are not all required for phase 1, but they are the right direction:

- `slot-info ( slot -- ... )` or equivalent internal API
- `free ( -- n )`
- `used ( -- n )`
- `snapshot? ( -- flag )`
- explicit machine-readable inspector functions in C, shared by REPL and link

### 10.3 `arity!` and metadata

The language spec already points toward stack-effect metadata. Tooling should depend on that direction, but phase 1 should not block on full metadata completeness.

Recommended policy:

- compiler/tooling authority should be numeric slot arity metadata, not parsed
  prose from `stack_effect` strings
- kernel primitives should expose explicit numeric arities through
  `froth_ffi_entry_t`, copied onto slots at registration time
- plain `:` definitions should ingest `( ... -- ... )` headers early so user
  words can record arity metadata even before full named lowering is enabled
- explicit `arity!` remains the fallback path for user words that do not carry a
  semantic stack-effect header
- board FFI may remain "unknown arity" initially; tools should degrade
  gracefully rather than scrape or infer

## 11. Editor Sync Semantics

### 11.1 Principle

The editor should sync **definitions**, not bytes.

That means the device-side contract should be:

- the host sends source text
- the device evaluates it atomically at the top-level form boundary
- the tool records which workspace form was sent and what slot name it defines

The device should not need to understand files, line numbers, or projects.

### 11.2 What the host may assume

The host may assume:

- top-level definitions are atomic
- errors restore stack depths
- the prompt survives
- safe boot exists for bad restore/autorun states

### 11.3 What the host must not assume

The host must not assume:

- that `mark/release` reverts definitions
- that re-sending a file can be "undone" automatically
- that the device can reconstruct source file structure

## 12. `mark/release` Policy for Tooling

### 12.1 Recommended policy

Tooling should use `mark/release` only for:

- scratch quotation generation
- temporary object construction
- exploratory helper data

Tooling must not use `mark/release` as:

- an editor undo stack
- a file-sync rollback mechanism
- a definition checkpoint system

### 12.2 Why

ADR-032 is explicit: the current model is a single heap watermark. It is not composable and does not protect against dangling references. That is the correct workshop primitive, but it is not sufficient for durable editor semantics.

### 12.3 Future checkpoint design

If reversible sync becomes a priority, it should be specified separately as a new profile or ADR, for example:

- `checkpoint`
- `revert`
- `commit`

Those operations would need to track at least:

- heap watermark
- overlay slot watermark or binding delta
- possibly metadata changes

That is explicitly out of scope for phase 1.

## 13. Direct-Mode Tooling Output

The current language spec already reserves lines starting with `#` for tooling. That remains useful, but only as a low-tech compatibility layer.

Recommended role for `#` lines going forward:

- useful for dumb terminal integrations
- useful for smoke tests
- not the primary transport for modern editor tooling

Suggested outputs to keep or add in Direct Mode:

- `#STACK ...`
- `#ERR ...`
- `#INFO ...`

But serious tooling should prefer framed link messages.

## 14. Device-Side Module Plan

When this is implemented, the C code should be split along these lines:

- `src/froth_link.h` / `src/froth_link.c`
  - frame encoding/decoding
  - CRC checks
  - message dispatch
- `src/froth_console_mux.h` / `src/froth_console_mux.c`
  - distinguish direct bytes, interrupt bytes, and framed traffic
- `src/froth_tooling.h` / `src/froth_tooling.c`
  - shared inspection and structured response helpers
- existing evaluator / REPL / snapshot modules remain focused on their own jobs

Do not bury link framing in:

- `froth_repl.c`
- platform implementations
- ad hoc editor-specific code paths

That will not age well.

## 15. Acceptance Criteria for Device-Side Phase 1

A phase-1 implementation should be considered complete when all of the following are true:

- a human can still use Froth from a plain terminal with no host tooling
- raw `Ctrl-C` outside frames interrupts runaway code
- malformed link frames do not wedge the REPL or crash the device
- the host can send a multi-line definition as a single framed request
- the host gets structured success/error data back without scraping human output
- the host can inspect a word by name and get its origin and display summary
- reconnecting a host tool does not require resetting the board

## 16. ADRs and Spec Work Needed for Part I

Before or alongside implementation, the following docs should be created or updated:

- ADR: Link transport replacement for current `STX/ETX` proposal
- spec update: replace current Link Mode section in `Froth_Interactive_Development_v0_5.md`
- ADR or spec note: machine-readable tooling surface
- possible ADR: clarify `mark/release` vs future checkpoints
- spec cleanup: reconcile any remaining mismatch between abstract `FROTH-Region` language and ADR-032's single implicit mark implementation

---

# Part II. Host Tooling Architecture Proposal

## 17. Host Tooling Goals

The host side should provide three things simultaneously:

- **fast onboarding**
- **serious interactive development**
- **durable multi-target project management**

It should do that without making the editor or CLI the only way to use Froth.

## 18. Recommended Host Stack

### 18.1 Language choice

The recommended host architecture is:

- **Go** for the authoritative host backend:
  - `froth` CLI
  - `frothd` daemon
  - shared build/session/protocol/project logic
- **TypeScript** for client surfaces:
  - VS Code extension
  - optional local web UI
- **Language-neutral specs and schemas** for shared contracts:
  - link transport
  - daemon RPC
  - project manifest
  - package manifest

This is recommended because it optimizes for long-term robustness rather than maximum early reuse in one language.

### 18.2 Why this split

This split is recommended because it gives Froth:

- a **portable native binary** for the commands users will rely on most
- a backend that is well-suited to:
  - serial sessions
  - subprocess orchestration
  - file watching
  - reconnect loops
  - long-running daemon behavior
- thin editor clients rather than logic-heavy editor extensions
- language-neutral contracts that can survive tool rewrites later

For a project expected to last a decade or more, this is a better trade than maximizing same-language reuse across every layer.

### 18.3 Why not all TypeScript/Node

An all-TypeScript host stack is attractive for early implementation because it maximizes code reuse across CLI, daemon, VS Code, and web. It is not recommended as the long-term core because it imposes more risk in:

- runtime distribution
- Node/toolchain dependency management
- long-term daemon operational stability
- package churn and supply-chain noise
- editor logic becoming too smart because the backend is "just another JS app"

TypeScript is still the right choice for VS Code and any browser UI. It is not the best authority layer.

### 18.4 Why not split Python + TypeScript

A Python CLI/daemon plus TypeScript extension is workable, but it creates the wrong kind of duplication:

- protocol definitions in two ecosystems
- parser and source-analysis logic in two ecosystems
- manifest validation in two ecosystems
- more operational complexity than Go without the same binary-distribution upside

If the project is going to split languages at all, Go is the stronger backend language.

### 18.5 Why not Rust first

Rust is technically attractive, but it raises contributor cost and slows initial delivery. It is better reserved for future hotspots if the host stack proves performance-sensitive.

## 19. Host Components

The recommended host architecture has four primary executables, one authoritative backend core, and thin clients layered over stable contracts.

### 19.1 Shared packages

These should live in a single host workspace, but they should be split by language and responsibility rather than forced into one implementation language.

Recommended Go modules:

- `froth-protocol`
  - frame types
  - COBS encoder/decoder
  - request/response codecs
- `froth-source`
  - tokenizer/completeness checker
  - top-level form splitter
  - definition extractor
  - minimal formatter helpers
- `froth-project`
  - project manifest schema
  - dependency schema
  - profile/layer resolution
  - board catalog loading
- `froth-build`
  - SDK discovery
  - target build/flash commands
  - toolchain doctor checks
- `froth-session`
  - serial transport
  - reconnect logic
  - link handshake
  - eval/inspect/info RPC wrappers
- `froth-daemon`
  - daemon service composition
  - session registry
  - event fanout
  - client RPC handlers
- `froth-lsp-core`
  - diagnostics
  - symbol extraction
  - completion/hover primitives

Recommended TypeScript client packages:

- `froth-daemon-client`
  - typed client for daemon RPC
- `froth-vscode`
  - extension entrypoints, commands, panels
- `froth-web`
  - optional browser UI over daemon RPC

Shared contracts should be specified in repo documents first and optionally mirrored into generated schema artifacts later. The authoritative source of truth should not live only in generated code.

### 19.2 `froth` CLI

The CLI is the lowest-friction host entry point and should exist before any GUI.

Implementation language: **Go**

Recommended commands:

- `froth doctor`
- `froth new`
- `froth boards`
- `froth profiles`
- `froth build`
- `froth flash`
- `froth monitor`
- `froth repl`
- `froth send`
- `froth sync`
- `froth info`
- `froth libs add`
- `froth libs update`

The CLI should be able to run without the daemon for simple tasks, but it should be able to reuse the daemon when present.

### 19.3 `frothd` local daemon

This is the key host service. It should own:

- serial port session lifetime
- board connection state
- build/flash orchestration
- device capability cache
- workspace-to-device sync state
- structured event stream for UI clients

It should expose a localhost API, preferably JSON-RPC over:

- Unix domain socket on Unix
- named pipe on Windows
- stdio for child-process usage where appropriate

It should be the canonical runtime used by:

- VS Code
- future web UI
- advanced CLI commands

Implementation language: **Go**

### 19.4 `froth-ls` language server

Long-term, Froth should also have a real language server.

It should handle:

- diagnostics
- completions
- hover
- go-to-definition
- outline / symbol list
- form boundary detection for editor commands

Recommended implementation language: **Go**, sharing analysis code with the backend.

This does not need to exist on day one. The acceptable early path is:

- phase 1: minimal source analysis in the Go backend core
- phase 2: expose editor-facing language features from `frothd`
- phase 3: split `froth-ls` into a dedicated process if the feature set warrants it

### 19.5 VS Code extension

The VS Code extension should be intentionally thin. It should provide:

- commands and UI
- editor integration
- panel rendering
- bridge to `frothd` and `froth-ls`

Implementation language: **TypeScript**

It should not own:

- protocol definitions
- serial logic
- build system knowledge
- parsing rules

Those belong in shared packages.

### 19.6 Local web UI

The local web UI is optional and should come after the daemon and VS Code integration.

It should:

- talk only to `frothd`
- reuse the same daemon RPC contracts as VS Code
- avoid direct serial or build-system ownership

Implementation language: **TypeScript**

## 20. Project Manifest

The host architecture needs a project manifest. A proposed filename is:

- `froth.toml`

Suggested first-stage schema:

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
introspection = true

[dependencies]
gpio-utils = { git = "https://github.com/example/froth-gpio-utils", rev = "v0.1.0" }
```

Responsibilities of the manifest:

- declare target board/platform
- declare profile/layer choices
- declare dependencies
- give the CLI and editor enough information to build and sync

## 21. Profiles and Layering

The long-term tooling story is stronger if Froth stops being treated as one fixed build and starts being treated as profiles plus optional layers.

Recommended profile model:

- `tiny`
- `interactive`
- `persist`
- `creative`

Recommended layer toggles:

- `string-lite`
- `introspection`
- `snapshots`
- `board-lib`
- future domain layers such as `audio`

The host tooling should:

- show these choices explicitly
- validate them against board limits
- estimate memory cost where possible

This is where the "Pi Imager for Froth" experience comes from. It is really a profile/layer builder, not just a flasher.

## 22. Library Packaging

### 22.1 Recommendation

Start with Git-based libraries and a lockfile. Do not start with a registry.

### 22.2 Proposed library metadata

Each library repo should have a small manifest, for example:

- `froth-package.toml`

Suggested fields:

- package name
- version
- license
- exported source files
- required Froth profile/layers
- optional board/platform constraints
- examples

### 22.3 Why Git first

Git-based dependencies are enough for phase 1 because they:

- are easy to understand
- avoid central service maintenance
- work for open source and private code
- are easy to vendor or pin

The CLI can later add:

- `froth search`
- `froth libs outdated`
- curated index support

without requiring a registry on day one.

## 23. VS Code User Experience

### 23.1 First-run flow

The initial user flow in VS Code should be:

1. Install extension
2. Run `Froth: Doctor`
3. Connect a board
4. Run `Froth: New Project` or `Froth: Open Device`
5. Choose board and profile
6. Build and flash
7. Land in a workspace with the device session already attached

### 23.2 Primary commands

The extension should expose at least:

- send selection
- send current form
- send current definition
- send current file
- sync workspace
- interrupt device
- safe boot connect
- reset device
- save snapshot
- wipe snapshot
- inspect word at cursor

### 23.3 Panels

Recommended initial panels:

- **Console**: raw device output and REPL transcript
- **Stack**: last known structured stack
- **Words**: word list and inspect results
- **Device**: board, platform, snapshot state, heap, connection
- **Sync**: changed definitions, last send result, device/workspace drift

### 23.4 Two working modes

The extension should explicitly support two modes:

- **Live mode** for improvisational work
- **Project mode** for file-backed work

That distinction matters for both teaching and professional use.

## 24. `frothd` API Surface

The daemon API should be explicit and stable. Recommended top-level service groups:

- `workspace.*`
- `device.*`
- `build.*`
- `flash.*`
- `sync.*`
- `inspect.*`
- `session.*`

Example operations:

- `device.listPorts`
- `device.connect`
- `device.disconnect`
- `device.interrupt`
- `device.safeBootConnect`
- `build.doctor`
- `build.buildProject`
- `flash.flashProject`
- `sync.sendSelection`
- `sync.sendDefinition`
- `sync.syncFile`
- `inspect.word`
- `inspect.info`
- `session.subscribeEvents`

The goal is not maximal RPC cleverness. The goal is stable separation of concerns.

The daemon RPC contract should be specified in a language-neutral form before client implementations are allowed to drift.

## 25. Source Model and Sync Model

The host tooling should parse Froth source into top-level forms and treat those forms as the unit of synchronization.

The parser layer should provide:

- completeness detection
- top-level form boundaries
- definition name extraction where possible
- a stable content hash for each form

The sync engine should then:

- compare workspace form hashes against the last sent hashes
- send only changed forms
- record send outcomes by form and by defined word
- show drift in the UI

This is the right place to make Froth feel like SLIME rather than like "paste an entire script into serial."

## 26. Build and Flash Architecture

The build layer should reuse the conventions already established in ADR-029.

Responsibilities:

- discover SDKs in `~/.froth/sdk/`
- validate environment and toolchain presence
- choose the correct target scaffolding under `targets/`
- map board/profile/layer choices into build arguments
- flash the device

This logic belongs in the shared backend modules and the daemon, not in the VS Code extension.

## 27. Web UI

The web UI should be a second client, not the primary architecture.

The recommended sequence is:

- build the Go backend core
- build CLI
- build VS Code
- then build a local web UI over the same daemon

The web UI is valuable for:

- workshops
- classrooms
- zero-install demos
- creative coding sessions

But it should not force the project into a browser-first architecture too early.

## 28. Staged Delivery Plan

### Stage 0: documentation and ADR cleanup

- decide on transport replacement
- write the new link ADR
- update the interactive spec
- define project manifest schema

### Stage 1: host core and CLI

- implement Go transport library
- implement Go serial session layer
- implement `froth doctor`, `build`, `flash`, `send`, `info`
- no editor yet

### Stage 2: daemon

- add Go `frothd`
- move session ownership and build orchestration into the daemon
- expose JSON-RPC API

### Stage 3: VS Code extension

- TypeScript client over daemon RPC
- commands for send/sync/inspect
- console, stack, words, device panels
- thin client over daemon

### Stage 4: structured sync and richer metadata

- better word inspection
- drift tracking
- metadata and stack-effect integration
- profile/layer UI

### Stage 5: web UI and workshop provisioning

- local web client over daemon
- polished board/profile provisioning flow

### Stage 6: advanced features

- checkpoint/revert profile if desired
- debugger/tracing support
- richer package index

## 29. What Not To Do

The following are explicitly discouraged:

- do not start with a custom all-in-one editor
- do not keep `STX/ETX` framing and bolt on escape hacks
- do not make host tooling depend on scraping `see`
- do not let VS Code own build logic or serial protocol logic
- do not let the TypeScript clients become a second backend
- do not pretend `mark/release` is transactional rollback
- do not build a registry before Git-based dependencies work
- do not collapse language services and device services into editor-only code

## 30. Review Questions for Maintainers

Before implementation begins, maintainers should answer these explicitly:

1. Is the project willing to replace the current `STX/ETX` Link Mode design outright?
2. Is Go acceptable as the canonical backend language for `froth` and `frothd`?
3. Should `froth-ls` be a separate Go process from the start, or should editor-facing language features initially live inside `frothd`?
4. Do we want phase-1 tooling to include `LIST_WORDS`, or should inspection be minimal first?
5. Do we want a future checkpoint profile, or is destructive redefinition acceptable for early editor sync?
6. Which parts of `#`-line tooling output should remain normative after framed link transport exists?

## 31. Recommended Immediate Documentation Follow-Ups

If this proposal is accepted in principle, the next docs should be:

- ADR: Link transport v1 replaces current Link Mode framing
- ADR: host tooling architecture (`froth`, `frothd`, `froth-ls`, VS Code)
- ADR: project manifest and library packaging
- spec revision: update `Froth_Interactive_Development_v0_5.md`
- design note: future checkpoint/rollback model if desired

## 32. Final Recommendation

The correct long-term direction is:

- keep Froth device-first
- replace fragile text framing with a real transport
- define a small, structured tooling surface on-device
- build one authoritative backend core
- keep protocol and manifest contracts language-neutral
- implement the backend as portable native tooling
- make the CLI first, daemon second, VS Code third, web UI fourth

That path is both realistic and durable. It gives Froth a tooling story that can grow into something genuinely modern without turning the device into a dumb peripheral of the host.
