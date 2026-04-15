#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

rg -n '^## Start Here$' README.md
rg -n 'Frothy_Workshop_Quick_Reference\.md|Frothy_Workshop_Clean_Machine_Validation\.md|boards/esp32-devkit-v4-game-board/WORKSHOP\.md|Frothy_Workshop_Rehearsal_Closeout_2026-04-14\.md' \
  README.md
rg -n 'proof\.sh workshop-v4 <PORT>' README.md \
  docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md
rg -n 'Frothy_Workshop_Quick_Reference\.md|Frothy_Workshop_Clean_Machine_Validation\.md' \
  docs/guide/Frothy_Workshop_Install_Quickstart.md \
  tools/vscode/README.md
rg -n 'boards/esp32-devkit-v4-game-board/WORKSHOP\.md' tools/vscode/README.md
rg -n '^## First Connect$|^## Prompt Checks$|^## Maintained Board Surface$|^## Persistence And Recovery$|^## When Something Goes Wrong$' \
  docs/guide/Frothy_Workshop_Quick_Reference.md
rg -n 'frothy-v<version>-darwin-arm64\.tar\.gz|frothy-v<version>-darwin-amd64\.tar\.gz|frothy-v<version>-linux-amd64\.tar\.gz|frothy-vscode-v<extension-version>\.vsix|esp32-devkit-v4-game-board' \
  docs/guide/Frothy_Workshop_Clean_Machine_Validation.md
rg -n '^## Promised Matrix$|^## Release Assets To Stage$|^## Clean-Machine Procedure$|^## Recording Sheet$|^## Exit Rule$' \
  docs/guide/Frothy_Workshop_Clean_Machine_Validation.md
rg -n 'without a repo checkout and without `esp-idf`|Only promise the rows that actually passed|Do not widen the support matrix because a machine "probably" works\.' \
  docs/guide/Frothy_Workshop_Clean_Machine_Validation.md
rg -n 'froth --port <path> doctor|froth --port <path> connect|dangerous\.wipe|froth --port <path> flash|idf\.py -DFROTH_BOARD=esp32-devkit-v4-game-board' \
  boards/esp32-devkit-v4-game-board/WORKSHOP.md
rg -n 'proof\.sh workshop-v4 <PORT>' \
  boards/esp32-devkit-v4-game-board/WORKSHOP.md
rg -n '^## Minimum Room Pack-Out$|^## First-Line Recovery$|^## Reflash Paths$|^## Do Not Do In The Room$' \
  boards/esp32-devkit-v4-game-board/WORKSHOP.md
rg -n 'Frothy_Workshop_Quick_Reference\.md|Frothy_Workshop_Clean_Machine_Validation\.md|boards/esp32-devkit-v4-game-board/WORKSHOP\.md|Frothy_Workshop_Rehearsal_Closeout_2026-04-14\.md' \
  docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md
rg -n '^## Required Proof Command$|^## Current Branch Status$|^## Measured Notes$|^## Remaining Manual Gate$' \
  docs/roadmap/Frothy_Workshop_Rehearsal_Closeout_2026-04-14.md
rg -n '^- \[x\] Minimal docs front door and quick reference$' TIMELINE.md
rg -n 'Workshop rehearsal plus measured performance/persistence closeout' TIMELINE.md
