# ADR-059: Version Spine

**Date**: 2026-04-05
**Status**: Accepted
**Spec sections**: N/A (tooling and release workflow decision)

## Context

Froth's release version currently appears in multiple unrelated places:

- the root CMake build
- the ESP-IDF target build
- the Go CLI and its tests
- several Go tests

That drift is already real. The VS Code extension has its own release cadence, and its package version has already diverged from the Froth runtime version. We need one source of truth for the Froth version, a deterministic propagation path to the files that must embed that version, and a simple bump workflow that tags the resulting commit.

The constraints are:

- the source of truth must be easy to read from shell, CMake-adjacent tooling, and Go tests
- propagation must work on macOS and Linux without depending on GNU-specific `sed -i`
- the CLI's embedded SDK mirror must stay in sync with the propagated CMake files
- pre-1.0 versioning policy must be explicit so bump intent is not ambiguous

## Options Considered

### Option A: Keep version strings in each subsystem

Leave the version where it is today and update each copy by hand when needed.

Trade-offs:
- Pro: no new files or scripts.
- Con: drift is inevitable.
- Con: tests silently hardcode stale values.
- Con: release bumps remain manual and error-prone.

### Option B: Single `VERSION` file with propagation scripts

Store the Froth version once in a root `VERSION` file. Propagate it to the files that must embed the version string. Keep the CLI SDK mirror in sync by running `make sync-sdk` after propagation.

Trade-offs:
- Pro: one obvious source of truth.
- Pro: shell scripts, CMake-adjacent tooling, and Go tests can read the same value.
- Pro: bumping, committing, and tagging can be automated without adding heavier tooling.
- Con: propagation is an extra step when the version changes.
- Con: generated mirror files must be staged alongside the primary targets.

### Option C: Compute version dynamically from Git tags

Derive the version from Git state at build time instead of storing it directly.

Trade-offs:
- Pro: fewer hand-edited version files.
- Con: fragile in shallow clones, dirty worktrees, and environments without Git metadata.
- Con: harder to consume from tests and embedded build steps.
- Con: bigger change than the current problem requires.

## Decision

**Option B.**

Froth now uses a root `VERSION` file as the single source of truth for the Froth release version. That version is propagated into the runtime-bearing targets:

- `CMakeLists.txt`
- `targets/esp-idf/main/CMakeLists.txt`
- `tools/cli/cmd/version.go`

After propagation, the CLI SDK mirror is refreshed with `make sync-sdk` so the embedded kernel copy stays aligned.

The bump workflow is script-driven:

- update `VERSION`
- propagate
- stage the changed files
- commit `release: vX.Y.Z`
- create lightweight tag `vX.Y.Z`

The bump script does not push. Publishing remains an explicit user action.

## Consequences

- Froth has one source of truth for its release version.
- `make test` can detect version drift through a dedicated `version-check` step.
- Go tests that need the Froth version now read it from the authoritative source rather than duplicating literals.
- The VS Code extension version remains independent and is not part of the Froth version spine.
- The CLI still reads the embedded kernel version through `sdk.VersionFromFS`, so no new Go-side parsing mechanism is needed.
- Pre-1.0 semver policy is explicit: minor bumps (`0.x.0`) may include breaking changes; patch bumps (`0.x.y`) are for fixes that do not change the language surface.

## References

- `docs/superpowers/specs/2026-04-05-developer-workflow-phase2-design.md`
- ADR-058: developer surface and root Makefile workflow
- `tools/cli/internal/sdk/extract.go`
