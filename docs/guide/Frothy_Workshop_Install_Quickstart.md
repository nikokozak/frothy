# Frothy Workshop Install Quickstart

This is the attendee-facing install note for the current workshop tranche.
Keep it short, operational, and truthful.

## Copy/Paste Email

```text
Subject: Frothy workshop install before you arrive

Please install the Frothy workshop tools before class.

What to install:
- the Frothy CLI release
- the matching Frothy VS Code extension VSIX

The product name is Frothy, but the installed CLI command is still `froth`
during the transition. That is expected.

Supported attendee path:
- macOS (Apple Silicon or Intel): install the `frothy` Homebrew formula
- Linux x86_64: install the release tarball directly
- Windows is not part of the maintained workshop path for this tranche

Install steps:
1. Install the CLI and run `froth doctor`
2. Install the matching `frothy-vscode-v<extension-version>.vsix`
3. Bring a known-good USB data cable for the preflashed `esp32-devkit-v1`

You do not need a repo checkout, ESP-IDF, or a source build before arriving.
```

## Support Matrix

| Surface | Supported attendee path |
| --- | --- |
| CLI | macOS via Homebrew; Linux x86_64 via release tarball |
| VS Code | matching `frothy-vscode-v<extension-version>.vsix` on a machine that can already run `froth` |
| Hardware | preflashed `esp32-devkit-v1` |

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
froth doctor
```

Linux x86_64:

```sh
tar -xzf frothy-v<version>-linux-amd64.tar.gz
mkdir -p "$HOME/.local/bin"
install -m 0755 froth "$HOME/.local/bin/froth"
froth doctor
```

VS Code:

```sh
code --install-extension /path/to/frothy-vscode-v<extension-version>.vsix
```

If VS Code cannot find `froth` on `PATH`, set `frothy.cliPath` to the absolute
path of the installed binary.

## First Connect

1. Plug in the preflashed `esp32-devkit-v1`.
2. Open VS Code.
3. Open a `.frothy` or `.froth` file.
4. Run `Frothy: Connect Device`.
5. Run `Frothy: Send Selection / Line` on a small expression.

## Preflight

Before class, confirm:

- `froth doctor` completes without CLI-path or serial-visibility surprises
- VS Code installs the VSIX successfully
- your laptop can see the board over USB

If you cannot complete those steps, stop there and bring that exact failure to
the workshop instead of installing extra toolchains ad hoc.
