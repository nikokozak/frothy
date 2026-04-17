# Frothy VS Code Extension

Frothy for VS Code is a thin client on top of the accepted direct-control
surface:

- the extension owns one helper child per window
- the helper owns one direct control session at a time
- there is no daemon, shared port owner, or local-runtime editor mode in the
  maintained path

Keep the rest of the workshop path in the small shared docs set:

- `../../docs/guide/Frothy_Workshop_Install_Quickstart.md`
- `../../docs/guide/Frothy_Workshop_Quick_Reference.md`
- `../../docs/guide/Frothy_Workshop_Clean_Machine_Validation.md`
- `../../boards/esp32-devkit-v4-game-board/WORKSHOP.md`
- `../../workshop/README.md`

## Install

The canonical attendee install flow lives in
`docs/guide/Frothy_Workshop_Install_Quickstart.md` in the repo root.

Extension-specific expectations:

- the installed CLI command is `frothy`
- macOS/Homebrew and Linux x86_64 release tarballs are the maintained attendee
  paths for this tranche
- Windows is not part of the maintained workshop path
- the workshop repo is intentionally tiny: [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop)
- VS Code auto-discovers `frothy` first and falls back to legacy `froth`
  during the transition
- if VS Code cannot find the CLI on `PATH`, set `frothy.cliPath` to the
  absolute path of the installed binary

## Maintained Commands

- `Frothy: Connect Device`
- `Frothy: Disconnect`
- `Frothy: Send Selection / Form`
- `Frothy: Run Binding`
- `Frothy: Pin Run Binding`
- `Frothy: Rerun Last Form`
- `Frothy: Run Pinned Binding`
- `Frothy: Send File`
- `Frothy: Interrupt`
- `Frothy: Words`
- `Frothy: See Binding`
- `Frothy: Save Snapshot`
- `Frothy: Restore Snapshot`
- `Frothy: Dangerous Wipe Snapshot`
- `Frothy: Run Doctor`

## Send Semantics

`Send Selection / Form` is intentional additive eval.

`Run Binding` prompts for a zero-arity binding name and evaluates `name:`.
`Rerun Last Form` repeats the last remembered run form. Sending an expression
or call with `Send Selection / Form` remembers it; sending definitions, `set`
forms, or top-level value updates leaves the remembered run form unchanged.

`Pin Run Binding` records a zero-arity binding name without running it.
`Run Pinned Binding` always evaluates the pinned `name:` call, so you can send
small edits with `Send Selection / Form` and then rerun a fixed target such as
`boot:` without moving the cursor back to that line.

`Send File` is whole-file `reset + eval`. If the connected firmware does not
support control `reset`, the extension warns before any explicitly unsafe
additive fallback and otherwise tells you to upgrade or reflash the firmware.

`Rerun Last Form` is bound to `Cmd+Option+R` on macOS and `Ctrl+Alt+R` on other
platforms.

`Pin Run Binding` is bound to `Cmd+Option+P` on macOS and `Ctrl+Alt+P` on other
platforms. `Run Pinned Binding` is bound to `Cmd+Option+Enter` on macOS and
`Ctrl+Alt+Enter` on other platforms.

`See Binding` is bound to `Cmd+Option+B` on macOS and `Ctrl+Alt+B` on other
platforms.

`Interrupt` is bound to `Cmd+Option+.` on macOS and `Ctrl+Alt+.` on other
platforms while a program is running.

## Development

```sh
npm ci
npm test
npm run test:package
```

`npm run test:package` builds a temporary VSIX and asserts that the packaged
artifact ships only the curated runtime/doc files. The full attendee
quickstart lives at `docs/guide/Frothy_Workshop_Install_Quickstart.md` in the
repo root.
