# Frothy ADR-122: Public Workshop Release Surface

**Date**: 2026-04-15
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 8 and 10
**Roadmap queue item**: `Support matrix and release/install artifacts`
**Related ADRs**: `docs/adr/100-repo-and-release-identity.md`, `docs/adr/111-vscode-extension-owned-control-session.md`, `docs/adr/120-cli-command-and-home-identity.md`, `docs/adr/121-workshop-base-image-board-library-surface.md`

## Context

Frothy is moving from an in-repo publishability reset into an actual public
distribution surface for workshop attendees and other users.

The release/install path needs to stay:

- simple for attendees
- simple for maintainers
- truthful about what is and is not published
- aligned with the preflashed workshop board model

Keeping a half-live attendee firmware download path, a special workshop
scaffold, and multiple competing editor distribution stories would add failure
surface without making the workshop easier to run.

## Decision

The maintained public workshop release surface is:

- one public `frothy` CLI release surface
- one public Homebrew tap for the installed `frothy` command
- one VS Code Marketplace listing, with matching VSIX fallback
- one tiny workshop repo containing `README.md` and `pong.frothy`
- one maintained workshop board promise: the preflashed
  `esp32-devkit-v4-game-board`

Attendee-facing release assets are:

- `frothy-v<version>-darwin-arm64.tar.gz`
- `frothy-v<version>-darwin-amd64.tar.gz`
- `frothy-v<version>-linux-amd64.tar.gz`
- `frothy-vscode-v<version>.vsix`
- `frothy-v<version>-checksums.txt`

This tranche does not publish an attendee-facing firmware zip.

`frothy flash` on an installed CLI is source-based only.
Attendee boards are preflashed.
Maintainer recovery stays source-based from a repo checkout.

The workshop repo is an exported artifact, not a second authored source:

- the canonical demo-board source stays in
  `boards/esp32-devkit-v4-game-board/lib/base.frothy`
- `workshop/pong.frothy` is generated from that source
- release automation must verify that export before publishing

The manual release workflow remains explicit:

- build CLI tarballs
- package the VSIX
- verify workshop docs and workshop export
- publish the GitHub release
- update the Homebrew tap

## Consequences

- attendee setup stays focused on install, connect, edit, save, and wipe
- the CLI no longer pretends to offer a public prebuilt firmware recovery path
- the release workflow is smaller and more truthful
- v1 and v4 board names remain board-model identifiers, not protocol
  generations; the workshop promise is simply narrower than the repo surface

## Proof

- `make test`
- `make test-publishability`
- `sh tools/frothy/export_workshop_repo.sh check`
- `sh tools/frothy/proof_workshop_ops_docs.sh`
- `make release`
