# Frothy Workshop Pong

This directory is the checked-in source for the tiny public workshop repo.

Files:

- `pong.frothy`: the editable workshop example and starter overlay

The board-side canonical source lives in
`boards/esp32-devkit-v4-game-board/lib/base.frothy`.
`pong.frothy` is exported from that base image so the shipped demo board and
the workshop overlay stay aligned.

Workshop flow:

1. install `frothy`
2. install the Frothy VS Code extension
3. plug in the preflashed `esp32-devkit-v4-game-board`
4. open `pong.frothy`
5. send or edit it live
6. use `save`, `restore`, and `dangerous.wipe` as needed

Do not hand-edit `pong.frothy` in the Frothy repo.
Update the canonical base image instead, then regenerate with:

```sh
sh tools/frothy/export_workshop_repo.sh write
```
