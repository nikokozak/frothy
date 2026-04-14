# Frothy Repo Audit 2026-04

Status: working audit, read-only
Date: 2026-04-14
Authority: `docs/spec/Frothy_Language_Spec_v0_1.md`, Frothy ADR-109, Frothy ADR-110, Frothy ADR-111, Frothy ADR-117, `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`

This document is the tracked audit artifact for the current publishability pass.
It does not change code or behavior. It classifies repo-tracked surfaces as
they exist now and turns cleanup into small landable tranches.

Decision rules used in this audit:

- optimize for a publishable reset, not for preserving breadth
- treat workshop concerns as sequencing constraints, not as reasons to keep
  under-owned surfaces
- classify each surface as `keep`, `merge`, `quarantine`, `archive`, or
  `delete`
- treat untracked local build noise as out of scope unless it leaks into the
  maintained path through tracked artifacts, docs, or workflows

Classification legend:

- `keep`: the surface belongs in the published Frothy repo as-is in principle
- `merge`: keep the capability, but unify overlapping surfaces into one
- `quarantine`: keep temporarily behind an explicit boundary while it is being
  replaced
- `archive`: keep only as historical or reference material, not in the active
  product path
- `delete`: remove from the maintained tree

## Current Maintained Surface

| Surface | Current truth | Authority source | Required runtimes | Packaged or local-only | Initial classification |
| --- | --- | --- | --- | --- | --- |
| Runtime and language core | `src/frothy_*` implements the Frothy language/runtime; many `src/froth_*` units remain required substrate in the same tree; POSIX host build is the default maintained developer path. | `docs/spec/Frothy_Language_Spec_v0_1.md`, `docs/reference/Froth_Substrate_References.md`, `CMakeLists.txt` | `C`, minimal `Shell` | Source of packaged host/runtime artifacts, also local dev | `keep` |
| ESP32 board and platform path | `targets/esp-idf`, `platforms/esp-idf`, and `boards/esp32-devkit-v1` are still actively built and shipped; they are not dead placeholders even where comments say `TODO`. | Frothy ADR-117, `.github/workflows/release.yml`, `targets/esp-idf/main/CMakeLists.txt` | `C`, `Shell`, ESP-IDF toolchain | Packaged firmware plus local hardware path | `keep` |
| CLI | `tools/cli` is a real shipped product surface; checkout builds emit `froth-cli`, release assets ship `froth`, and the code still carries old `froth` module identity internally. | `README.md`, `tools/cli/go.mod`, `.github/workflows/release.yml` | `Go`, minimal `Shell` | Packaged and local | `keep` |
| VS Code extension | `tools/vscode` is a real shipped product surface; the packaged extension is helper-based and direct-control-based, not daemon-based. | Frothy ADR-111, `tools/vscode/package.json`, `.github/workflows/release.yml` | `Node` | Packaged and local | `keep` |
| Proof harness | The maintained proof story is spread across root `make`, CTest, Go test runner code, shell wrappers, and hardware scripts. The capability is real; the surface is not yet tight. | Frothy ADR-109, `Makefile`, `tools/frothy/proof.sh`, `tools/cli/cmd/test-runner/*` | `C`, `Go`, `Shell`, temporary `Python` on hardware lanes | Local and CI | `merge` |
| Release automation | CLI tarballs, VSIX packaging, firmware zips, Homebrew metadata, and release workflows are all active, but the release surface still mixes transitional naming and uneven helper logic. | `.github/workflows/release.yml`, `tools/package-release.sh`, `tools/package-firmware-release.sh`, `tools/release-common.sh` | `Go`, `Shell`, `Node`, temporary `Python` | Release-only | `merge` |
| Control docs | The live control surface is mostly thin and correct: repo root README, roadmap current-state block, Frothy ADRs, `PROGRESS.md`, `TIMELINE.md`, and extension README all matter, but some install/release guidance is duplicated. | `README.md`, `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`, Frothy ADR-116, `PROGRESS.md`, `TIMELINE.md`, `tools/vscode/README.md` | none | Packaged docs plus local repo docs | `merge` |
| Reference and archive docs | `docs/archive/` and `docs/reference/` are legitimate reference surfaces, not product surfaces. They should stay explicit and non-authoritative. | `AGENTS.md`, `docs/spec/README.md`, `docs/adr/README.md`, `docs/reference/Froth_Substrate_References.md` | none | Local-only | `archive` |

