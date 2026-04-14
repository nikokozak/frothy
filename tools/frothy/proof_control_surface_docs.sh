#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

rg -n '^Current milestone: `queued follow-on only`$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n '^Next artifact: attendee install, naming-alignment, and preflight hardening cut across CLI/VSCode release artifacts, serial readiness, and workshop starter materials$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n '^Next proof command: `sh tools/frothy/proof_control_surface_docs.sh`$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md

rg -n '^- Active milestone: `queued follow-on only`$' PROGRESS.md TIMELINE.md
rg -n '^- Next artifact: attendee install, naming-alignment, and preflight hardening cut across CLI/VSCode release artifacts, serial readiness, and workshop starter materials$' \
  PROGRESS.md TIMELINE.md
rg -n '^- Next proof command: `sh tools/frothy/proof_control_surface_docs.sh`$' \
  PROGRESS.md TIMELINE.md

rg -n 'Support matrix and release/install artifacts|Attendee-facing naming alignment|Attendee install email and quickstart|Workshop preflight and serial recovery path|Workshop starter project and frozen board/game surface|Minimal docs front door and quick reference|Clean-machine validation on promised platforms|Classroom hardware and recovery kit|Workshop rehearsal plus measured performance/persistence closeout|Later workspace/image-flow work|Workshop Gate For 2026-04-16' \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md
rg -n 'The queue below is intentionally movable' TIMELINE.md
rg -n 'For targeted work|Run the broader read pass when' AGENTS.md
rg -n 'Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md' \
  README.md \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md \
  PROGRESS.md \
  TIMELINE.md
