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

## Install

The canonical attendee install flow lives in
`docs/guide/Frothy_Workshop_Install_Quickstart.md` in the repo root.

Extension-specific expectations:

- the installed CLI command is `frothy`
- macOS/Homebrew and Linux x86_64 release tarballs are the maintained attendee
  paths for this tranche
- Windows is not part of the maintained workshop path
- VS Code auto-discovers `frothy` first and falls back to legacy `froth`
  during the transition
- if VS Code cannot find the CLI on `PATH`, set `frothy.cliPath` to the
  absolute path of the installed binary

## Maintained Commands

- `Frothy: Connect Device`
- `Frothy: Disconnect`
- `Frothy: Send Selection / Line`
- `Frothy: Send File`
- `Frothy: Interrupt`
- `Frothy: Words`
- `Frothy: See Binding`
- `Frothy: Save Snapshot`
- `Frothy: Restore Snapshot`
- `Frothy: Dangerous Wipe Snapshot`
- `Frothy: Run Doctor`

## Send Semantics

`Send Selection / Line` is intentional additive eval.

`Send File` is whole-file `reset + eval`. If the connected firmware does not
support control `reset`, the extension blocks the send and tells you to upgrade
or reflash the firmware instead of replaying the file additively.

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
