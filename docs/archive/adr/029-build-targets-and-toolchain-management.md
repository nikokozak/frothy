# ADR-029: Build Targets and Toolchain Management

**Date**: 2026-03-12
**Status**: Accepted
**Spec sections**: N/A (project infrastructure)

## Context

Froth targets multiple hardware platforms (POSIX, ESP-IDF, eventually Pico
SDK, Zephyr, etc.). Each platform has its own build system and toolchain.
ESP-IDF uses its own CMake integration (`idf.py`), Pico SDK uses its own
CMake flow, and the POSIX host build uses plain CMake.

The Froth kernel is pure C11 with no platform dependencies, but building a
flashable binary for a microcontroller requires platform-specific project
scaffolding (ESP-IDF component registration, `sdkconfig`, partition tables,
entry points, etc.).

We need to decide:
1. Where platform-specific build scaffolding lives in the repo.
2. How users obtain and manage platform toolchains (ESP-IDF, etc.).
3. How all of this feeds into eventual CLI/editor automation.

## Options Considered

### Option A: Document-only (user installs toolchain, we provide instructions)

No tooling from us. Point users at Espressif's install guide, tell them to
source `export.sh`, and provide an ESP-IDF project they can build manually.

Trade-offs: zero maintenance burden for us. Maximum friction for users.
Every user hits a slightly different install issue. No path toward
automated "pick a board, flash it" workflow.

### Option B: Git submodule

Pin ESP-IDF (and eventually Pico SDK, etc.) as git submodules. Users run
`git submodule update --init` and get a known-good version.

Trade-offs: reproducible builds, version pinning. Adds 500+ MB per SDK
to clone size. Ties us to a specific SDK version. Scales poorly as we
add platforms (each one adds another large submodule).

### Option C: Setup script to a known location

Provide a per-platform setup script (`tools/setup-esp-idf.sh`) that fetches
the SDK to a fixed path (`~/.froth/sdk/esp-idf/`), runs the install, and
tells the user how to activate it. Users who already have the SDK installed
can skip the script and point `IDF_PATH` at their own install.

Trade-offs: controlled version without bloating the repo. Known install
location lets a future CLI find the SDK without asking. Small maintenance
cost (update the version pin when we test against a new release). Users
with existing installs are not forced into our layout.

### Option D: Kernel-only repo, user brings their own project

The Froth repo contains only the kernel, platforms, and boards. No build
scaffolding for any target. Users create their own ESP-IDF project (or
PlatformIO project, or whatever) and pull in the Froth sources.

Trade-offs: maximum flexibility. No turnkey build. Every user reinvents
the project setup. Impossible to automate from a CLI. Fine for
experienced embedded developers, hostile to everyone else.

## Decision

**Option C** (setup script to known location), combined with shared build
target scaffolding in the repo.

### Build targets (`targets/`)

Each build system that Froth supports gets a directory under `targets/`:

```
targets/
  esp-idf/
    CMakeLists.txt          top-level ESP-IDF project file
    main/
      CMakeLists.txt        main component, pulls in kernel + platform + board
      main.c                app_main() calling froth_boot()
    sdkconfig.defaults      sane defaults for Froth on ESP32
```

The ESP-IDF project is shared across all ESP-IDF boards. The board is
selected via a CMake variable (`FROTH_BOARD`). The `board.json` `"chip"`
field determines the `idf.py set-target` argument.

Build flow:
```
source ~/.froth/sdk/esp-idf/export.sh
cd targets/esp-idf
idf.py set-target esp32
idf.py -DFROTH_BOARD=esp32-devkit-v1 build
idf.py flash monitor
```

The POSIX build target remains at the repo root (`CMakeLists.txt`). It
requires no setup script and no SDK. `cmake .. && make` works out of the
box.

### Toolchain setup (`tools/`)

Each platform SDK that requires installation gets a setup script:

```
tools/
  setup-esp-idf.sh         fetch + install ESP-IDF to ~/.froth/sdk/esp-idf/
```

The script:
1. Checks if `~/.froth/sdk/esp-idf/` already exists.
2. If not, clones the pinned ESP-IDF version.
3. Runs `install.sh` for the relevant chip targets.
4. Prints activation instructions.

Users who already have ESP-IDF installed elsewhere can skip the script
and set `IDF_PATH` to their own install.

The `~/.froth/` directory is the canonical location for Froth-managed
SDK installs. A future CLI or editor will look here first when it needs
to build for a given platform.

## Consequences

- Adding a new platform requires: a platform implementation (ADR-028),
  a build target directory under `targets/`, and a setup script under
  `tools/`. The kernel and board layer remain untouched.
- The `~/.froth/sdk/` convention means a future CLI can check for
  installed SDKs without prompting the user. First-time setup becomes
  "run this script" (or, eventually, the CLI runs it for you).
- SDK versions are pinned in the setup scripts. Updating requires
  changing the pin and testing. This is a small maintenance cost.
- Users with non-standard SDK installs are not locked out. `IDF_PATH`
  and equivalent variables override the default locations.
- The POSIX target at the repo root has zero setup friction. This remains
  the fastest path into Froth development.
- The `targets/` directory will grow as platforms are added, but each
  entry is small (a few files of build scaffolding, not SDK code).

## References

- ADR-028 (board and platform architecture)
- ESP-IDF build system: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-guides/build-system.html
- PlatformIO (prior art for multi-platform build management)
- Arduino CLI (prior art for "pick a board, flash it" workflow)