## Findings Ledger

### Identity Matrix

| Artifact | Current name | Why it exists now | Keep? | Target end state |
| --- | --- | --- | --- | --- |
| Repo, product, docs, extension branding | `Frothy` | Accepted product identity | yes | keep `Frothy` |
| Repo-local CLI checkout binary | `froth-cli` | Distinguishes local build from installed binary during the transition | temporary | remove once one published binary name is frozen |
| Release/install CLI binary | `froth` | Explicit transitional command name in current packaging and docs | temporary | converge to one publishable CLI name after the naming tranche |
| Go module path | `github.com/nikokozak/froth/tools/cli` | Fork inheritance leak, not product policy | no | rename to Frothy-owned module path |
| Runtime symbol prefixes | `froth_*` substrate plus `frothy_*` product | Real retained substrate and product code coexist today | yes, with boundary | keep both only if the retained substrate boundary is documented and explicit |

| Area | Item | Current purpose | Authority source | Evidence | Required runtimes | Packaged or local-only | Classification | Recommended action | Migration blocker | Proof command after cleanup |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Editor/control | `tools/vscode/src/daemon-client.ts`, `tools/vscode/src/daemon-supervisor.ts`, `tools/vscode/src/froth-paths.ts` | Historical daemon/socket editor path | Frothy ADR-111, `tools/vscode/package.json`, `tools/vscode/test/package-smoke.js` | Active extension code imports `control-session-client`; packaged files omit daemon outputs; package smoke explicitly forbids `out/daemon-client.js`, `out/daemon-supervisor.js`, and `out/froth-paths.js` | `Node` | Local-only source | `delete` | Remove daemon-era TS source and any stale references; keep the helper-owned control-session path only | none; the packaged path already excludes these files | `cd tools/vscode && npm test && npm run test:package` |
| Identity | `tools/cli/go.mod` and Go imports still use `github.com/nikokozak/froth/...` | Buildable CLI module path | Frothy ADR-109, `tools/cli/go.mod`, `tools/cli/main.go` | Module declaration and imports still use the old fork identity | `Go` | Packaged and local | `merge` | Normalize module path and internal imports in one dedicated naming tranche after the binary-name policy is frozen | avoid mixing module rename with the eventual CLI binary rename | `cd tools/cli && go test ./... && go list ./...` |
| Identity | Repo/product/CLI naming split leaks through docs and helper discovery | Transitional compatibility across repo-local, packaged, and intended identities | `README.md`, `tools/vscode/README.md`, `tools/vscode/src/cli-discovery.ts`, `tools/cli/cmd/root.go` | `Frothy`, `froth`, and `froth-cli` all appear on active paths; some uses are honest transition notes, others are spread too widely | `Go`, `Node` | Packaged and local | `merge` | Freeze one published naming matrix, then shrink references so only the sanctioned transition points still mention the old names | current packaged binary name remains `froth` | `make test-all && rg -n 'froth-cli|release-time `froth`|Frothy CLI' README.md tools/vscode/README.md tools/vscode/src/cli-discovery.ts tools/cli/cmd/root.go` |
| Proofs | Proof orchestration is split across `make`, Go, shell, and Python | Run host tests, CLI tests, and hardware smokes | Frothy ADR-109, `Makefile`, `tools/frothy/proof.sh`, `tools/cli/cmd/test-runner/proofs.go` | Root `make` builds a Go test runner, which shells into `tools/frothy/proof.sh`, which shells into multiple scripts and sometimes Python | `C`, `Go`, `Shell`, temporary `Python` | Local and CI | `merge` | Collapse proof dispatch around the Go test runner and a four-tier proof matrix; keep shell as thin glue only where it adds no policy | ESP32 device lanes still depend on shell/Python wrappers | `make test && make test-all && make test-list` |
| Repo pollution | Tracked binary `tools/cli/test-runner` | None; it is a compiled local executable | Frothy ADR-109, `Makefile`, `tools/cli/Makefile` | `file tools/cli/test-runner` reports a Mach-O arm64 executable; root `make` already builds the maintained runner to `build/test/bin/froth-test-runner` | none | Local-only tracked artifact | `delete` | Untrack the binary and rely on generated build output only | none | `! git ls-files | rg '^tools/cli/test-runner$' && make test` |
| Dependency budget | Inline Python in `tools/package-firmware-release.sh` | Parse `flasher_args.json` for firmware zips | `tools/package-firmware-release.sh`, `.github/workflows/release.yml` | Release packaging uses `python3 -c` only to read JSON and print file names | `Shell`, temporary `Python` | Release-only | `quarantine` | Rewrite manifest parsing into Go or another non-Python helper so release packaging does not need Python | none technical; only implementation time | `! rg -n 'python3' tools/package-firmware-release.sh && ./tools/package-firmware-release.sh targets/esp-idf/build $(cat VERSION) /tmp/frothy-firmware-test.zip` |
| Dependency budget | Python-backed hardware smokes | Exercise ESP32 proof paths | `tools/frothy/proof_m10_smoke.sh`, `tools/frothy/proof_m9_esp32_ffi_smoke.py`, `docs/roadmap/Frothy_Development_Roadmap_v0_1.md` | `proof_m10_smoke.sh` requires `python3` and execs `proof_m10_esp32_smoke.py`; M9 hardware smoke is also Python | `Shell`, temporary `Python` | Hardware-only | `quarantine` | Keep Python only as an explicitly temporary hardware-only exception until a Go helper or simpler manual proof replaces it | serial transcript and board interaction logic currently live in Python | `make test && make test-all` for core lanes; hardware lane remains an explicit separate exception until replaced |
| Runtime boundary | `src/froth_*` retained substrate and `src/frothy_*` product code are interleaved | Build the actual runtime today | `docs/reference/Froth_Substrate_References.md`, `CMakeLists.txt` | Host and ESP32 targets compile a large mixed list of `froth_*` and `frothy_*` units from the same directory without an explicit structural boundary | `C` | Packaged and local | `merge` | Produce an explicit retained-substrate manifest, then separate or namespace substrate code without blindly renaming working units | Frothy runtime and tests directly link many substrate units | `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure -L frothy` |
| Runtime boundary | Compatibility layer files `src/frothy_console_compat.c` and `src/frothy_link_stub.c` | Bridge inherited interfaces during the transition | Frothy ADR-109, Frothy ADR-110, `src/frothy_console_compat.c`, `src/frothy_link_stub.c` | `frothy_console_compat.c` exists only to answer an inherited idle query; `frothy_link_stub.c` returns `FROTH_ERROR_LINK_UNKNOWN_TYPE` for inherited link callbacks | `C` | Packaged and local | `quarantine` | Isolate these as an explicit compatibility layer or delete them once nothing depends on the inherited interfaces | host and ESP32 targets still compile them | `rg -n 'compat|stub' src/frothy_console_compat.c src/frothy_link_stub.c CMakeLists.txt targets/esp-idf/main/CMakeLists.txt && make test` |
| Hygiene | Misleading `TODO` banners on maintained ESP32 files | None; these comments now understate real code | Frothy ADR-117, `boards/esp32-devkit-v1/ffi.c`, `platforms/esp-idf/platform.c`, `targets/esp-idf/main/main.c` | `ffi.c` and `platform.c` contain real active implementations but still open with `TODO`; `main.c` is a live wrapper but also marked `TODO` | `C` | Packaged and local | `merge` | Remove false placeholder labeling from kept files so TODO means genuinely incomplete work again | none | `! rg -n '^/\\* TODO:' boards/esp32-devkit-v1/ffi.c platforms/esp-idf/platform.c targets/esp-idf/main/main.c && make test-all` |
| Docs | Install and release guidance is duplicated across root README, extension README, and roadmap notes | Explain install, extension, and workshop path | `README.md`, `tools/vscode/README.md`, `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md` | Brew install, `froth doctor`, and VSIX install appear in more than one maintained doc; roadmap notes also repeat support/install intent | none | Packaged docs and local docs | `merge` | Keep one repo front door, shorten extension README to extension-specific behavior, keep roadmap notes queue-shaped rather than user-install-shaped | support matrix is not yet fully frozen | `rg -n 'brew install frothy|code --install-extension|froth doctor' README.md tools/vscode/README.md` |
| Dependency budget | Node toolchain is legitimate only because the VS Code extension is a kept shipped surface | Build, test, and package the extension | Frothy ADR-111, `.github/workflows/release.yml`, `tools/vscode/package.json` | Release workflow has a dedicated `build-vscode` job; root `make test` does not require Node | `Node` | Release-only and extension-local | `keep` | Codify Node as extension-only and release-only; do not add it to the root Frothy proof contract | none | `make test && make test-all && (cd tools/vscode && npm test && npm run test:package)` |

