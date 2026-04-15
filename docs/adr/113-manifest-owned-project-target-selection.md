# Frothy ADR-113: Manifest-Owned Project Target Selection

**Date**: 2026-04-12
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 1, 8
**Roadmap milestone(s)**: post-M10 follow-on helper and proof hardening
**Inherited Froth references**: `docs/archive/adr/044-project-system.md`

## Context

Frothy now has a real project workflow:

- `frothy new` scaffolds a project
- `frothy build` reads `froth.toml`
- optional platform toolchains are installed separately with `frothy setup`

Target selection now appears in three different contexts:

- `frothy new` creates a new manifest
- `frothy build` and `frothy flash` operate either on an existing manifest
  project or on a legacy non-project checkout
- optional platform toolchains such as ESP-IDF should still stay out of the
  default flow unless a project or legacy path explicitly asks for them

The real authority problem is not "never accept `--target`".
The real problem is letting `--target` compete with `froth.toml` after a
manifest project already exists.

## Options Considered

### Option A: Ban `--target` for all manifest-oriented flows

How it works:

- `frothy new` is always posix-first
- users must edit `[target]` manually for every non-posix project
- `frothy build` and `frothy flash` read the manifest only

Trade-offs:

- Pro: maximally strict authority boundary.
- Con: removes a useful scaffold convenience even though `new` is not yet
  building anything or resolving a toolchain.

### Option B: Keep manifest projects authoritative, but allow `frothy new --target` as scaffold-time sugar

How it works:

- `frothy new` defaults to the posix scaffold
- `frothy new --target <board>` may prefill `[target]` in the new manifest
- once a manifest project exists, `frothy build` and `frothy flash` reject
  `--target` and require the project target to come from `froth.toml`
- outside a manifest project, legacy build and flash paths may still use
  `--target`
- optional toolchains are resolved only when the chosen manifest or legacy path
  explicitly names that platform

Trade-offs:

- Pro: one clear authority for existing manifest projects.
- Pro: preserves a short scaffold command for hardware projects.
- Pro: keeps ESP-IDF opt-in instead of default.
- Con: keeps a small amount of board-to-platform inference at scaffold time.

## Decision

**Option B.**

For manifest-based projects, the `[target]` block in `froth.toml` is the
authority for project platform selection after scaffolding.

Policy:

- `frothy new` scaffolds the default posix project when `--target` is omitted
- `frothy new --target <board>` may prefill the new manifest with a non-posix
  target, but this is only scaffold-time sugar; it does not resolve or install
  any platform toolchain by itself
- optional platform toolchains are resolved only when the manifest explicitly
  names that platform
- once a manifest project exists, `frothy build` and `frothy flash` do not accept
  `--target`; users must edit `[target]` in `froth.toml` instead
- legacy non-project build and flash paths may continue to use target flags
  until they are retired, but that flag is not the authority for manifest
  projects

The deciding factor is clarity of authority:
project target choice should live in one explicit file once the project exists,
while scaffold-time convenience remains acceptable because it only initializes
that file.

## Consequences

- Frothy's default project scaffold stays platform-agnostic.
- ESP-IDF remains an optional follow-on dependency rather than an implied part
  of every new project.
- Proofs and tooling become easier to reason about because project platform
  selection is visible in `froth.toml`.
- Hardware-oriented scaffolds keep a short `new --target` path without creating
  a second authority for project builds.

## References

- `tools/cli/cmd/new.go`
- `tools/cli/cmd/build.go`
- `tools/cli/cmd/flash.go`
- `tools/frothy/proof_f1_control_smoke.sh`
