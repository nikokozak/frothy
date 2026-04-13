# ADR-028: Board and Platform Architecture

**Date**: 2026-03-12
**Status**: Accepted
**Spec sections**: N/A (project architecture, not language semantics)

## Context

Froth needs to run on multiple hardware targets (ESP32, RP2040, bare-metal ARM, POSIX host) while keeping the kernel portable and making it easy for newcomers and LLMs to add support for new boards. The first non-POSIX target is ESP32 DevKit V1 (original ESP32, dual-core Xtensa, ESP-IDF).

Arduino's longevity comes from separating what changes per chip family (rare, expert-written) from what changes per board (frequent, newcomer-contributed). Froth needs a similar separation.

Key constraints:
- Must work on resource-constrained targets (no dynamic linking, no filesystem assumed, no JSON parser at runtime)
- Must be approachable for contributors who know their hardware but not Froth internals
- Must produce structured metadata for editor tooling and LLM-assisted board generation
- Must not introduce build system complexity disproportionate to the problem

## Options Considered

### Option A: Flat board directories (status quo)

Each board is a directory with C files and headers. No metadata, no separation between platform and board. Every new target copies and modifies `main.c`, `platform_posix.c`, and `boards/posix/`.

Trade-offs: simple to understand, but every board reinvents the platform layer. No structured metadata for tooling. Newcomers must understand the full build system and platform API to add a board.

### Option B: Three-layer architecture (platform / board / boot)

Separate platform implementations (SDK-level, expert-written, shared across boards) from board definitions (hardware-specific, declarative, newcomer-contributed). Shared boot sequence as a convenience wrapper.

Trade-offs: more upfront structure, but each layer has a clear audience and rate of change. Declarative board definitions enable tooling and LLM generation. Slightly more complex build system.

### Option C: Full abstraction with code generation

Build system reads board metadata and generates entry points, FFI registration code, pin constant files, and linker scripts. Board contributor only writes JSON and maybe Froth.

Trade-offs: maximum convenience for board authors, but generated code is hard to debug, hides control flow, and creates build system complexity that becomes a maintenance burden. Overly magical.

## Decision

**Option B**, with the constraint that generated pin constants (JSON to Froth) are the ceiling of build-time generation complexity. No generated C code, no generated entry points.

### Layer 1: Platform (`platforms/<name>/`)

One per SDK/RTOS family. Implements `platform.h`. Contains generic FFI modules for peripherals common to all boards on that platform.

Expected platforms (ever): POSIX, ESP-IDF, Pico SDK, Zephyr, bare-metal ARM. Maybe 5-10 total. Written by experienced contributors.

Contents:
- `platform_<name>.c` — implements `platform_init`, `platform_emit`, `platform_key`, `platform_key_ready`, `platform_fatal`, and optionally `platform_snapshot_*`
- `ffi_gpio_<name>.c`, `ffi_adc_<name>.c`, etc. — generic FFI modules using the platform's native APIs. One file per peripheral category. Shared across all boards on this platform.

Generic FFI modules are parameterized by board metadata (pin maps, channel configs), not hardcoded per chip variant. The GPIO driver for ESP32 and ESP32-S3 is the same C code — chip-specific validation is delegated to the underlying SDK.

Platforms are not required to have POSIX simulation equivalents. Testing without hardware is the platform author's problem to solve or not solve.

### Layer 2: Board (`boards/<name>/`)

One per physical board or hardware configuration. The thing newcomers, LLMs, and eventually the editor contribute.

Required file:
- `board.json` — metadata, build configuration, pin map, declared peripherals

Optional files:
- `lib.froth` — board-specific convenience words in Froth (e.g., `led.on`, `led.off`, named pin aliases beyond the generated constants)
- `ffi.c` / `ffi.h` — board-specific C FFI bindings for custom hardware not covered by generic platform modules (specific sensors, displays, proprietary protocols)

#### `board.json` schema

```json
{
  "name": "ESP32 DevKit V1",
  "vendor": "DoIt",
  "platform": "esp-idf",
  "chip": "esp32",
  "description": "DoIt ESP32 DevKit V1 development board",

  "config": {
    "cell_size_bits": 32,
    "heap_size": 4096,
    "ds_depth": 64,
    "rs_depth": 32,
    "slot_count": 128,
    "has_snapshots": true,
    "snapshot_block_size": 4096
  },

  "peripherals": ["gpio", "adc", "dac", "i2c", "uart"],

  "pins": {
    "LED_BUILTIN": 2,
    "BOOT_BUTTON": 0,
    "SDA": 21,
    "SCL": 22
  }
}
```

