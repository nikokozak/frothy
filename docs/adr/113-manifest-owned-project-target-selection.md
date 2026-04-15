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

The real authority problem is not "never accept CLI selection flags".
The real problem is letting CLI selection compete with `froth.toml` after a
manifest project already exists, while also confusing platform choice with
board choice outside a project checkout.

## Options Considered

### Option A: Ban CLI selection flags for all manifest-oriented flows

How it works:

- `frothy new` is always posix-first
- users must edit `[target]` manually for every non-posix project
- `frothy build` and `frothy flash` read the manifest only

Trade-offs:

- Pro: maximally strict authority boundary.
- Con: removes a useful scaffold convenience even though `new` is not yet
  building anything or resolving a toolchain.

### Option B: Keep manifest projects authoritative, but separate `--target`
and `--board`

How it works:

- `frothy new` defaults to the posix scaffold
- `frothy new --board <board>` may prefill `[target]` in the new manifest
- once a manifest project exists, `frothy build` and `frothy flash` keep
  reading `froth.toml`, and any CLI `--target` / `--board` selection is
  ignored with an explicit note
- outside a manifest project, legacy build and flash paths may still use
  `--target <platform>` plus optional `--board <board>`
- optional toolchains are resolved only when the chosen manifest or legacy path
  explicitly names that platform

Trade-offs:

- Pro: one clear authority for existing manifest projects.
- Pro: restores an explicit board selector for legacy repo build and flash.
- Pro: preserves a short scaffold command for hardware projects.
- Pro: keeps ESP-IDF opt-in instead of default.
- Con: keeps a small amount of board-to-platform inference at scaffold time.

## Decision

**Option B.**

For manifest-based projects, the `[target]` block in `froth.toml` is the
authority for project platform selection after scaffolding.

Policy:

- `frothy new` scaffolds the default posix project when target and board are
  omitted
- `frothy new --board <board>` may prefill the new manifest with a non-posix
  target, but this is only scaffold-time sugar; it does not resolve or install
  any platform toolchain by itself
- optional platform toolchains are resolved only when the manifest explicitly
  names that platform
- once a manifest project exists, `frothy build` and `frothy flash` keep the
  manifest authoritative; CLI `--target` / `--board` selection prints an
  explicit override note and does not change the project build
- legacy non-project build and flash paths may continue to use
  `--target <platform>` and `--board <board>` until they are retired, but
  those flags are not the authority for manifest projects
- on the legacy repo-checkout path, explicit `--target` / `--board` selection
  must clean stale build directories before reconfiguring so sticky cache state
  cannot silently preserve the wrong board

The deciding factor is clarity of authority:
project target choice should live in one explicit file once the project exists,
while scaffold-time convenience and legacy checkout selection remain acceptable
so long as `target` still means platform, `board` still means board, and the
manifest remains the single authority once the project exists.

## Consequences

- Frothy's default project scaffold stays platform-agnostic.
- ESP-IDF remains an optional follow-on dependency rather than an implied part
  of every new project.
- Proofs and tooling become easier to reason about because project platform
  selection is visible in `froth.toml`.
- Legacy repo build and flash regain an explicit board selector instead of
  relying on sticky CMake cache state.
- Hardware-oriented scaffolds keep a short `new --board` path without creating
  a second authority for project builds.

## References

- `tools/cli/cmd/new.go`
- `tools/cli/cmd/build.go`
- `tools/cli/cmd/flash.go`
- `tools/frothy/proof_f1_control_smoke.sh`
