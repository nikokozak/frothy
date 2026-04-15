# Frothy Workshop Quick Reference

Use this during the maintained workshop path.
It covers only the promised attendee surface:

- released `froth` CLI
- matching `frothy-vscode-v<extension-version>.vsix`
- preflashed `esp32-devkit-v1`

Start elsewhere when needed:

- install and attendee preflight:
  `Frothy_Workshop_Install_Quickstart.md`
- promised-platform validation and recording:
  `Frothy_Workshop_Clean_Machine_Validation.md`
- room-side hardware and recovery card:
  `../../boards/esp32-devkit-v1/WORKSHOP.md`

## First Connect

1. Plug in the preflashed `esp32-devkit-v1`.
2. Run `froth doctor`.
3. If several serial ports are visible, rerun `froth --port <path> doctor`.
4. Open a `.froth` or `.frothy` file in VS Code.
5. Run `Frothy: Connect Device`.
6. Run `Frothy: Send Selection / Line` on `1 + 1`.
7. If the extension path is blocked, fall back to
   `froth --port <path> connect`.

## Prompt Checks

Use these first when the room state is unclear:

```frothy
words
info @millis
info @blink
info @adc.percent
info @boot
show @boot
```

What they tell you:

- `words`: which names are currently live
- `info @millis`: the board-native uptime slot is present
- `info @blink` and `info @adc.percent`: the workshop base image is installed
- `info @boot` or `show @boot`: what startup code is currently bound

## Maintained Board Surface

Stable workshop pins:

- `LED_BUILTIN`
- `A0`
- `BOOT_BUTTON`

Most-used board and base-image calls:

```frothy
millis:
gpio.output: LED_BUILTIN
gpio.high: LED_BUILTIN
gpio.low: LED_BUILTIN
gpio.toggle: LED_BUILTIN
gpio.read: LED_BUILTIN
led.on:
led.off:
led.toggle:
led.blink: 3, 75
adc.percent: A0
```

The sanctioned starter project adds the lesson and game names used in the
classroom path:

- `lesson.ready`
- `lesson.blink`
- `lesson.sample`
- `lesson.animate`
- `game.reset`
- `game.step`
- `game.capture`

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

- VS Code cannot find the CLI: set `frothy.cliPath` to the absolute `froth`
  binary path.
- Several USB serial devices are visible: use
  `froth --port <path> doctor` and `froth --port <path> connect`.
- Whole-file send is blocked as unsafe: the firmware is too old for reset-safe
  send; reflash instead of replaying the file additively.
- The board boots into bad saved state: use safe boot, inspect `boot`, then
  run `dangerous.wipe`.
- The extension is unstable on one laptop: keep the class moving with the CLI
  fallback, not ad hoc toolchain installs.
