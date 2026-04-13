# ADR-041: Strict Bare Identifiers

**Date**: 2026-03-17
**Status**: Accepted
**Spec sections**: Froth_Language_Spec_v1_1 (identifier resolution, slot creation)

## Context

The evaluator uses `resolve_or_create_slot` for all identifier resolution. This function creates a new slot (with heap-allocated name) if the name doesn't exist yet. At top level, a bare identifier like `bar` creates a slot, then tries to execute it, then fails with "undefined word." The slot persists.

This causes three problems:

1. **Typos pollute the slot table.** Every mistyped word creates a permanent slot entry and consumes heap for the name string. On constrained targets, this is wasted memory that can't be reclaimed without `reset` or `wipe`.

2. **Ghost slots break reset/restore.** After `dangerous-reset`, the heap pointer rolls back to the watermark, but ghost slots created by typos retain dangling name pointers into reclaimed heap space. A subsequent `restore` finds the ghost via the dangling pointer (`strcmp` matches because the bytes haven't been overwritten yet), skips creating a fresh slot, then heap allocations overwrite the name bytes. The restored word becomes unfindable. This is the proximate bug that surfaced this ADR.

3. **Non-idempotent errors.** A user mistake (typing an undefined name) should produce an error and leave the system unchanged. Today it mutates the slot table and heap. Running the same typo twice has different internal effects than running it once (second time finds the existing empty slot instead of creating a new one). User-visible behavior happens to be the same (both error), but the internal state diverges.

Slot creation is legitimately needed in three other contexts:

- **Inside quotation bodies** (`[ bar ]`): forward references. The word might be defined after the quotation is built. The slot must exist so the CALL tag can hold a valid index.
- **Tick-identifiers** (`'bar`): explicit intent to name a slot. Used in `'bar [ ... ] def`.
- **Colon sugar** (`: bar ... ;`): definition, which goes through the tick path internally.

## Options Considered

### Option A: Strict top-level resolution

Change `froth_evaluator_handle_identifier` to use `froth_slot_find_name` directly. If the name doesn't exist, return `FROTH_ERROR_UNDEFINED_WORD` without creating a slot or allocating heap. All other call sites (`resolve_or_create_slot` inside quotation builder, tick-identifier handler, colon sugar) remain unchanged.

Trade-offs:
- Pro: one-line change. Zero risk to forward references or definition paths.
- Pro: eliminates ghost slot problem entirely.
- Pro: errors become side-effect-free at top level.
- Con: none identified. Top-level bare execution of an undefined name has no legitimate use case.

### Option B: Lazy slot creation (defer heap allocation)

Create slot table entries without allocating a name on the heap. Store names inline in a fixed-size field on the slot struct. Avoids heap pollution but still creates slot entries.

Trade-offs:
- Pro: forward references work everywhere without heap cost.
- Con: slot table entries still leak on typos.
- Con: major struct change, increases slot table memory footprint.
- Con: doesn't fix the ghost slot problem (slot entry still exists after reset).

### Option C: Mark all post-boot slots as overlay

Have `froth_slot_create` set `overlay=1` when `boot_complete` is true, regardless of whether `def` runs. Ghost slots would then be cleaned by `froth_slot_reset_overlay`.

Trade-offs:
- Pro: fixes the ghost slot bug.
- Con: doesn't fix the slot table pollution problem. Typos still create slots (they just get cleaned on reset).
- Con: masks the real issue. The invariant "user mistakes don't mutate state" is still violated.

## Decision

**Option A.** Top-level bare identifier resolution must not create slots.

The change is in `froth_evaluator_handle_identifier` only. Replace `resolve_or_create_slot` with `froth_slot_find_name`. If the name is not found, return `FROTH_ERROR_UNDEFINED_WORD`. The slot table and heap are untouched.

All other identifier resolution paths (quotation builder, tick-identifier, colon sugar) continue using `resolve_or_create_slot` because they have legitimate reasons to create slots.

## Consequences

- Typos at the REPL no longer pollute the slot table or consume heap.
- The ghost slot / dangling pointer bug is eliminated. `restore` after `reset` works correctly regardless of what the user typed between the two.
- Forward references inside quotations still work. `[ foo ] 'foo [ 42 ] def` remains valid.
- No user-visible behavior change: the error message for undefined words is identical.
- The "strict bare identifiers" open question in PROGRESS.md is resolved.

## References

- PROGRESS.md "Open Questions": strict bare identifiers audit finding
- ADR-037: `reset` primitive (exposed the ghost slot bug)
- `src/froth_evaluator.c`: `resolve_or_create_slot`, `froth_evaluator_handle_identifier`
