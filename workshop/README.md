# Frothy Workshop Starter

This directory is the checked-in source for the tiny public workshop repo.

Files:

- `starter.frothy`: the editable workshop starter scaffold

The starter is intentionally separate from the preflashed board base image.
The board carries the workshop helper surface and recovery behavior; attendees
edit and send `starter.frothy` as their own overlay.

Workshop flow:

1. install `frothy`
2. install the Frothy VS Code extension
3. plug in the preflashed `esp32-devkit-v4-game-board`
4. open `starter.frothy`
5. send or edit it live
6. use `save`, `restore`, and `dangerous.wipe` as needed

Regenerate the starter template with:

```sh
sh tools/frothy/export_workshop_repo.sh write
```
