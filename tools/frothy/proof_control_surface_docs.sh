#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

rg -n '^Current milestone: `evaluator execution-stack hardening`$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n 'Frothy ADR-118 plus the first evaluator-trampoline tranche for' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md
rg -n '^Next proof command: `cmake -S \. -B build && cmake --build build && \./build/frothy_eval_tests && sh tools/frothy/proof_eval_stack_budget\.sh`$' \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md

rg -n '^- Active milestone: `evaluator execution-stack hardening`$' PROGRESS.md TIMELINE.md
rg -n 'Frothy ADR-118 plus the first evaluator-trampoline tranche' \
  PROGRESS.md TIMELINE.md
rg -n '^- Next proof command: `cmake -S \. -B build && cmake --build build && \./build/frothy_eval_tests && sh tools/frothy/proof_eval_stack_budget\.sh`$' \
  PROGRESS.md TIMELINE.md

rg -n 'evaluator execution-stack hardening|Support matrix and release/install artifacts|Attendee-facing naming alignment|Attendee install email and quickstart|Workshop preflight and serial recovery path|Workshop starter project and frozen board/game surface|Minimal docs front door and quick reference|Clean-machine validation on promised platforms|Classroom hardware and recovery kit|Workshop rehearsal plus measured performance/persistence closeout|Later workspace/image-flow work|Workshop Gate For 2026-04-16' \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md
rg -n 'The queue below is intentionally movable' TIMELINE.md
rg -n '^- \[~\] Evaluator execution-stack hardening$' TIMELINE.md
rg -n 'For targeted work|Run the broader read pass when' AGENTS.md
rg -n 'Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md' \
  README.md \
  docs/roadmap/Frothy_Development_Roadmap_v0_1.md \
  PROGRESS.md \
  TIMELINE.md
