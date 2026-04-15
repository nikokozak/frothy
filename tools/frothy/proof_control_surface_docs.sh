#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

require_literal() {
  needle=$1
  shift
  rg -n -F "$needle" "$@"
}

require_literal_in_each_file() {
  needle=$1
  shift
  for path in "$@"; do
    rg -n -F "$needle" "$path"
  done
}

require_absent() {
  pattern=$1
  shift
  if rg -n "$pattern" "$@" >/dev/null; then
    echo "error: unexpected pattern '$pattern' found in control docs" >&2
    exit 1
  fi
}

queue_section=$(awk '
  $0 == "## Deferred Queue" { capture=1 }
  capture { print }
  $0 == "## Review Standard" { exit }
' docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md)

if [ -z "$queue_section" ]; then
  echo "error: could not read the deferred workspace/image-flow queue section" >&2
  exit 1
fi

boundary_record=$(awk '
  $0 == "<!-- workspace-image-flow-boundary-record:start -->" { capture=1; next }
  $0 == "<!-- workspace-image-flow-boundary-record:end -->" { exit }
  capture { print }
' docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md)

expected_boundary_record=$(cat <<'EOF'
boundary.slot_bundle=first
boundary.host_side=first
boundary.loader_first=no
boundary.helper_control_growth_first=no
boundary.manifest_growth_first=no
boundary.vscode_ui_growth_first=no
boundary.registry_daemon_pty_shared_owner_return=no
boundary.on_device_artifact_story=no
boundary.real_device_proof_before_signoff=yes
EOF
)

if [ "$boundary_record" != "$expected_boundary_record" ]; then
  echo "error: workspace/image-flow boundary record drifted" >&2
  printf 'expected:\n%s\n' "$expected_boundary_record" >&2
  printf 'actual:\n%s\n' "$boundary_record" >&2
  exit 1
fi

future_queue_block=$(printf '%s\n' "$queue_section" | awk '
  $0 == "Stable boundary record:" { capture=1 }
  capture && $0 == "## Review Standard" { exit }
  capture { print }
')

expected_future_queue_block=$(cat <<'EOF'
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
EOF
)

if [ "$future_queue_block" != "$expected_future_queue_block" ]; then
  echo "error: deferred future queue block drifted" >&2
  printf 'expected:\n%s\n' "$expected_future_queue_block" >&2
  printf 'actual:\n%s\n' "$future_queue_block" >&2
  exit 1
fi

rg -n '^Current milestone: `evaluator execution-stack hardening`$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n '^Next artifact:' docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n 'bounded frame arena' docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n 'non-recursive evaluator path' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n 'frothy_eval_tests && \./build/frothy_shell_tests && sh tools/frothy/proof_eval_stack_budget\.sh' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md

require_absent '^## Current Control Snapshot$' PROGRESS.md TIMELINE.md
rg -n '^## Remaining Gates$' PROGRESS.md
require_literal '`TIMELINE.md` for the live movable queue' PROGRESS.md
rg -n '^\- \[~\] Evaluator execution-stack hardening closeout$' TIMELINE.md
rg -n '^\- \[ \] Clean-machine validation on promised platforms$' TIMELINE.md
rg -n '^\- \[ \] Classroom hardware and recovery kit$' TIMELINE.md
rg -n '^\- \[~\] Workshop rehearsal plus measured performance/persistence closeout$' TIMELINE.md
rg -n '^\- \[ \] Deferred workspace/image-flow queue$' TIMELINE.md

rg -n '^## Why This Order$|^## Publishability Reset$|^## Workshop Gate For 2026-04-16$' \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md
rg -n 'bounded frame-arena|operational|Workspace_Image_Flow_Tranche_1|Frothy_Repo_Audit_2026-04' \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md
require_absent '^## Priority Stack$|^### [0-9]+\.' \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md
rg -n 'The queue below is intentionally movable' TIMELINE.md
rg -n 'For targeted work|Run the broader read pass when' AGENTS.md
rg -n 'Before sign-off on any task in this repo, run at least one proof on a real' \
  AGENTS.md
require_literal_in_each_file 'Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md' \
  README.md \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md \
  PROGRESS.md \
  TIMELINE.md
require_literal 'Deferred Queue Owner' \
  docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md
require_literal '## Deferred Queue' \
  docs/roadmap/Frothy_Workspace_Image_Flow_Tranche_1.md
require_literal_in_each_file 'Frothy_Workspace_Image_Flow_Tranche_1.md' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md \
  PROGRESS.md \
  TIMELINE.md
rg -n 'Workspace/image flow remains intentionally deferred' PROGRESS.md
rg -n '^\- \[ \] .*workspace/image-flow' TIMELINE.md

if rg -n 'Cut 2|Cut 3|host-only slot-bundle inspection/generation|later apply/load growth' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md \
  PROGRESS.md \
  TIMELINE.md >/dev/null; then
  echo "error: deferred workspace/image-flow staging leaked back into secondary control docs" >&2
  exit 1
fi

require_absent 'worktree' \
  PROGRESS.md \
  TIMELINE.md \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md
