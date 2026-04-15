# ESP32 DevKit V1 Workshop Kit

Status: room-side checklist and recovery card
Date: 2026-04-14

This is the maintained classroom hardware path for the current workshop
tranche.
It is the only board path promised to attendees here.

## Board Facts

- board: `esp32-devkit-v1`
- platform: `esp-idf`
- pins: `LED_BUILTIN=2`, `A0=34`, `BOOT_BUTTON=0`, `UART_TX=17`,
  `UART_RX=16`
- board/base-image surface:
  `millis`, `gpio.mode`, `gpio.write`, `gpio.read`, `ms`, `adc.read`,
  `blink`, `animate`, `led.*`, and `adc.percent`

## Minimum Room Pack-Out

- one preflashed `esp32-devkit-v1` per seat
- one known-good USB data cable per board
- at least one spare preflashed board
- at least one spare known-good USB data cable
- labels that tie each board and cable back to the room inventory
- one maintainer machine that already has the matching CLI, VSIX, `esptool`,
  and `esp-idf`
- staged copies of the current CLI assets, VSIX, firmware zip, and checksums

This checklist is not complete until someone physically packs and labels the
kit.

## First-Line Recovery

1. Run `frothy --port <path> doctor`.
2. Run `frothy --port <path> connect` and evaluate `1 + 1`.
3. If saved state is broken, power-cycle or reset the board, press `Ctrl-C`
   during the safe-boot window, then run `dangerous.wipe`.
4. After recovery, confirm `info @blink` and `1 + 1` at the prompt before
   handing the board back.
5. If the VS Code path is blocked, keep using the CLI path instead of changing
   toolchains on the attendee machine.

## Reflash Paths

Fastest default-board recovery from released assets:

```sh
frothy --port <path> flash
```

Starter-project recovery when you need the sanctioned lesson path back on the
board:

```sh
frothy new --target esp32-devkit-v1 recover-board
cd recover-board
frothy --port <path> doctor
frothy --port <path> flash
```

After reflashing, reconnect and re-run the starter or lesson path from the
maintained workshop docs.

## Do Not Do In The Room

- do not switch attendees to unproved boards
- do not trust charge-only cables
- do not install `esp-idf` on attendee laptops ad hoc
- do not keep replaying whole files additively when safe send says the
  firmware is too old
