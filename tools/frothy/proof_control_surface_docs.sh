#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

rg -n '^Current milestone: `queued follow-on only`$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n '^Next artifact: first workshop install/editor/recovery hardening cut across CLI release, VSCode distribution, and reset-safe send$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n '^Next proof command: `sh tools/frothy/proof_control_surface_docs.sh`$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md

rg -n '^- Active milestone: `queued follow-on only`$' PROGRESS.md TIMELINE.md
rg -n '^- Next artifact: first workshop install/editor/recovery hardening cut across CLI release, VSCode distribution, and reset-safe send$' \
  PROGRESS.md TIMELINE.md
rg -n '^- Next proof command: `sh tools/frothy/proof_control_surface_docs.sh`$' \
  PROGRESS.md TIMELINE.md

rg -n 'Workshop install, editor, and recovery surface|Inspection-first teaching surface|Workshop base-image library and board surface|Readability language tranche|Records for workshop/game objects|Performance and persistence closeout on workshop programs|Workshop content and rehearsal|Later workspace/image-flow work|Workshop Gate For 2026-04-16' \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md
rg -n 'The queue below is intentionally movable' TIMELINE.md
rg -n 'For targeted work|Run the broader read pass when' AGENTS.md
rg -n 'Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md' \
  README.md \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md \
  PROGRESS.md \
  TIMELINE.md