## Cut Ledger

| Area | Item | Current purpose | Authority source | Evidence | Required runtimes | Packaged or local-only | Classification | Recommended action | Migration blocker | Proof command after cleanup |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Editor/control | `tools/vscode/src/daemon-client.ts` | Historical daemon JSON-RPC client | Frothy ADR-111, `tools/vscode/test/package-smoke.js` | Forbidden from the packaged VSIX and unused by the active helper-based extension | `Node` | Local-only | `delete` | Delete the file and keep helper-owned control session only | none | `cd tools/vscode && npm test && npm run test:package` |
| Editor/control | `tools/vscode/src/daemon-supervisor.ts` | Historical daemon lifecycle manager | Frothy ADR-111, `tools/vscode/test/package-smoke.js` | Forbidden from the packaged VSIX and contradicts the no-daemon editor path | `Node` | Local-only | `delete` | Delete the file and remove any remaining daemon ownership assumptions | none | `cd tools/vscode && npm test && npm run test:package` |
| Editor/control | `tools/vscode/src/froth-paths.ts` | Historical daemon socket path helper | Frothy ADR-111, `tools/vscode/test/package-smoke.js` | Its only purpose is the daemon socket path; package smoke forbids its output | `Node` | Local-only | `delete` | Delete the file with the daemon client/supervisor tranche | none | `cd tools/vscode && npm test && npm run test:package` |
| Repo pollution | `tools/cli/test-runner` | None; compiled artifact committed into the tree | Frothy ADR-109, `Makefile` | Tracked Mach-O executable; maintained build emits runner under `build/test/bin` instead | none | Local-only | `delete` | Remove from Git and keep it in `.gitignore` by policy through generated build paths only | none | `! git ls-files | rg '^tools/cli/test-runner$' && make test` |
| Historical proof artifacts | `tools/frothy/m10_esp32_proof_transcript.txt` | Historical board-proof transcript | `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`, `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md` | It is evidence, not executable tooling, but it lives under `tools/frothy/` | none | Local-only tracked reference artifact | `archive` | Move to a docs/reference or docs/archive proof-evidence location and update references | roadmap and priority docs currently cite the existing path | `rg -n 'm10_esp32_proof_transcript' docs/roadmap docs/reference docs/archive && ! git ls-files | rg '^tools/frothy/m10_esp32_proof_transcript.txt$'` |
| Historical proof artifacts | `tools/frothy/m3a_esp32_prompt_transcript.txt` | Historical device-smoke transcript | `TIMELINE.md`, `docs/roadmap/Frothy_Development_Roadmap_v0_1.md` | It is historical proof evidence, not active tooling, but it also lives under `tools/frothy/` | none | Local-only tracked reference artifact | `archive` | Move beside other proof evidence under docs, not under active tooling | any doc references to the old path must be updated | `rg -n 'm3a_esp32_prompt_transcript' docs/roadmap docs/reference docs/archive && ! git ls-files | rg '^tools/frothy/m3a_esp32_prompt_transcript.txt$'` |

