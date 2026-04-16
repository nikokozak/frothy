# Frothy

Frothy is a small live lexical language for programmable devices.

Frothy `v0.1` is functionally closed.

The live roadmap milestone is `workshop operational closeout`. The remaining
work is operational rather than architectural: execute the promised
clean-machine validation, finish the room-side hardware/recovery pack-out, and
record one complete workshop-board rehearsal on the maintained path.

The forward queue after that is explicit:

- FFI boundary quality and porting discipline
- small useful core library growth
- robust string support
- measured performance tightening
- direct-control tooling improvements
- later workspace/image-flow growth only after the workshop-critical tranches
  prove themselves

See `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md` for the
kept-vs-deferred stack.

The repo reuses inherited Froth substrate where that is the simplest working
path, but Froth's old roadmap, stack-centric user model, and language
priorities are not active policy here.

## Start Here

Use the smallest maintained doc set for the workshop path:

- `docs/guide/Frothy_Workshop_Install_Quickstart.md`: attendee install note
  and preflight
- `docs/guide/Frothy_Workshop_Quick_Reference.md`: first connect, inspection,
  board surface, persistence, and troubleshooting
- `docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`: promised-platform
  validation checklist and recording sheet
- `boards/esp32-devkit-v4-game-board/WORKSHOP.md`: room-side hardware pack-out and
  recovery card
- `workshop/README.md`: the tiny checked-in source for the public workshop
  repo, published at [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop)
- `docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md`: workshop
  rehearsal status note and required real-device proof command

## Workshop Support Matrix

The promised attendee path is smaller than the repo surface.
These are the assets and listings the current manual release path is set up to
publish:

| Surface | Release surface | Workshop promise |
| --- | --- | --- |
| CLI release | `frothy-v<version>-darwin-arm64.tar.gz`, `frothy-v<version>-darwin-amd64.tar.gz`, `frothy-v<version>-linux-amd64.tar.gz` | macOS via Homebrew is the preferred attendee path; Linux x86_64 can use the release tarball directly |
| VS Code | Marketplace listing `NikolaiKozak.frothy`, with matching `frothy-vscode-v<extension-version>.vsix` fallback | supported on the same machines that can already run the installed CLI |
| Firmware / recovery | workshop-board recovery for `esp32-devkit-v4-game-board` is maintainer-only from the repo checkout and [boards/esp32-devkit-v4-game-board/WORKSHOP.md](/Users/niko/Developer/Frothy/boards/esp32-devkit-v4-game-board/WORKSHOP.md) | attendees do not flash; maintainers carry preflashed `esp32-devkit-v4-game-board` boards |
| Source build | checkout build via `make build` | maintainer path, not required before the workshop |
| Workshop repo | [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop) containing `README.md` and `pong.frothy` | attendees open and edit `pong.frothy` against the preflashed demo board |

Windows, extra boards, and custom toolchain setups are not part of the
maintained attendee promise for this tranche.

Board targets such as `esp32-devkit-v1` and `esp32-devkit-v4-game-board`
refer to specific hardware revisions. They are not Frothy protocol or repo
generation markers. The workshop promise is narrower and currently centers on
the mounted preflashed `esp32-devkit-v4-game-board`, but `esp32-devkit-v1`
remains an accepted board model in the repo.

## Naming Matrix

The published naming split is explicit for now:

| Thing | Name today |
| --- | --- |
| Product, repo, docs, release assets, Homebrew formula, and editor surface | `Frothy` / `frothy` |
| Installed CLI command from released assets | `frothy` |
| Repo-local checkout CLI build | `tools/cli/frothy-cli` |
| Host runtime built from source | `build/Frothy` |

The CLI rename tranche is now landed: Frothy owns the public product and
release identity, and the installed/repo-local CLI surface is `frothy`.
VS Code still keeps legacy `froth` discovery as a temporary compatibility path
during the transition.

## Workshop Install