The `peripherals` list drives which generic FFI modules the build system includes from the platform. The `pins` map is used to generate a `board_pins.froth` file at build time (e.g., `2 'LED_BUILTIN def`) that is embedded and loaded during boot alongside the stdlib.

The `config` section maps directly to CMake defines (`FROTH_CELL_SIZE_BITS`, `FROTH_HEAP_SIZE`, etc.).

#### `lib.froth` contract

- Runs after stdlib (`core.froth`) and generated pin constants, before `boot_complete` / restore / autorun
- Has access to all stdlib words and pin constants
- Board words must use a prefix namespace (`led.`, `board.`, `esp.`, etc.) — never shadow stdlib names
- Should be kept small — it runs on every boot and affects boot-to-autorun latency

### Layer 3: Boot sequence

The current boot sequence in `main.c` (register primitives, register board, platform_init, load stdlib, boot_complete, restore, autorun, REPL) is extracted into a shared function:

```c
void froth_boot(const froth_ffi_entry_t *board_bindings);
```

This function calls documented public functions in sequence. It is a **convenience wrapper, not a black box**. If a board needs custom init ordering (WiFi before FFI, clock config, self-test), the author copies the ~15 lines from `froth_boot()` and inserts their own steps. The individual functions (`froth_ffi_register`, `platform_init`, `froth_evaluate_input`) are the real API.

Platform-specific entry points are hand-written, not generated:
- POSIX: `int main(void) { froth_boot(board_bindings); return 0; }`
- ESP-IDF: `void app_main(void) { froth_boot(board_bindings); }`

### Core hardware vocabulary

Platforms that provide a given peripheral must use these standard word names and stack effects:

| Word | Effect | Description |
|------|--------|-------------|
| `gpio.mode` | `( pin mode -- )` | Configure pin direction |
| `gpio.write` | `( pin val -- )` | Set digital output |
| `gpio.read` | `( pin -- val )` | Read digital input |
| `adc.read` | `( chan -- val )` | Read ADC channel |
| `ms` | `( n -- )` | Delay milliseconds |
| `us` | `( n -- )` | Delay microseconds |

Platform-specific extras are fine as additional words with platform-prefixed names (e.g., `esp.wifi.connect`, `rp2.pio.init`). This table will grow via future ADRs as new peripheral categories are standardized.

### Out of scope

- **"Froth as a scripting component"**: adding Froth to an existing firmware project (e.g., as an ESP-IDF component in someone else's `app_main`) is a different integration story. Deferred to a potential "Froth-Script" variation.
- **Separate board repos / board manager**: start monorepo, split later if the ecosystem grows.
- **POSIX simulation requirement**: boards are not required to have POSIX test equivalents. Hardware-in-the-loop testing is the board author's responsibility.

## Consequences

- Adding a new board on an existing platform: create a directory, write `board.json`, optionally `lib.froth`. No C required for standard peripherals.
- Adding a new platform: implement `platform.h` (~6 functions), write generic FFI modules, create at least one board. Expert-level work.
- The `peripherals` list in `board.json` becomes a contract: if you declare `"gpio"`, the platform must provide `gpio.mode`, `gpio.write`, `gpio.read` with the standard stack effects.
- Build system gains responsibility for: reading `board.json`, setting CMake defines from `config`, including platform + generic FFI sources, generating `board_pins.froth` from `pins`. This is the main complexity cost.
- Editor tooling can parse `board.json` for completions, pin validation, and peripheral availability without running the build.
- LLMs can generate `board.json` + `lib.froth` from a datasheet with high reliability — the schema is small and structured.
- Core vocabulary table constrains all future platform authors. Adding new standard words requires an ADR.
- `froth_boot()` must remain simple and copyable. If it grows beyond ~20 lines, something is wrong.

## References

- Arduino platform specification: https://arduino.github.io/arduino-cli/latest/platform-specification/
- ESP-IDF build system (component architecture): https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html
- ADR-019 (FFI public C API)
- ADR-027 (platform snapshot storage API)
- Current board structure: `boards/posix/`, `platform_posix.c`
