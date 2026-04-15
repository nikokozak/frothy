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
| macOS Apple Silicon | `brew install frothy` preferred; `frothy-v<version>-darwin-arm64.tar.gz` fallback | matching `frothy-vscode-v<extension-version>.vsix` | preflashed `esp32-devkit-v1` | `froth doctor`, VS Code connect, selection send, CLI fallback |
| macOS Intel | `brew install frothy` preferred; `frothy-v<version>-darwin-amd64.tar.gz` fallback | matching `frothy-vscode-v<extension-version>.vsix` | preflashed `esp32-devkit-v1` | `froth doctor`, VS Code connect, selection send, CLI fallback |
| Linux x86_64 | `frothy-v<version>-linux-amd64.tar.gz` | matching `frothy-vscode-v<extension-version>.vsix` | preflashed `esp32-devkit-v1` | `froth doctor`, VS Code connect, selection send, CLI fallback |

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
- `frothy-v<version>-esp32-devkit-v1.zip`
- `frothy-v<version>-checksums.txt`

Carry those exact assets plus the matching checksums file into the clean-machine
passes.

## Clean-Machine Procedure

1. Start from a machine without a repo checkout and without `esp-idf`.
2. Install the CLI from the promised path for that platform.
3. Run `froth doctor`.
4. If several ports are visible, rerun `froth --port <path> doctor`.
5. Install the matching VSIX with `code --install-extension`.
6. If VS Code cannot find `froth`, set `frothy.cliPath`.
7. Connect the preflashed `esp32-devkit-v1` and run `Frothy: Connect Device`.
8. Run `Frothy: Send Selection / Line` on `1 + 1`.
9. Confirm the prompt and workshop base image with `words`, `info @blink`, and
   `info @adc.percent`.
10. Prove recovery with `save`, `dangerous.wipe`, reconnect, and
    `froth --port <path> connect`.
11. After recovery, confirm the board is back in a usable base image with
    `words`, `info @blink`, and `1 + 1`.
12. Record the exact machine, asset names, board, pass/fail result, and any
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