The attendee quickstart lives in
`docs/guide/Frothy_Workshop_Install_Quickstart.md`.
The in-room prompt and recovery cheat sheet lives in
`docs/guide/Frothy_Workshop_Quick_Reference.md`.

Use that guide for the exact Homebrew, release-tarball, and VSIX install
commands.

The maintained workshop assumptions for this tranche are:

- attendees use the installed CLI command `frothy`
- attendees do not need a repo checkout, `esp-idf`, or source builds before
  they arrive
- the current workshop run uses a preflashed `esp32-devkit-v4-game-board`
  proto board
- the public workshop repo is [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop)
- if VS Code cannot find `frothy` on `PATH`, set `frothy.cliPath` to the
  absolute path of the installed binary; legacy `froth` fallback remains
  available during the transition

The maintained editor path stays on the accepted direct-control surface:

- VS Code owns one helper child per window
- the helper owns one direct control session at a time
- there is no daemon, shared port owner, or local editor runtime in the
  maintained workshop path

`Send Selection / Form` is intentional additive eval.
`Send File` is whole-file `reset + eval`; if the connected firmware is too old
for control `reset`, the extension blocks the send and asks you to upgrade or
reflash instead of replaying the file unsafely.

## Build And Test

From a checkout:

```sh
make build
make run
make test
make test-all
make test-publishability
```

Optional extension lanes:

```sh
make test-vscode
make test-vscode-board PORT=/dev/...
```

The host build produces:

- `build/Frothy`: primary Frothy host runtime

The maintained test contract is:

- `make test`: fast self-contained local gate (`C`, `Go`, `Shell`)
- `make test-all`: exhaustive local gate (`C`, `Go`, `Shell`)
- `make test-publishability`: full shipped-surface local gate (`make test-all` plus `make test-vscode`)
- `make test-vscode`: explicit extension-local `Node` lane
- `make test-vscode-board PORT=/dev/...`: explicit real-device extension lane
- `make test-list`: list maintained suites and profiles
- `sh tools/frothy/proof_workshop_ops_docs.sh`: workshop front-door and ops
  docs sanity
- `sh tools/frothy/proof.sh workshop-v4 <PORT>`: focused non-interactive
  real-device `esp32-devkit-v4-game-board` workshop proof
- `sh tools/frothy/proof.sh workshop-v4 --live-controls <PORT>`: optional
  manual joystick/button extension to the same board proof

Run the currently shipped CLI as `frothy`:

```sh
frothy --version
frothy doctor
frothy build
frothy flash
frothy connect
frothy send src/main.froth
```

## Active Docs

- `docs/spec/Frothy_Language_Spec_v0_1.md`: normative Frothy language and
  interactive-profile spec
- `docs/spec/Frothy_Language_Spec_vNext.md` and
  `docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md`: draft next-stage
  language direction without widening current behavior
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`: live control surface and
  accepted milestone roadmap
- `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`: near-term
  post-`v0.1` queue and workshop gate
- `docs/adr/README.md`: ADR authority split and Frothy `100`-series index
- `PROGRESS.md`: thin operational note
- `TIMELINE.md`: movable checkbox ledger

## Reference Material

The original Froth repo at `/Users/niko/Developer/Froth` is reference material
only. Use it for substrate reuse, boot/persistence/transport background, and
implementation salvage where explicitly adopted. Do not treat its roadmap,
language semantics, AGENTS guidance, or implementation priorities as active
Frothy policy.

See `docs/reference/Froth_Substrate_References.md` and
`docs/reference/Frothy_Retained_Substrate_Manifest.md` for the curated
reference set and the current retained-substrate boundary.

Historical Froth-era design notes now live under `docs/archive/`.

## Repo Shape

- The repo root is the source of truth for kernel, platform, board, and target
  sources.
- `make sdk-payload` generates the CLI's embedded SDK archive from that source
  tree; the repo does not track a maintained mirror under
  `tools/cli/internal/sdk`.
- Stale bootstrap drafts, legacy runtime code, and archived Froth design notes
  are not part of the live control surface.
