# Frothy ADR-100: Repo And Release Identity

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 1, 7, Appendix B
**Roadmap milestone(s)**: M1
**Inherited Froth references**: `docs/adr/059-version-spine.md`, `docs/adr/060-distribution-pipeline.md`

## Context

The new repo already contains inherited Froth docs, ADRs, release scripts, and
CI workflows. If left as-is, the repo can still present itself as Froth and can
still encourage implementers to follow Froth's old roadmap and language
direction.

Frothy needs a clean fork boundary before implementation deepens:

- permanent Frothy control docs must become authoritative
- release-facing assets must stop defaulting to Froth identity
- inherited Froth source and docs must remain available for substrate reuse
- internal command names and `froth_*` implementation symbols must remain
  stable during the first parallel-runtime phase

## Options Considered

### Option A: Rename everything to Frothy immediately

Flip repo docs, commands, binaries, symbols, and build targets at once.

Trade-offs:

- Pro: single visible identity.
- Con: large churn before the Frothy runtime exists.
- Con: higher risk of breaking inherited tooling during bootstrap.

### Option B: Split outward identity now, keep internal names transitional

Move repo policy and release-facing packaging to Frothy, but keep the inherited
 `froth` command names, project format, build targets, and implementation
 symbols until the parallel Frothy runtime is established.

Trade-offs:

- Pro: prevents accidental Froth publishing immediately.
- Pro: preserves working substrate and tooling during migration.
- Con: temporary dual naming that must be documented clearly.

### Option C: Leave release identity alone until runtime work starts

Only rewrite docs during bootstrap and defer release separation.

Trade-offs:

- Pro: smaller immediate diff.
- Con: still leaves an accidental publish path as Froth.
- Con: keeps repo authority and release identity mixed.

## Decision

**Option B.**

Frothy adopts a split bootstrap:

- root control docs, permanent spec, roadmap, and Frothy ADRs are authoritative
- release-facing asset names, repo slug defaults, formula names, and packaging
  defaults move to Frothy
- release publishing becomes manual during bootstrap
- local developer commands, runtime command names, `.froth` project format,
  build targets, and internal `froth_*` symbols stay unchanged for now

Inherited Froth docs and ADRs remain in the repo as reference material only.
They are not active Frothy policy unless a Frothy ADR explicitly adopts a
substrate behavior from them.

## Consequences

- Frothy can no longer publish by default under Froth branding.
- Implementers get a clear authority split before code reorganization begins.
- The repo temporarily carries Frothy release names and `froth` local commands.
- Later runtime and tooling work must decide when command and binary renaming
  becomes worth the churn.

## References

- `README.md`
- `docs/spec/README.md`
- `docs/adr/README.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
