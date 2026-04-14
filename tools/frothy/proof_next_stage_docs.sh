#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"

require_phrase_within() {
  file=$1
  max_line=$2
  phrase=$3
  if ! awk -v max_line="$max_line" -v phrase="$phrase" '
    NR <= max_line && index($0, phrase) {
      print NR ":" $0;
      found = 1;
    }
    END {
      exit found ? 0 : 1;
    }
  ' "$ROOT_DIR/$file"; then
    echo "error: expected '$phrase' in first $max_line lines of $file" >&2
    exit 1
  fi
}

require_phrase_after_anchor() {
  file=$1
  anchor=$2
  max_delta=$3
  phrase=$4
  if ! awk -v anchor="$anchor" -v max_delta="$max_delta" -v phrase="$phrase" '
    index($0, anchor) && anchor_line == 0 {
      anchor_line = NR;
    }
    anchor_line != 0 && NR >= anchor_line && NR <= anchor_line + max_delta && index($0, phrase) {
      print NR ":" $0;
      found = 1;
    }
    END {
      exit found ? 0 : 1;
    }
  ' "$ROOT_DIR/$file"; then
    echo "error: expected '$phrase' within $max_delta lines after '$anchor' in $file" >&2
    exit 1
  fi
}

reject_phrase() {
  file=$1
  phrase=$2
  if awk -v phrase="$phrase" '
    index($0, phrase) {
      print NR ":" $0;
      found = 1;
    }
    END {
      exit found ? 0 : 1;
    }
  ' "$ROOT_DIR/$file"; then
    echo "error: unexpected '$phrase' in $file" >&2
    exit 1
  fi
}

require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'Current milestone: `none`'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'next-stage language-definition docs landed as a doc-only closeout, control-session slice 2 and bounded-memory hardening landed, and CLI naming alignment stays next'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'CLI naming-alignment artifact'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'Blocked by: none'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  "rg -n 'repo-local \\`froth-cli\\`' README.md"
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'keep transitional `froth-cli` / release-time `froth` naming explicit in docs and tooling notes before touching binaries or release surfaces'
require_phrase_after_anchor "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" \
  '#### Next-stage language definition' 55 \
  'landed on 2026-04-13 as a doc-only closeout'
require_phrase_after_anchor "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" \
  '#### Next-stage language definition' 55 \
  'spoken-ledger syntax'
require_phrase_after_anchor "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" \
  '#### Next-stage language definition' 55 \
  'Frothy ADR-114 records the chosen remaining draft shape'
require_phrase_after_anchor "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" \
  '#### CLI naming alignment' 20 \
  'queued after the 2026-04-13 next-stage language-definition closeout'
reject_phrase "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" \
  'land the next-stage language-definition docs for counted iteration,'
reject_phrase "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" \
  'rg -n '\''froth-cli|release-time `froth`|intended global `frothy`'\'' README.md PROGRESS.md TIMELINE.md docs/roadmap/Frothy_Development_Roadmap_v0_1.md'

require_phrase_within "PROGRESS.md" 25 \
  'Active milestone: `none`'
require_phrase_within "PROGRESS.md" 25 \
  'Blocked by: none'
require_phrase_within "PROGRESS.md" 25 \
  'CLI naming-alignment artifact'
require_phrase_within "PROGRESS.md" 25 \
  "rg -n 'repo-local \\`froth-cli\\`' README.md"
require_phrase_within "PROGRESS.md" 25 \
  'Next proof:'
require_phrase_within "PROGRESS.md" 25 \
  'executable-adjacent tool surfaces'
require_phrase_within "PROGRESS.md" 35 \
  'Next-stage language definition is closed on 2026-04-13'
require_phrase_after_anchor "PROGRESS.md" \
  '## Next Proof' 3 \
  "rg -n 'repo-local \\`froth-cli\\`' README.md"
require_phrase_after_anchor "PROGRESS.md" \
  '## Next Artifact' 3 \
  'CLI naming-alignment artifact'
reject_phrase "PROGRESS.md" \
  'this branch does not own `README.md`'

require_phrase_within "TIMELINE.md" 25 \
  'Active milestone: `none`'
require_phrase_within "TIMELINE.md" 25 \
  'Blocked by: none'
require_phrase_within "TIMELINE.md" 25 \
  'CLI naming-alignment artifact'
require_phrase_within "TIMELINE.md" 25 \
  'Next proof command:'
require_phrase_within "TIMELINE.md" 25 \
  "rg -n 'repo-local \\`froth-cli\\`' README.md"
require_phrase_within "TIMELINE.md" 25 \
  'executable-adjacent tool surfaces'
require_phrase_within "TIMELINE.md" 80 \
  'Next-stage language definition`: landed on 2026-04-13 as a doc-only'
require_phrase_within "TIMELINE.md" 45 \
  'Operational label: `queued follow-on only`'
reject_phrase "TIMELINE.md" \
  'this branch does not own `README.md`'

require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 60 \
  'spoken-ledger syntax tranche 1'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 60 \
  '- `here name is expr`'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 60 \
  '- `:` calls and `call expr with ...`'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 60 \
  'try/catch, and binding/place designators remain draft.'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 240 \
  'Binding/place work is narrowed to stable top-level slot designators only.'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 360 \
  '## 7. Staging'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 360 \
  '## 8. Summary'

require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 60 \
  'spoken-ledger syntax tranche 1'
require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 60 \
  '- `here name is expr`'
require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 60 \
  '- `:` calls and `call expr with ...`'
require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 60 \
  'try/catch, and binding/place designators remain draft.'
require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 80 \
  '`in prefix [ ... ]` is not part of this frozen baseline.'
require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 310 \
  'ordinary-code `@name` is restricted to stable top-level slot designators'
require_phrase_within "docs/spec/README.md" 20 \
  '`Frothy_Language_Spec_vNext.md`'
require_phrase_within "docs/spec/README.md" 20 \
  '`Frothy_Surface_Syntax_Proposal_vNext.md`'
require_phrase_within "docs/adr/114-next-stage-structural-surface-and-recovery-shape.md" 20 \
  'Frothy ADR-114: Next-Stage Structural Surface And Recovery Shape'
require_phrase_within "docs/adr/114-next-stage-structural-surface-and-recovery-shape.md" 20 \
  'sections 3, 4, 6, 7, 8'
