# Frothy ADR-120: CLI Command And Home Identity

**Date**: 2026-04-15
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 1 and 8; Appendix B
**Roadmap milestone(s)**: post-M10 release/install identity tranche
**Inherited Froth references**: `docs/adr/059-version-spine.md`, `docs/adr/060-distribution-pipeline.md`

## Context

Frothy already owns the repo, product, release-asset, Homebrew, and editor
identity, but the shipped CLI command and default home directory still carried
the inherited `froth` name.

That left two practical problems on the maintained workshop path:

- side-by-side Froth and Frothy installs still collide on `froth` in `PATH`
- side-by-side installs still collide on the default `~/.froth` state
  directory unless the user overrides it manually

Frothy still needs a narrow tranche here:

- rename the user-facing CLI/install surface without renaming internal
  `froth_*` C symbols
- keep `froth.toml`, `.froth-build`, and source-extension authority unchanged
  in this patch
- keep VS Code discovery compatible with legacy `froth` during the transition
  instead of shipping two equal installed commands

## Options Considered

### Option A: Keep the transitional `froth` CLI and `~/.froth` default longer

Leave the installed command and default home directory unchanged until a later
larger cleanup.

Trade-offs:

- Pro: smallest immediate diff.
- Con: keeps side-by-side install collisions on both `PATH` and default state.
- Con: keeps workshop/install docs teaching a knowingly ambiguous command.

### Option B: Rename the user-facing CLI and default home now, keep explicit legacy fallback

Ship `frothy` as the installed and repo-local CLI, move the default home to
`~/.frothy`, add `FROTHY_HOME`, and keep legacy `froth` discovery only where
compatibility is still needed during the transition.

Trade-offs:

- Pro: removes the default collision path for side-by-side installs.
- Pro: keeps the tranche narrow and user-facing.
- Pro: does not require a broad internal symbol or project-format rewrite.
- Con: requires touching packaging, docs, tests, and editor discovery together.

### Option C: Ship both `frothy` and `froth` as equal installed commands

Publish both names from the same release artifacts.

Trade-offs:

- Pro: easiest short-term compatibility story.
- Con: preserves command ambiguity and `PATH` conflicts.
- Con: weakens the rename by teaching two equal names at once.

## Decision

**Option B.**

Frothy now adopts the Frothy-owned CLI/install identity:

- the installed CLI command is `frothy`
- the repo-local checkout build is `tools/cli/frothy-cli`
- CLI help, usage, and version text print `frothy`
- release packaging, release workflow, and Homebrew install `frothy`
- `FROTHY_HOME` is the primary environment override
- the default Frothy state directory is `~/.frothy`
- legacy `FROTH_HOME` remains a compatibility fallback only when
  `FROTHY_HOME` is unset
- VS Code discovery prefers `frothy` first and keeps legacy `froth` fallback
  during the transition

This ADR changes the user-facing CLI/install identity and default state-home
policy only.
It does not rename internal `froth_*` symbols, `froth.toml`, `.froth-build`,
or source extensions.

## Consequences

- Frothy and Froth no longer collide by default on the installed command name.
- Frothy and Froth no longer share the same default home/state directory.
- Compatibility remains explicit instead of silent: legacy `froth` discovery
  and `FROTH_HOME` fallback are transitional, not equal primary surfaces.
- A later tranche may still revisit `froth.toml`, `.froth-build`, or source
  extension naming, but that is now a separate decision.

## References

- `docs/adr/100-repo-and-release-identity.md`
- `README.md`
- `tools/cli/cmd/root.go`
- `tools/cli/internal/sdk/extract.go`
- `tools/package-release.sh`
- `.github/workflows/release.yml`
- `tools/vscode/src/cli-discovery.ts`
