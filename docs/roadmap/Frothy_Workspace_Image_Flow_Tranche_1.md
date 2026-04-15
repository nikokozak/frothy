# Frothy Workspace/Image-Flow Tranche 1

Status: doc-only closeout on 2026-04-13
Primary proof command: `sh tools/frothy/proof_control_surface_docs.sh`
Authority: `docs/spec/Frothy_Language_Spec_v0_1.md`, `docs/adr/101-stable-top-level-slot-model.md`, `docs/adr/105-canonical-ir-as-persisted-code-form.md`, `docs/adr/106-snapshot-format-and-overlay-walk-rules.md`, `docs/adr/110-single-owner-control-session-transport.md`, `docs/adr/111-vscode-extension-owned-control-session.md`, `docs/adr/113-manifest-owned-project-target-selection.md`, `docs/adr/114-next-stage-structural-surface-and-recovery-shape.md`, `docs/adr/115-first-workspace-image-flow-tranche.md`

## Purpose

This note defines the first truthful workspace/image flow tranche for Frothy
without rebuilding the old host stack or widening language/runtime semantics
carelessly.

The tranche is intentionally narrow:

- docs and ADR only
- slot bundle first
- source first
- host local

No code, protocol, helper, editor, manifest, or kernel surface widens here.

## Deferred Queue Owner

This note owns the deferred workspace/image-flow queue after tranche 1 closeout.

`PROGRESS.md`, `TIMELINE.md`, the workshop-prep note, and the main roadmap may
record placement and deferral, but they should point back here instead of
carrying separate Cut 2 / Cut 3 descriptions.

Other control docs should stay at deferral/reference level only.
Do not restate the staged queue or its held boundary outside this note.

Prefer the smallest useful implementation cut or design-tightening doc that
makes the next decision clearer.
If a resumed cut needs extra machinery to justify itself, stop and tighten the
design instead of widening the surface.
The deferred future-queue block below is proof-owned; keep its boundary record
and staged Cut 2 / Cut 3 bullets literal when editing.

## Tranche Boundary

The invariant held by this tranche is:

- no daemon
- no PTY layer
- no shared-owner broker
- no registry
- no module loader
- no package surface
- no helper or control-session command growth
- no manifest schema growth
- no new on-device image-loading request

The accepted result is design closure, not implementation breadth.

## First Artifact: Named Slot Bundle

The first useful artifact is a host-local slot bundle.

The slot bundle is not:

- a second namespace
- a module object
- an on-device image format
- an IR byte stream
- a registry package
- a background service contract

The slot bundle freezes only these future fields:

- bundle name
- entry file
- owned slot names
- owned dotted slot prefixes
- required base slot names
- required base dotted slot prefixes
- resolved file list
- runtime source payload

An on-disk path such as `.froth-build/runtime.frothy` may still exist as a
derived host convenience, but it is not part of the frozen bundle contract.

This keeps the first workspace/image flow artifact aligned with:

- stable top-level slots as public identity
- source-time module grouping over prefixed slots
- current project resolution through `froth.toml`
- current build outputs under `.froth-build/`
- current direct control-session replay behavior

## Apply Semantics

Live apply stays on the existing direct control path:

- `reset-to-base + replay bundle forms`

Here `RESET` means a live reset to the current base image.
It does not clear persisted overlay state.

`wipe()` remains the separate persisted-overlay clearing operation.
Slot bundles do not redefine `wipe()` as live apply.

Persistent seed/apply stays on the existing build/flash path:

- future bundle-oriented target: `reset-to-base + replay + save()`

Today the checked-in build/flash implementation reaches the same end state by
starting a fresh local runtime or freshly flashed firmware, replaying runtime
source, and then calling `save()`.
It does not yet expose a reusable reset-based bundle apply primitive.

Additive replay is not the blessed primary model for workspace/image flow.

One current authority drift is recorded here rather than fixed in this tranche:

- `docs/adr/111-vscode-extension-owned-control-session.md` keeps whole-file
  send conceptually at `reset + eval`
- `tools/cli/cmd/send.go` currently uses `wipe()` before replay

That drift remains implementation debt below the accepted design authority.

`IR capsule` stays deferred in tranche 1.
If it ever appears later, it should begin as a derived/cache artifact on top of
source-first slot bundles, not as the first public contract.

## Current Constraints

Editor and helper constraints:

