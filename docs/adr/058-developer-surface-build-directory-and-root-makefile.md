# ADR-058: Developer Surface, POSIX Build Directory, and Root Makefile Delegation

**Date**: 2026-04-05
**Status**: Accepted
**Spec sections**: N/A (tooling and workflow decision)

## Context

Froth now has two distinct build surfaces:

- the kernel repo itself, where contributors need a fast host-native edit-build-test loop
- the CLI project system, where user projects build into `.froth-build/` per ADR-044

The current root `Makefile` defaults to running all tests. That is slow for routine development and does not expose the available workflows clearly. At the same time, the legacy POSIX build directory is named `build64/` even though the default host build is 32-bit. That name is misleading and is hardcoded across tests and host tooling.

We need a stable developer-facing contract for the kernel repo without disturbing project builds or broadening the CLI Makefile into a second repo-orchestration layer.

## Options Considered

### Option A: Keep the current root Makefile and `build64/`

Leave the root `Makefile` test-first and preserve the existing directory name.

Trade-offs:
- Pro: zero churn.
- Con: the default workflow stays slow and undiscoverable.
- Con: `build64/` continues to misdescribe the actual POSIX build.
- Con: host tooling and tests keep carrying a misleading path.

### Option B: Make the root Makefile the developer surface and rename `build64/` to `build/`

Use the repo-root `Makefile` as the discoverable entry point for kernel contributors, make `help` the default target, rename the POSIX build directory to `build/`, and delegate only the CLI-owned workflows to `tools/cli/Makefile`.

Trade-offs:
- Pro: `make` becomes a useful entry point instead of an expensive surprise.
- Pro: the POSIX build directory name matches reality.
- Pro: kernel contributors get one stable root workflow surface.
- Pro: CLI-owned logic remains in the CLI Makefile.
- Con: several operational references need to be updated together.
- Con: local ignored directories such as `build64/` stop matching the new convention and require a rebuild.

### Option C: Move all workflow logic into the CLI and de-emphasize the root Makefile

Push most developer entry points into the Go CLI and keep the root `Makefile` minimal.

Trade-offs:
- Pro: one cross-platform tool could own everything eventually.
- Con: this is larger than the current problem.
- Con: it adds CLI dependency to simple kernel-repo workflows that should stay lightweight.
- Con: it blurs the line between kernel development and project builds.

## Decision

**Option B.**

The repo-root `Makefile` is the developer surface for this repository. It defaults to `help`, exposes the common kernel-repo workflows, and keeps the fast host-native path easy to discover.

The legacy POSIX build directory is renamed from `build64/` to `build/`. This applies only to the kernel repo's direct POSIX build path. It does **not** change `.froth-build/` project artifacts defined by ADR-044.

Delegation is intentionally narrow:

- `build-cli`, `test-cli`, and `sync-sdk` delegate to `tools/cli/Makefile`
- `clean-cli` and `test-integration` stay owned by the root `Makefile`

This keeps CLI-specific logic in the CLI Makefile without forcing every repo-level concern through it.

## Consequences

- `make` and `make help` now describe the available root workflows instead of running the full test suite by default.
- The kernel repo's POSIX binary path becomes `build/Frothy`.
- Tests, local runtime discovery, and legacy host build paths must use `build/` consistently.
- Historical documents that mention `build64/` as a past state can remain unchanged.
- Project builds under `.froth-build/` are explicitly unaffected.
- The root Makefile owns the repo-local Go cache policy for its test workflows and passes that cache through when delegating CLI tests.

## References

- ADR-003: Build system choice (CMake for host builds)
- ADR-029: Build targets and toolchain management
- ADR-039: Host tooling UX and daemon lifecycle
- ADR-042: Extension UX and local target
- ADR-044: Project system and `.froth-build/` artifacts
- `docs/superpowers/specs/2026-04-05-developer-workflow-phase1-design.md`
