# Spec Authority

The active Frothy core semantic spec is:

- `Frothy_Language_Spec_v0_1.md`

This file is normative for the stable Frothy core:

- language semantics
- image behavior
- interactive profile
- persistence contract
- FFI boundary expectations

That accepted `v0.1` spec is the semantic floor.
It is not, by itself, the whole present-day ceiling of the user-facing Frothy
surface.

Accepted post-`v0.1` ADRs and their referenced roadmap notes may widen the
maintained base-image/tooling surface so long as they preserve the core model.
Current examples:

- `docs/adr/121-workshop-base-image-board-library-surface.md`
- `docs/adr/123-post-v0_1-embedded-tool-surface.md`
- `docs/roadmap/Frothy_Embedded_Tool_Surface_Tranche_1.md`

Non-normative next-stage design work may also live beside the active spec.
Current examples:

- `Frothy_Language_Spec_vNext.md`
- `Frothy_Surface_Syntax_Proposal_vNext.md`

Those drafts remain exploratory and implementation-oriented.
They do not override the accepted core contract unless and until a later
accepted spec or ADR says so.

Retained Froth specs in this directory remain in-tree for substrate and
historical reference only:

- `Froth_Interactive_Development_v0_5.md`
- `Froth_Snapshot_Overlay_Spec_v0_5.md`

Archived Froth language specs live under `docs/archive/spec/`.

Those Froth specs do not define active Frothy semantics unless a Frothy ADR
explicitly adopts a substrate behavior from them.
