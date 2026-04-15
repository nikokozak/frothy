# Frothy Workshop Install Quickstart

This is the attendee-facing install note for the current workshop tranche.
Keep it short, operational, and truthful.

Use this with:

- `Frothy_Workshop_Quick_Reference.md` during the actual lesson path
- `Frothy_Workshop_Clean_Machine_Validation.md` when maintainers rehearse the
  promised install surface

## Copy/Paste Email

```text
Subject: Frothy workshop install before you arrive

Please install the Frothy workshop tools before class.

What to install:
- the Frothy CLI release
- the Frothy VS Code extension from the Marketplace, or the matching VSIX

The product name is Frothy and the installed CLI command is `frothy`.
VS Code still accepts legacy `froth` discovery during the transition when
needed, but new install guidance should use `frothy`.

Supported attendee path:
- macOS (Apple Silicon or Intel): install the `frothy` Homebrew formula
- Linux x86_64: install the release tarball directly
- Windows is not part of the maintained workshop path for this tranche

Install steps:
1. Install the CLI and run `frothy doctor`
2. Install the `frothy.frothy` VS Code extension, or the matching `frothy-vscode-v<extension-version>.vsix`
3. Bring a known-good USB data cable for the preflashed `esp32-devkit-v4-game-board`
   proto board

You do not need a repo checkout, ESP-IDF, or a source build before arriving.
```

## Support Matrix

| Surface | Supported attendee path |
| --- | --- |
| CLI | macOS via Homebrew; Linux x86_64 via release tarball |
| VS Code | Marketplace listing `frothy.frothy` preferred; matching `frothy-vscode-v<extension-version>.vsix` fallback on a machine that can already run `frothy` |
| Hardware | preflashed `esp32-devkit-v4-game-board` proto board |
| Workshop source | [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop) |

Not promised here:

- Windows
- extra boards
- source builds or repo-local checkout workflows
- installing `esp-idf` before class

## Install

macOS:

```sh
brew tap nikokozak/frothy
brew install frothy
frothy doctor
```

Linux x86_64:

```sh
tar -xzf frothy-v<version>-linux-amd64.tar.gz
mkdir -p "$HOME/.local/bin"
install -m 0755 frothy "$HOME/.local/bin/frothy"
frothy doctor
```

VS Code:

```sh
code --install-extension frothy.frothy
```

VSIX fallback:

```sh
code --install-extension /path/to/frothy-vscode-v<extension-version>.vsix
```

If VS Code cannot find `frothy` on `PATH`, set `frothy.cliPath` to the
absolute path of the installed binary. Legacy `froth` fallback remains
available during the transition.

## First Connect

1. Plug in the preflashed `esp32-devkit-v4-game-board` proto board.
2. Confirm the board is already running the shipped Pong demo.
3. Clone or open [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop) and open `pong.frothy`.
4. Open VS Code.
5. Run `Frothy: Connect Device`.
6. Run `Frothy: Send Selection / Line` on a small expression.
7. Run `matrix.init:`, `grid.clear:`, `grid.show:`, or edit `pong.frothy` for the first visible hardware check.

## Preflight

Before class, confirm:

- `frothy doctor` completes without CLI-path or serial-visibility surprises
- VS Code installs the Marketplace extension or the matching VSIX successfully
- your laptop can see the board over USB
- the board is already running Pong when first plugged in
- `matrix.init:` and `grid.show:` work on the attached proto board
- `pong.frothy` opens and sends cleanly

If you cannot complete those steps, stop there and bring that exact failure to
the workshop instead of installing extra toolchains ad hoc.
