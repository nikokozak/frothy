# Frothy Workshop Rehearsal Closeout

Status: branch-local status note
Date: 2026-04-14

This note is the workshop rehearsal closeout surface for the current branch.
It records the current focused workshop-board proof state on the maintained
`esp32-devkit-v4-game-board` path.

## Required Proof Command

```sh
sh tools/frothy/proof.sh workshop-v4 <PORT>
```

Add `--live-controls` only for an explicit manual joystick/button pass.

## Current Branch Status

- The docs/front-door, clean-machine checklist, room-side recovery card, and
  focused v4 workshop proof are checked in on this branch.
- The maintained classroom hardware path is now the attached
  `esp32-devkit-v4-game-board`, not the older v1 board.

## Measured Notes

- A successful focused real-device proof was recorded on 2026-04-15 against
  the mounted `esp32-devkit-v4-game-board` on `/dev/cu.usbserial-0001`.
- Command run: `sh tools/frothy/proof.sh workshop-v4 /dev/cu.usbserial-0001`
- Observed behavior matched the frozen workshop helper surface:
  `matrix.init`, `grid.clear`, `grid.fill`, `grid.set`, `grid.rect`,
  `knob.left/right`, idle `joy.*?`, and `dangerous.wipe` restoring
  `joy.*.pin` base slots.
- No timings are claimed here; this note records behavior, not performance.

## Remaining Manual Gate

- rerun the focused v4 proof if the workshop base image, helper surface, or
  board wiring changes before Friday 2026-04-17
- keep any future additions to this note limited to exact observed behavior