## Consolidation Ledger

| Area | Item | Current purpose | Authority source | Evidence | Required runtimes | Packaged or local-only | Classification | Recommended action | Migration blocker | Proof command after cleanup |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Identity | Published identity matrix for repo, CLI binary, module path, and docs wording | Keep the fork transition workable | Frothy ADR-109, `README.md`, `tools/cli/go.mod`, `tools/vscode/src/cli-discovery.ts` | Identity is currently explicit but spread across too many surfaces | `Go`, `Node` | Packaged and local | `merge` | Freeze one matrix, then land module-path cleanup and naming cleanup as separate reviewable cuts | binary rename and module rename should not land in the same patch | `make test-all && cd tools/cli && go test ./...` |
| Proofs | Root proof entrypoints, Go runner, shell wrappers, and hardware lanes | Run local, CI, and hardware verification | Frothy ADR-109, `Makefile`, `tools/frothy/proof.sh`, `tools/cli/cmd/test-runner/*` | There are too many entrypoints for what is conceptually one proof system | `C`, `Go`, `Shell`, temporary `Python` | Local and CI | `merge` | Keep one proof matrix and one main dispatcher; demote shell to thin wrappers and quarantine hardware-only exceptions | hardware lanes still need distinct device handling | `make test && make test-all && make test-list` |
| Packaging | CLI tarball packaging, firmware zip packaging, VSIX packaging, and release notes | Ship all released artifacts | `.github/workflows/release.yml`, `tools/package-release.sh`, `tools/package-firmware-release.sh`, `tools/vscode/package.json` | Packaging is functionally complete but implemented in three styles with uneven dependency choices | `Go`, `Shell`, `Node`, temporary `Python` | Release-only | `merge` | Normalize around a smaller release surface and one dependency budget; keep Node only for the extension and remove Python from release glue | firmware packaging still uses inline Python | `.github/workflows/release.yml` succeeds on all three artifact jobs |
| Runtime boundary | Retained Froth substrate versus Frothy product code | Keep working substrate while tightening repo shape | `docs/reference/Froth_Substrate_References.md`, `CMakeLists.txt` | Boundary is real in docs but not explicit in directory layout or naming policy | `C` | Packaged and local | `merge` | Write down the retained substrate set, quarantine compatibility shims, then move or namespace only what remains justified | large CMake/test link surface still mixes both layers | `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure -L frothy` |
| Docs | Root README, extension README, roadmap notes, and queue docs | Explain install, release, and active control surface | Frothy ADR-116, `README.md`, `tools/vscode/README.md`, `PROGRESS.md`, `TIMELINE.md` | Guidance is mostly truthful but still repeated in more than one place | none | Packaged docs and local docs | `merge` | Keep one front door, one extension-specific doc, one live queue, and archive historical prose that still matters | frozen support matrix is not yet fully declared | `rg -n 'brew install frothy|code --install-extension|froth doctor' README.md tools/vscode/README.md` |
| Dependency budget | Node and Python exceptions | Support the extension and temporary hardware/release helpers | Frothy ADR-111, `tools/vscode/package.json`, `tools/frothy/proof_m10_smoke.sh`, `tools/package-firmware-release.sh` | Node is justified by the kept VSIX surface; Python is only justified by temporary hardware/release helpers | `Node`, temporary `Python` | Release-only and hardware-only | `quarantine` | Make exceptions explicit, prevent them from entering the core root proof path, and remove Python from release first | hardware smokes still use Python today | `make test && make test-all` with no Node or Python required |
| Board support | Sanctioned board/platform surface | Keep the repo small and truthful | Frothy ADR-117, `.github/workflows/release.yml`, `boards/README.md` | The active published path is effectively POSIX host plus `esp32-devkit-v1`; the rest should not be implied as a broad support matrix | `C`, `Shell` | Packaged and local | `merge` | Publish an explicit support matrix and trim or quarantine anything that suggests broader active support than the repo actually proves | workshop support matrix still needs to be frozen in docs | `rg -n 'esp32-devkit-v1|posix' README.md tools/vscode/README.md .github/workflows/release.yml boards/README.md` |

