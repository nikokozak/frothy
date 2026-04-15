#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
NEXT_ARTIFACT='first host-only slot-bundle inspection/generation artifact in the CLI project layer'
NEXT_PROOF='make test-all && sh tools/frothy/proof_next_stage_docs.sh'

require_rg() {
  pattern=$1
  shift
  rg -n "$pattern" "$@"
}

reject_rg() {
  pattern=$1
  shift
  if rg -n "$pattern" "$@"; then
    echo "error: unexpected match for $pattern" >&2
    exit 1
  fi
}

require_rg '^Current milestone: `none`$' \
  "$ROOT_DIR/docs/roadmap/Frothy_Development_Roadmap_v0_1.md"
require_rg '^Blocked by: none$' \
  "$ROOT_DIR/docs/roadmap/Frothy_Development_Roadmap_v0_1.md"
require_rg "^Next artifact: $NEXT_ARTIFACT$" \
  "$ROOT_DIR/docs/roadmap/Frothy_Development_Roadmap_v0_1.md"
require_rg "^Next proof command: \`$NEXT_PROOF\`$" \
  "$ROOT_DIR/docs/roadmap/Frothy_Development_Roadmap_v0_1.md"

require_rg "^- Active milestone: \`none\`$" \
  "$ROOT_DIR/PROGRESS.md" "$ROOT_DIR/TIMELINE.md"
require_rg "^- Blocked by: none$" \
  "$ROOT_DIR/PROGRESS.md" "$ROOT_DIR/TIMELINE.md"
require_rg "^- Next artifact: $NEXT_ARTIFACT$" \
  "$ROOT_DIR/PROGRESS.md" "$ROOT_DIR/TIMELINE.md"
require_rg "^- Next proof(:| command:)? \`$NEXT_PROOF\`$" \
  "$ROOT_DIR/PROGRESS.md" "$ROOT_DIR/TIMELINE.md"

require_rg 'repo-local `frothy-cli`' "$ROOT_DIR/README.md"
require_rg 'Installed CLI command from released assets \| `frothy`' "$ROOT_DIR/README.md"
reject_rg '`F1 Core hardening`' "$ROOT_DIR/README.md"
