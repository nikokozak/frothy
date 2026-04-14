# Frothy ADR-115: First Workspace/Image-Flow Tranche

**Date**: 2026-04-13
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 2.1, 7, 8, Appendix A.4, Appendix A.5, Appendix A.6
**Roadmap milestone(s)**: post-M10 follow-on workspace/image flow
**Inherited Froth references**: none

## Context

The direct control session, helper-owned editor path, manifest-owned project
flow, and checked-in `.froth-build/runtime.frothy` apply path are now stable
enough to support a first truthful workspace/image flow design pass.

The repo still does not have a justified bundle or capsule format.
The roadmap and timeline intentionally deferred workspace/image loading until
the helper and editor surfaces became smaller and clearer.

The design pressure is real:

- Frothy needs a small workspace/image flow story beyond raw whole-file replay
- any first artifact must preserve stable slot identity, canonical IR
  persistence rules, and the single-owner direct control session
- the first slice must not reopen daemon, PTY, shared-owner, registry, or
  loader questions

One existing drift also needs to be named instead of normalized:

- `docs/adr/111-vscode-extension-owned-control-session.md` treats whole-file
  send as conceptually `reset + eval`
- `tools/cli/cmd/send.go` currently uses `wipe()` before replay
- that difference is implementation debt below the accepted authority, not a
  reason to widen the first workspace/image flow tranche

## Options Considered

### Option A: Doc-only slot-bundle-first tranche

Freeze the first artifact in docs and ADRs only.
Make the first useful concept a host-local named slot bundle:
explicit owned stable slots or slot prefixes plus one resolved runtime source
payload.
Keep live apply semantics on top of the existing `RESET` plus replay path,
keep `wipe()` separate as persisted-overlay clearing, and defer any code,
protocol, or schema expansion.

Trade-offs:

- Pro: keeps the first slice truthful and reviewable
- Pro: matches stable slots, canonical IR, current `.froth-build/runtime.frothy`,
  and ADR-110 / ADR-111's direct control model
- Pro: avoids premature decisions about helper commands, manifest schema, or
  device loader contracts

### Option B: Add immediate CLI or helper implementation

Start with host-side bundle generation, inspection, or apply code immediately.

Trade-offs:

- Pro: visible tool progress sooner
- Con: forces format and ownership decisions before the design is closed
- Con: risks colliding with existing `froth.toml`, `resolve-source`, and
  `.froth-build` authority
- Con: increases helper, editor, and kernel surface area before the first
  artifact is even defined

### Option C: Make IR capsules the first public artifact

Define a new IR-first exchange format and prepare for load/apply over the
control session.

Trade-offs:

- Pro: closer to canonical persisted code
- Con: widens compatibility, tooling, and protocol questions too early
- Con: weak fit for source-first project resolution and owned slot prefixes
- Con: invites a new loader or cache contract before the host workflow is
  small and stable

## Decision

**Option A.**

Frothy's first workspace/image flow tranche is doc-only and slot-bundle-first.

The accepted first artifact is a host-local named slot bundle.

A slot bundle is:

- a bundle name
- one entry file
- explicit owned stable top-level slot names and/or owned dotted slot prefixes
- explicit required base slot names and/or required base dotted prefixes
- one resolved file list
- one resolved runtime source payload

Boundary rules:

- the slot bundle is not a module object, second namespace, registry package,
  daemon session, or IR byte stream
- a materialized host path such as `.froth-build/runtime.frothy` is a derived
  convenience, not part of the frozen bundle contract
- live apply is `reset-to-base + replay bundle forms` on the existing direct
  control session, where `RESET` means a live reset to the current base image
  and does not clear persisted overlay state
- `wipe()` remains the separate persisted-overlay clearing operation and is not
  the accepted live-apply primitive for slot bundles
- persistent seed/apply is the future bundle-oriented
  `reset-to-base + replay + save()` model for build/flash workflows only
- additive replay is not the blessed primary model
- `IR capsule` is deferred to a later derived/cache artifact only if
  source-first slot bundles prove insufficient
- tranche 1 does not add helper commands, editor UI, manifest keys, kernel
  load requests, daemon ownership, PTY passthrough, or shared-owner brokering

## Consequences

- Frothy gets a decision-complete first workspace/image flow boundary without
  widening `v0.1` semantics or the direct control protocol
- future host-side bundle work must compose with `froth.toml`,
  `project.Resolve`, `SplitTopLevelForms`, `.froth-build/resolved.froth`, and
  `.froth-build/runtime.frothy` instead of inventing a second authority
- today's checked-in build/flash paths do not yet issue a reusable
  reset-based bundle apply request; they start a fresh local runtime or fresh
  flashed firmware, replay runtime source, and then call `save()`
- future on-device work, if it arrives, must preserve stable slot identity and
  canonical IR as internal truth rather than treating bundle apply as a second
  persistence system
- the `reset + eval` versus `wipe()` send drift is recorded as existing debt
  and remains out of scope for tranche 1
- any move beyond a host-local slot bundle requires a later ADR before helper,
  editor, manifest, or kernel surfaces widen

## References

- `docs/adr/101-stable-top-level-slot-model.md`
- `docs/adr/105-canonical-ir-as-persisted-code-form.md`
- `docs/adr/106-snapshot-format-and-overlay-walk-rules.md`
- `docs/adr/110-single-owner-control-session-transport.md`
- `docs/adr/111-vscode-extension-owned-control-session.md`
- `docs/adr/113-manifest-owned-project-target-selection.md`
- `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`
- `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`
- `tools/cli/cmd/send.go`
