# Frothy VS Code Extension

Frothy for VS Code is a thin client on top of the accepted direct-control
surface:

- the extension owns one helper child per window
- the helper owns one direct control session at a time
- there is no daemon, shared port owner, or local-runtime editor mode in the
  maintained path

## Install

The extension requires the transitional `froth` CLI on your machine.

Workshop install path:

```sh
brew tap nikokozak/frothy
brew install frothy
froth doctor
```

Then install the release VSIX from the matching GitHub Release:

```sh
code --install-extension /path/to/frothy-vscode-v<extension-version>.vsix
```

If VS Code cannot find `froth` on `PATH`, set `frothy.cliPath` to the absolute
path of the installed binary.

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
artifact ships only the curated runtime/doc files.
