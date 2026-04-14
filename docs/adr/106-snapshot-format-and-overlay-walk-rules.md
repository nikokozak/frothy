# Frothy ADR-106: Snapshot Format And Overlay Walk Rules

**Date**: 2026-04-09
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 2.3, 7.1, 7.4, 7.5, 7.6, 7.7, 8.1, Appendix A.6
**Roadmap milestone(s)**: M2, M7
**Inherited Froth references**: `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md`, `src/froth_snapshot.c`

## Context

Persistence is central to Frothy's identity. The snapshot contract must remain
transparent, pointer-free, and small enough to explain. The runtime should
reuse the inherited A/B storage and CRC plumbing where practical, but the
persisted payload rules need to match Frothy's slot-and-IR model rather than
Froth's stack-visible language.

## Options Considered

### Option A: Persist the overlay image only with an explicit symbol-based walk

Store overlay slot bindings plus persistable objects and cells payload reachable
through those bindings. Rebuild the base image on boot and remap by symbol.

Trade-offs:

- Pro: preserves Frothy's base-plus-overlay model.
- Pro: keeps restore semantics explainable.
- Pro: stays compatible with safe recovery to base-only state.

### Option B: Persist the whole live runtime image

Serialize everything reachable in runtime state.

Trade-offs:

- Pro: seemingly direct.
- Con: captures execution state and non-persistable internals Frothy forbids.
- Con: harder to validate and version safely.

### Option C: Persist only source text for overlay definitions

Treat persistence as a replay log.

Trade-offs:

- Pro: smaller initial serializer.
- Con: weak fit for cells payload and canonical inspection.
- Con: parser behavior becomes a restore dependency.

## Decision

**Option A.**

Frothy snapshots persist the overlay image only.

Minimum persisted content:

1. header
2. symbol table
3. object table
4. top-level binding table
5. persistent cells payload

Rules:

- persist overlay top-level slots only
- persist code objects via canonical IR
- persist text objects
- persist cells descriptors and owned cells payload
- reject non-persistable runtime state explicitly
- remap top-level names by symbol identity on restore
- keep A/B storage and CRC validation
- if restore fails, remain in a usable base state
- `dangerous.wipe` clears both live and stored overlay state

## Consequences

- Persistence remains aligned with Frothy's live-image story rather than hidden
  runtime internals.
- Snapshot compatibility must be versioned explicitly.
- The serializer walk stays shallow and slot-owned in `v0.1`, which constrains
  later data-structure growth usefully.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`
- `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
