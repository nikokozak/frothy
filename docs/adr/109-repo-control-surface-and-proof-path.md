# Frothy ADR-109: Repo Control Surface And Proof Path

**Date**: 2026-04-12
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 1, 8, Appendix B
**Roadmap milestone(s)**: post-M10 control-surface cleanup
**Inherited Froth references**: `docs/adr/058-developer-surface-build-directory-and-root-makefile.md`, `docs/adr/059-version-spine.md`

## Context

The Frothy implementation is functionally near-complete, but the repo surface
still reads like bootstrap scaffolding:

- `README.md` still frames Frothy as experimental
- root `make run` still shares build/test language with inherited Froth
- root `make test` is still a monolithic serial lane instead of a predictable
  local gate
- inherited runtime and shell-test residue still occupies the maintained repo
  surface
- the CLI SDK payload is generated for embedding, but the repo surface still
  suggests a maintained mirror and multiple competing Frothy proof entrypoints
- the maintained test story is implicit enough that hidden prerequisites and
  slow suites surprise local runs

That drift makes the repo harder to navigate and weakens the proof story.

## Options Considered

### Option A: Keep the current mixed control surface

Leave the repo root Froth-first until a later rename pass.

Trade-offs:

- Pro: less immediate churn.
- Con: the default developer path does not match the actual Frothy product.
- Con: Frothy proofs stay partially hidden behind legacy shell wiring.

### Option B: Rename everything to Frothy immediately

Flip the repo root, CLI names, runtime names, and every transitional surface at
once.

Trade-offs:

- Pro: single visible identity.
- Con: larger churn than the accepted roadmap requires.
- Con: breaks transitional tooling and inherited substrate expectations.

### Option C: Make the repo root Frothy-first, archive legacy surfaces, and keep only the direct-control CLI path

Use the root control surface to point at Frothy, remove the legacy runtime and
daemon paths from the maintained build/test surface, and archive historical
Froth docs out of the active tree.

Trade-offs:

- Pro: the default path matches the active product.
- Pro: the active tree no longer advertises unmaintained legacy buckets.
- Pro: proof coverage becomes obvious and reviewable.
- Con: some archived historical references move out of their original paths.

## Decision

**Option C.**

Frothy adopts a Frothy-first repo control surface:

- `README.md` describes Frothy `v0.1` and the active follow-on queue, not
  bootstrap scaffolding
- root `make run` launches `build/Frothy`
- root `make test` is the fast self-contained local gate
- root `make test-all` is the exhaustive local gate
- root `make test-frothy` runs Frothy host `ctest` coverage plus the single
  `tools/frothy/proof.sh` host proof path
- root `make test-cli`, `make test-cli-local`, and `make test-integration`
  expose the maintained CLI lanes directly
- historical Froth docs move under `docs/archive/`
- required PR CI runs the heavy lanes as separate parallel jobs rather than
  hiding them behind one serial `make test`
- the repo root remains the only maintained kernel source tree
- CLI embedding uses a generated archive payload rather than an in-tree source
  mirror
- root version checks validate only the maintained source tree, not daemon-era
  artifacts

## Consequences

- Contributors get one obvious default path for Frothy host work and Frothy
  proofs.
- Legacy Froth substrate remains available only where Frothy still links it.
- The maintained test contract is short, explicit, and aligned with CI.
- The CLI no longer depends on daemon routing or the legacy host runtime.

## References

- `README.md`
- `docs/archive/`
- `Makefile`
- `tools/frothy/proof.sh`
- `tools/cli/Makefile`
