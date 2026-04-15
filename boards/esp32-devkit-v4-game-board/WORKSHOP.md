# ESP32 DevKit V4 Game Board Workshop Kit

Status: room-side checklist and recovery card
Date: 2026-04-15

This is the maintained classroom hardware path for the Friday 2026-04-17
workshop run.
It is the board surface the workshop helper docs now describe.

## Board Facts

- board: `esp32-devkit-v4-game-board`
- carrier: ESP32 DevKit V4
- platform: `esp-idf`
- display pins: `TM1629_STB=18`, `TM1629_CLK=19`, `TM1629_DIO=23`
- input pins:
  `POT_LEFT=33`, `POT_RIGHT=32`, `JOY_1=13`, `JOY_2=25`, `JOY_3=16`,
  `JOY_4=17`, `JOY_6=14`
- raw pad note: `A0` and `BUTTON_1` both name GPIO34 on this mounted board;
  button helpers stay out of the current workshop surface
- frozen semantic joystick map:
  `joy.left.pin=JOY_1`, `joy.click.pin=JOY_2`, `joy.down.pin=JOY_3`,
  `joy.up.pin=JOY_4`, `joy.right.pin=JOY_6`
- board/base-image surface:
  `millis`, `gpio.*`, `adc.read`, `adc.percent`, `blink`, `animate`,
  `led.*`, `tm1629.raw.*`, `tm1629.*`, `matrix.*`, `grid.*`, `joy.*`, and
  `knob.*`

## Minimum Room Pack-Out

- one preflashed `esp32-devkit-v4-game-board` proto board per seat
- one known-good USB data cable per board
- at least one spare preflashed proto board
- at least one spare known-good USB data cable
- labels that tie each board and cable back to the room inventory
- one maintainer machine that already has the matching CLI, VSIX, `esptool`,
  and `esp-idf`
- one maintainer checkout of the current `main` branch with the v4 board
  target already proved locally

This checklist is not complete until someone physically packs and labels the
kit.

## Focused Proof

Run this against the attached workshop proto board to prove the baked-in v4
helper surface directly, including live joystick transitions:

```sh
sh tools/frothy/proof.sh workshop-v4 --live-controls <PORT>
```

## First-Line Recovery

1. Run `froth --port <path> doctor`.
2. Run `froth --port <path> connect` and evaluate `1 + 1`.
3. If saved state is broken, power-cycle or reset the board, press `Ctrl-C`
   during the safe-boot window, then run `dangerous.wipe`.
4. After recovery, confirm `info @matrix.init`, `info @grid.clear`,
   `info @joy.up?`, and `1 + 1` at the prompt before handing the board back.
5. If the display is dark, run `matrix.init:`, `grid.clear:`, and `grid.show:`
   before assuming the board is bad.
6. If the VS Code path is blocked, keep using the CLI path instead of changing
   toolchains on the attendee machine.

## Reflash Paths

Fastest project-based recovery from a maintainer checkout:

```sh
froth new --target esp32-devkit-v4-game-board recover-board
cd recover-board
froth --port <path> doctor
froth --port <path> flash
```

Direct maintainer reflash from this repo when you need the current checked-in
base image:

```sh
cd targets/esp-idf
. "$HOME/.froth/sdk/esp-idf/export.sh"
idf.py -DFROTH_BOARD=esp32-devkit-v4-game-board -p <path> flash
```

After reflashing, reconnect and re-run the workshop checks from
`docs/guide/Frothy_Workshop_Quick_Reference.md`.

## Do Not Do In The Room

- do not switch attendees back to the old v1 board path mid-session
- do not trust charge-only cables
- do not install `esp-idf` on attendee laptops ad hoc
- do not keep replaying whole files additively when safe send says the
  firmware is too old