- `tools/vscode/src/control-session-client.ts` keeps one helper child per VS
  Code window and one active control owner
- `tools/vscode/src/send-file.ts` resolves saved-on-disk source through
  `frothy tooling resolve-source`
- `tools/vscode/src/extension.ts` derives workspace/root context from the
  active folder and stays on direct helper-owned control
- `tools/vscode/src/send-file-reset.ts` preserves truthful whole-file
  `reset + eval` behavior

CLI and project constraints:

- `tools/cli/cmd/tooling.go` keeps the maintained helper surface at
  `resolve-source`, `control-session`, and `control-smoke`
- `tools/cli/internal/project/manifest.go` keeps `froth.toml` authoritative
  for project entry and target selection
- `tools/cli/internal/project/resolve.go` keeps resolution local, rooted, and
  source first
- `tools/cli/internal/project/runtime_forms.go` keeps replay ordered as
  top-level forms rather than as one opaque transaction
- `tools/cli/cmd/build.go`, `tools/cli/cmd/send.go`, and `tools/cli/cmd/flash.go`
  already own `.froth-build/resolved.froth`, `.froth-build/runtime.frothy`,
  whole-project replay, and today's fresh-start replay-plus-save build/flash
  path

Runtime constraints:

- `src/frothy_control.h` and `src/frothy_control.c` keep the direct request set
  explicit and small
- `src/frothy_base_image.c` and `src/frothy_snapshot.c` keep reset, save,
  restore, and wipe aligned with the base-plus-overlay image model
- ADR-101 keeps stable slot identity authoritative
- ADR-105 keeps canonical IR as internal persisted code truth
- ADR-106 keeps snapshot persistence shallow, slot owned, and overlay only

## Ownership And Collision Zones

This tranche directly updates:

- `docs/adr/115-first-workspace-image-flow-tranche.md`
- `docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md`
- current-state and ledger updates in
  `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`, `PROGRESS.md`, and
  `TIMELINE.md`

This tranche intentionally avoids collision zones:

- `README.md`
- executable-adjacent CLI naming notes
- `tools/vscode/src/*`
- `tools/cli/internal/frothycontrol/*`
- `tools/cli/internal/project/*`
- `tools/cli/cmd/send.go`
- `tools/cli/cmd/build.go`
- `tools/cli/cmd/flash.go`
- `src/frothy_control*`
- `src/frothy_snapshot*`

## Deferred Queue

Cut 1 is the only landed cut in tranche 1:

- docs and ADR only
- proof: `sh tools/frothy/proof_control_surface_docs.sh`

Deferred future queue, only if later reprioritized:

Stable boundary record:
<!-- workspace-image-flow-boundary-record:start -->
boundary.slot_bundle=first
boundary.host_side=first
boundary.loader_first=no
boundary.helper_control_growth_first=no
boundary.manifest_growth_first=no
boundary.vscode_ui_growth_first=no
boundary.registry_daemon_pty_shared_owner_return=no
boundary.on_device_artifact_story=no
boundary.real_device_proof_before_signoff=yes
<!-- workspace-image-flow-boundary-record:end -->
- Cut 2: host-only slot-bundle inspection or generation in the CLI project
  layer
- Cut 2 held boundary: respect the stable boundary record above; keep the
  work on top of resolved source plus existing derived `.froth-build` outputs
- Cut 2 host-side proof bar: `make test-cli && make test-cli-local && make test-integration`
- Cut 2 promotion gate: move past Cut 2 only if inspection/generation proves
  useful by making later workspace/image-flow work smaller or clearer
- Cut 2 promotion gate: if Cut 2 does not buy that clarity, stop and tighten
  the design docs instead of widening the implementation
- Cut 3: later apply/load growth only after Cut 2 proves useful
- Cut 3 held boundary: respect the stable boundary record above
- Cut 3 held boundary: any live apply composes with the existing helper/control
  path as `reset + replay`
- Cut 3 host-side proof bar: `make test-all && sh tools/frothy/proof_f1_control_smoke.sh --host-only`
- Cut 2/3 sign-off bar: host-side proof bar plus a real-device sanity proof on
  a connected ESP32-class target before sign-off

## Review Standard

The first workspace/image flow artifact is successful only if it makes later
implementation smaller and clearer.

If later work needs extra machinery to justify the artifact, the artifact is
too large for tranche 1.