## Implementation Tranches

### Tranche A: Audit Scaffold

Purpose:

- land this audit artifact and freeze the cleanup vocabulary before changing
  code

Scope:

- no runtime or tooling behavior changes
- no doc edits outside this file
- classify every top-level maintained surface once

Exit criteria:

- this audit exists in-tree
- all six required sections are present
- every finding row carries a classification and a proof target

Proof:

- `rg -n '^## Current Maintained Surface|^## Findings Ledger|^## Cut Ledger|^## Consolidation Ledger|^## Implementation Tranches|^## Proof Impact' docs/audit/Frothy_Repo_Audit_2026-04.md`

### Tranche B: Immediate Cuts

Purpose:

- remove dead packaged-surface drift and tracked repo pollution first

In scope:

- delete the daemon-era VS Code files
- untrack `tools/cli/test-runner`
- archive proof transcript artifacts out of `tools/frothy/`

Held boundary:

- no binary rename
- no module-path rename
- no proof runner rewrite yet

Exit criteria:

- no daemon-era files remain in the active extension tree
- no compiled binaries are tracked in `tools/cli/`
- historical transcripts live under docs, not active tooling

Proof:

- `make test`
- `cd tools/vscode && npm test && npm run test:package`

### Tranche C: Identity and Packaging Normalization

Purpose:

- freeze one publishable naming story and remove avoidable identity leaks

In scope:

