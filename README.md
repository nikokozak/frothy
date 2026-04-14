# Frothy

Frothy is a small live lexical language for programmable devices.

Frothy `v0.1` is functionally closed. The live control surface is in
`docs/roadmap/Frothy_Development_Roadmap_v0_1.md`; the next queued artifact is
host-only slot-bundle inspection/generation in the CLI project layer, while
accepted language, snapshot, and FFI semantics remain on the closed `v0.1`
baseline.

The repo reuses inherited Froth substrate where that is the simplest working
path, but Froth's old roadmap, stack-centric user model, and language
priorities are not active policy here.

## Build And Test

From a checkout:

```sh
make build
make run
make test
make test-all
```

The host build produces:

- `build/Frothy`: primary Frothy host runtime

The maintained test contract is:

- `make test`: fast self-contained local gate
- `make test-all`: exhaustive local gate
- `make test-list`: list maintained suites and profiles

The current CLI naming split is explicit:

- repo-local `froth-cli`: checkout builds produce `tools/cli/froth-cli`
- release-time `froth`: packaged and installed CLI command name today
- intended global `frothy`: product, repo, and editor identity that later
  cleanup may converge toward

Run the currently shipped CLI as `froth`:

```sh
froth --version
froth doctor
froth build
froth flash
froth connect
froth send src/main.froth
```

That command surface stays transitional for now. Frothy repo policy and release
identity are separated from inherited Froth, but the command and
implementation-symbol transition is deliberately narrower than the
language/runtime cleanup.

## Active Docs

- `docs/spec/Frothy_Language_Spec_v0_1.md`: normative Frothy language and
  interactive-profile spec
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`: live control surface and
  milestone tracker
- `docs/adr/README.md`: ADR authority split and Frothy `100`-series index
- `PROGRESS.md`: thin execution journal
- `TIMELINE.md`: thin milestone ledger

## Reference Material

The original Froth repo at `/Users/niko/Developer/Froth` is reference material
only. Use it for substrate reuse, boot/persistence/transport background, and
implementation salvage where explicitly adopted. Do not treat its roadmap,
language semantics, AGENTS guidance, or implementation priorities as active
Frothy policy.

See `docs/reference/Froth_Substrate_References.md` for the curated reference
set that remains useful during the transition.

Historical Froth-era design notes now live under `docs/archive/`.

## Repo Shape

- The repo root is the source of truth for kernel, platform, board, and target
  sources.
- `make sdk-payload` generates the CLI's embedded SDK archive from that source
  tree; the repo does not track a maintained mirror under
  `tools/cli/internal/sdk`.
- Stale bootstrap drafts, legacy runtime code, and archived Froth design notes
  are not part of the live control surface.
