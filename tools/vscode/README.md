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
- `../../boards/esp32-devkit-v1/WORKSHOP.md`

## Install

The extension requires the installed Frothy CLI command, which is still
spelled `froth` during the transition.

Supported attendee path:

- macOS: install the `frothy` Homebrew formula, which provides `froth`
- Linux x86_64: install the release tarball and place `froth` on `PATH`
- Windows is not part of the maintained workshop path for this tranche

Preferred macOS install:

```sh
brew tap nikokozak/frothy
brew install frothy
froth doctor
```

Then install the matching release VSIX:

```sh
code --install-extension /path/to/frothy-vscode-v<extension-version>.vsix
```

If VS Code cannot find `froth` on `PATH`, set `frothy.cliPath` to the absolute
path of the installed binary.

The public naming split is intentional for now:

- product, docs, release assets, and commands palette: `Frothy`
- installed CLI command: `froth`
- repo-local checkout build: `tools/cli/froth-cli`

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