- publish the identity matrix in README/release/install docs
- normalize Go module path and imports
- reduce stray `froth`/`froth-cli` wording outside sanctioned transition points

Held boundary:

- do not rename the installed CLI binary in the same tranche as the module path
- do not touch runtime symbol prefixes yet

Exit criteria:

- one documented identity matrix exists
- the Go module path no longer advertises the old repo identity
- only sanctioned transition points mention old binary names

Proof:

- `cd tools/cli && go test ./...`
- `make test-all`

### Tranche D: Proof and Dependency Collapse

Purpose:

- reduce the proof story to one four-tier matrix and push non-core runtimes out
  of the default path

In scope:

- collapse proof dispatch around the Go runner
- remove Python from release packaging
- keep hardware-only exceptions explicit
- codify Node as extension-only and release-only

Held boundary:

- hardware smoke may stay temporarily separate if the replacement is not ready
- do not widen the root `make` surface to require Node or Python

Exit criteria:

- `make test` and `make test-all` need only `C`, `Go`, and thin `Shell`
- firmware packaging no longer shells out to Python
- the proof matrix is documented and discoverable through one command

Proof:

- `make test`
- `make test-all`
- `make test-list`

### Tranche E: Runtime Boundary Tightening

Purpose:

- make the retained Froth substrate explicit and quarantine compatibility glue

In scope:

- write down the retained substrate set in code-adjacent docs or build metadata
- isolate compatibility shims and stubs
- remove false `TODO`/placeholder signaling from active maintained files

Held boundary:

- no speculative rewrite of working substrate
- no blind global renaming of `froth_*` symbols

Exit criteria:

- every retained `froth_*` unit has a clear justification
- compatibility shims are explicit and bounded
- active maintained files no longer present themselves as placeholders

Proof:

- `cmake -S . -B build && cmake --build build`
- `ctest --test-dir build --output-on-failure -L frothy`

### Tranche F: Docs Front Door and Archive Pass

Purpose:

- make the repo read like one small product again

In scope:

- keep one root front door
- shorten extension docs to extension-specific behavior
- keep roadmap, `PROGRESS.md`, and `TIMELINE.md` queue-shaped only
- move historical proof artifacts and stale prose into archive/reference space

Held boundary:

- do not turn the roadmap or timeline back into narrative status logs
- do not duplicate install instructions across product docs

Exit criteria:

- install and release instructions have one canonical home
- extension README no longer repeats repo-front-door material
- historical proof evidence is archived, not mixed into active tooling

Proof:

- `rg -n 'brew install frothy|code --install-extension|froth doctor' README.md tools/vscode/README.md`
- `sh tools/frothy/proof_control_surface_docs.sh`

## Proof Impact

### Target Proof Tiers

| Tier | Purpose | Target commands | Required runtimes | Policy |
| --- | --- | --- | --- | --- |
| Core local gate | Fast default proof for normal Frothy work | `make test` | `C`, `Go`, minimal `Shell` | Must not require `Node` or `Python` |
| Extended local gate | Full host and CLI regression pass | `make test-all` | `C`, `Go`, minimal `Shell` | Must not require `Node` or `Python` |
| Hardware-only gate | Device-specific proof for sanctioned board paths | `sh tools/frothy/proof.sh m10 <PORT>` or its later replacement | `C`, `Go`, `Shell`, temporary `Python` until replaced | Explicit exception only; not part of default local publishability gate |
| Release-only gate | Build shipped artifacts and packaging proofs | `.github/workflows/release.yml` lanes or local equivalents | `Go`, `Shell`, `Node`, temporary `Python` until firmware packaging is rewritten | `Node` is justified only here and in extension-local development |

### Dependency Budget

- Core repo budget: `C`, `Go`, minimal `Shell`
- `Node` is allowed only because the VS Code extension is a kept shipped
  surface
- `Python` is not acceptable in the long-term core Frothy path
- Temporary `Python` exceptions are allowed only in explicit hardware-only or
  release-only lanes until replaced
- No tracked compiled binaries or generated SDK artifacts belong in the repo

### Completion Bar For This Audit

This audit is complete when all of the following are true:

- every top-level maintained surface in the repo is classified
- every removal candidate has explicit evidence
- every kept surface has one proof command after cleanup
- every non-core dependency is justified or marked for migration out
- the cleanup backlog is ordered into independent tranches with no hidden
  policy decisions left for implementation
