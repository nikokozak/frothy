# Frothy Workshop Quick Reference

Use this during the maintained workshop path.
It covers only the promised attendee surface:

- released `frothy` CLI
- Frothy VS Code Marketplace install, with matching VSIX fallback
- preflashed `esp32-devkit-v4-game-board` proto board
- workshop repo [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop)

Start elsewhere when needed:

- install and attendee preflight:
  `Frothy_Workshop_Install_Quickstart.md`
- promised-platform validation and recording:
  `Frothy_Workshop_Clean_Machine_Validation.md`
- room-side hardware and recovery card:
  `../../boards/esp32-devkit-v4-game-board/WORKSHOP.md`

## First Connect

1. Plug in the preflashed `esp32-devkit-v4-game-board` proto board.
2. Run `frothy doctor`.
3. If several serial ports are visible, rerun `frothy --port <path> doctor`.
4. Confirm the board is already running the shipped Pong demo.
5. Open `pong.frothy` or another `.frothy` file in VS Code.
6. Run `Frothy: Connect Device`.
7. Run `Frothy: Send Selection / Form` on `1 + 1`.
8. Run `matrix.init:`, `grid.clear:`, `grid.set: 1, 1, true`, and `grid.show:`
   for the first visible matrix proof.
9. If the extension path is blocked, fall back to
   `frothy --port <path> connect`.

## Prompt Checks

Use these first when the room state is unclear:

```frothy
words
info @matrix.init
info @grid.clear
info @joy.up?
info @knob.left
show @matrix.init
```

What they tell you:

- `words`: which names are currently live
- `info @matrix.init` and `info @grid.clear`: the matrix workshop base image is installed
- `info @joy.up?` and `info @knob.left`: the semantic input helper layer is installed
- `show @matrix.init`: the baked-in board init path is currently bound

## Maintained Board Surface

Stable workshop helper surface:

- `matrix.*`: explicit hardware-facing display setup and drawing
- `grid.*`: workshop-facing display helpers
- `joy.*`: semantic active-low joystick readers
- `knob.*`: semantic analog helpers
- `dangerous.wipe`: base-image factory reset for the live overlay
- `demo.pong.*` plus `boot`: the shipped demo-board surface and the editable
  workshop example

Most-used board and base-image calls:

```frothy
matrix.init:
grid.clear:
grid.set: 1, 1, true
grid.rect: 2, 2, 4, 3, true
grid.fill: true
grid.fill: false
grid.show:
joy.up?:
joy.down?:
joy.left?:
joy.right?:
joy.click?:
knob.left:
knob.right:
```

Advanced/raw slots still exist for inspection and remapping:

- raw display pins: `TM1629_STB`, `TM1629_CLK`, `TM1629_DIO`
- raw joystick pins: `JOY_1`, `JOY_2`, `JOY_3`, `JOY_4`, `JOY_6`
- raw knob pins: `POT_LEFT`, `POT_RIGHT`
- semantic pin slots you can redefine live: `joy.up.pin`, `joy.down.pin`,
  `joy.left.pin`, `joy.right.pin`, `joy.click.pin`, `knob.left.pin`,
  `knob.right.pin`

## Persistence And Recovery

Use the image controls directly at the prompt:

```frothy
save
restore
dangerous.wipe
```

Remember:

- `save` snapshots the current overlay image
- `restore` replaces the live overlay with the saved one
- `dangerous.wipe` clears both the live overlay and the saved snapshot, then
  returns to the base image
- safe boot happens before snapshot restore and `boot:`; press `Ctrl-C` during
  `boot: CTRL-C for safe boot` if saved state is broken

## When Something Goes Wrong

- VS Code cannot find the CLI: set `frothy.cliPath` to the absolute `frothy`
  binary path. Legacy `froth` fallback remains available during the transition.
- Several USB serial devices are visible: use
  `frothy --port <path> doctor` and `frothy --port <path> connect`.
- The matrix stays dark: run `matrix.init:`, then `grid.clear:` and `grid.show:`.
- Whole-file send is blocked as unsafe: the firmware is too old for reset-safe
  send; reflash instead of replaying the file additively.
- The board boots into bad saved state: use safe boot, inspect `boot`, then
  run `dangerous.wipe`.
- The board does not start in Pong after a clean power cycle: treat that as a
  maintainer recovery issue, not an attendee setup issue.
- The extension is unstable on one laptop: keep the class moving with the CLI
  fallback, not ad hoc toolchain installs.
