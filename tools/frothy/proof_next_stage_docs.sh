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

require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'Current milestone: `[~] Next-stage language definition`'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'spoken-ledger syntax tranche 1'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 635 \
  'here name is expr'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 635 \
  'call expr with ...'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'binding/place values on top of the frozen spoken-ledger tranche 1 baseline'
require_phrase_within "docs/roadmap/Frothy_Development_Roadmap_v0_1.md" 35 \
  'sh tools/frothy/proof_next_stage_docs.sh'

require_phrase_within "PROGRESS.md" 25 \
  'Active milestone: `[~] Next-stage language definition`'
require_phrase_within "PROGRESS.md" 35 'Spoken-ledger syntax tranche 1'
require_phrase_within "PROGRESS.md" 35 'here name is expr'
require_phrase_within "PROGRESS.md" 35 'call expr with ...'
require_phrase_within "PROGRESS.md" 25 \
  'binding/place values on top of'
require_phrase_within "PROGRESS.md" 25 \
  'sh tools/frothy/proof_next_stage_docs.sh'

require_phrase_within "TIMELINE.md" 25 \
  'Active milestone: `[~] Next-stage language definition`'
require_phrase_within "TIMELINE.md" 65 'Spoken-ledger syntax tranche 1'
require_phrase_within "TIMELINE.md" 65 'here name is expr'
require_phrase_within "TIMELINE.md" 65 'call expr with ...'
require_phrase_within "TIMELINE.md" 25 \
  'binding/place values on top of'
require_phrase_within "TIMELINE.md" 25 \
  'sh tools/frothy/proof_next_stage_docs.sh'

require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 60 \
  'spoken-ledger syntax tranche 1'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 60 \
  '- `here name is expr`'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 60 \
  '- `:` calls and `call expr with ...`'
require_phrase_within "docs/spec/Frothy_Language_Spec_vNext.md" 60 \
  'try/catch, and binding/place values remain draft.'

require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 60 \
  'spoken-ledger syntax tranche 1'
require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 60 \
  '- `here name is expr`'
require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 60 \
  '- `:` calls and `call expr with ...`'
require_phrase_within "docs/spec/Frothy_Surface_Syntax_Proposal_vNext.md" 60 \
  'try/catch, and binding/place values remain draft.'
