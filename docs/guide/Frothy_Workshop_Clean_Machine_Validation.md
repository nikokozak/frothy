# Frothy Workshop Clean-Machine Validation

Status: checked-in validation path
Date: 2026-04-14

This doc freezes the exact validation path for the attendee surface Frothy
currently promises.

It does not claim that every promised platform has already been exercised on
this checkout.
Use it to run and record those passes.

## Promised Matrix

| Platform | CLI install path | Extension asset | Board path | Must pass |
| --- | --- | --- | --- | --- |
| macOS Apple Silicon | `brew install frothy` preferred; `frothy-v<version>-darwin-arm64.tar.gz` fallback | Marketplace listing `frothy.frothy` preferred; matching `frothy-vscode-v<extension-version>.vsix` fallback | preflashed `esp32-devkit-v4-game-board` proto board | `frothy doctor`, VS Code connect, Pong-on-boot, `pong.frothy`, CLI fallback |
| macOS Intel | `brew install frothy` preferred; `frothy-v<version>-darwin-amd64.tar.gz` fallback | Marketplace listing `frothy.frothy` preferred; matching `frothy-vscode-v<extension-version>.vsix` fallback | preflashed `esp32-devkit-v4-game-board` proto board | `frothy doctor`, VS Code connect, Pong-on-boot, `pong.frothy`, CLI fallback |
| Linux x86_64 | `frothy-v<version>-linux-amd64.tar.gz` | Marketplace listing `frothy.frothy` preferred; matching `frothy-vscode-v<extension-version>.vsix` fallback | preflashed `esp32-devkit-v4-game-board` proto board | `frothy doctor`, VS Code connect, Pong-on-boot, `pong.frothy`, CLI fallback |

Not promised here:

- Windows
- extra boards
- source builds
- pre-workshop `esp-idf` installs on attendee machines

## Release Assets To Stage

The current release workflow builds and publishes:

- `frothy-v<version>-darwin-arm64.tar.gz`
- `frothy-v<version>-darwin-amd64.tar.gz`
- `frothy-v<version>-linux-amd64.tar.gz`
- `frothy-vscode-v<extension-version>.vsix`
- `frothy-v<version>-checksums.txt`

The attendee extension path is the Marketplace listing `frothy.frothy`, with
the VSIX kept as fallback.
The workshop proto-board firmware is not a published attendee asset.
Carry the CLI assets, the matching checksums file, access to
[nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop) for
`pong.frothy`, and preflashed `esp32-devkit-v4-game-board` boards into the
clean-machine passes.

## Clean-Machine Procedure

1. Start from a machine without a repo checkout and without `esp-idf`.
2. Install the CLI from the promised path for that platform.
3. Run `frothy doctor`.
4. If several ports are visible, rerun `frothy --port <path> doctor`.
5. Install the Marketplace extension or the matching VSIX with `code --install-extension`.
6. If VS Code cannot find `frothy`, set `frothy.cliPath`. Legacy `froth`
   fallback remains available during the transition.
7. Connect the preflashed `esp32-devkit-v4-game-board` proto board and run
   `Frothy: Connect Device`.
8. Confirm the board is already running the shipped Pong demo.
9. Run `Frothy: Send Selection / Line` on `1 + 1`.
10. Open [nikokozak/frothy-workshop](https://github.com/nikokozak/frothy-workshop) and send `pong.frothy`.
11. Change one visible behavior in `pong.frothy` and resend it.
12. Confirm the prompt and workshop base image with `words`,
   `info @matrix.init`, `info @grid.clear`, `info @joy.up?`, and
   `info @knob.left`.
13. Run `matrix.init:`, `grid.clear:`, `grid.set: 1, 1, true`, and
    `grid.show:` for a visible matrix proof.
14. Confirm one input and one analog helper with `joy.up?:` and `knob.left:`.
15. Prove recovery with `save`, `dangerous.wipe`, reconnect, and
    `frothy --port <path> connect`.
16. After recovery, confirm the board is back in a usable base image with
    `words`, `info @grid.clear`, `info @joy.up?`, and `1 + 1`.
17. Record the exact machine, asset names, board, pass/fail result, and any
    remediation.

## Recording Sheet

| Date | Operator | Platform | CLI asset | VSIX | Port | Result | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| pending | pending | macOS Apple Silicon | pending | pending | pending | pending | pending |
| pending | pending | macOS Intel | pending | pending | pending | pending | pending |
| pending | pending | Linux x86_64 | pending | pending | pending | pending | pending |

## Exit Rule

- Only promise the rows that actually passed.
- Treat install failure as a workshop blocker, not a late bug.
- Do not widen the support matrix because a machine "probably" works.
