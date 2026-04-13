# ADR-060: Distribution Pipeline

**Date**: 2026-04-06
**Status**: Accepted
**Spec sections**: N/A (tooling, release, and distribution workflow decision)

## Context

Froth now has a stable CLI, embedded SDK extraction, project workflow, and version spine, but there is still no complete distribution pipeline. A workshop attendee should be able to install the CLI with Homebrew, connect to pre-flashed hardware immediately, and flash stock ESP32 firmware without installing ESP-IDF. At the same time, kernel developers working from a repo checkout must not lose the local source-build and local flash workflow established by ADR-044.

The constraints are:

- releases must be driven by a tagged version that matches the propagated version spine
- the CLI artifact set must stay small and predictable
- Homebrew tap updates must be automated but idempotent
- ESP32 prebuilt flashing must verify downloads before use
- macOS users installed via Homebrew may have `gmake` but not `make`

## Options Considered

### Option A: Keep releases manual and source-build-only

Ship no binary artifacts, no firmware zip, and no Homebrew automation. Users build everything from a clone.

Trade-offs:
- Pro: minimal new automation.
- Con: fails the workshop/newcomer path.
- Con: distribution depends on undocumented local build knowledge.
- Con: no stable release artifact set for users or taps.

### Option B: Tag-driven release pipeline with Homebrew tap and prebuilt firmware

Use a GitHub Actions pipeline triggered by `v*` tag pushes. Build three CLI tarballs from one Linux runner, build one ESP32 firmware zip, publish a GitHub Release, update the Homebrew tap, and let the CLI download verified prebuilt firmware when outside a project or repo checkout.

Trade-offs:
- Pro: supports the workshop install path and a stock firmware flash path.
- Pro: keeps release inputs explicit through the version spine.
- Pro: preserves repo-checkout workflows for kernel developers.
- Con: adds CI and tap maintenance machinery.
- Con: requires careful checksum and cache validation to stay safe.

### Option C: Split CLI and firmware distribution into separate systems

Publish CLI artifacts with GitHub Releases but keep firmware downloads or Homebrew updates manual.

Trade-offs:
- Pro: simpler than full automation.
- Con: leaves the most failure-prone onboarding step manual.
- Con: creates mismatched release surfaces and more operator burden.

## Decision

**Option B.**

Froth now uses a tag-driven distribution pipeline:

- push `vX.Y.Z` after the version spine has been propagated
- CI runs `make test` on pushes to `main` and pull requests to `main`
- the release workflow verifies `GITHUB_REF_NAME` matches `VERSION` and runs `make version-check`
- the CLI is cross-compiled on Linux for `darwin/arm64`, `darwin/amd64`, and `linux/amd64`
- release tarballs contain a single top-level `froth` binary
- ESP32 firmware is built once in CI and shipped as `froth-vX.Y.Z-esp32-devkit-v1.zip` with the directory structure from `flasher_args.json` preserved
- one release-wide checksums file includes both CLI tarballs and the firmware zip
- the Homebrew tap is updated automatically from the release job
- the CLI flashes prebuilt firmware only when outside a project and outside a local kernel checkout; project and checkout source-build workflows remain intact
- CLI local-build and doctor checks resolve `make` first, then `gmake`

## Consequences

- `brew install nikokozak/froth/froth` becomes the canonical newcomer install path.
- `froth flash` can flash stock ESP32 firmware without ESP-IDF by downloading and verifying release artifacts.
- kernel developers still retain source-build flashing from a local checkout.
- Homebrew automation remains safe to re-run because formula updates are deterministic and no-op commits are skipped.
- macOS release binaries target Monterey 12.0+ and are not described as fully static; Linux release binaries stay CGO-free and static in practice.
- Release correctness now depends on the version spine and checksum generation being part of the workflow, not on manual operator discipline.

## References

- `docs/superpowers/specs/2026-04-05-developer-workflow-phase3-design.md`
- ADR-044: project system and CLI architecture
- ADR-058: developer surface and root Makefile workflow
- ADR-059: version spine
- `tools/package-release.sh`
- `.github/workflows/ci.yml`
- `.github/workflows/release.yml`
