# Boards

A board represents a specific piece of hardware: an ESP32 DevKit, a custom
PCB, a Raspberry Pi Pico, or the POSIX host (used for development). Adding
a board does not require understanding Froth internals. You need to know
your hardware and a small amount of C.

Boards sit on top of **platforms**. A platform handles the chip family and
SDK (POSIX, ESP-IDF, Pico SDK). A board handles the specifics of one piece
of hardware: which pins are available, what peripherals are connected, and
what convenience words to expose to the user. If a platform already exists
for your chip, writing a board is straightforward. If not, see
`platforms/README.md` for how to write one.

Board names such as `esp32-devkit-v1` and `esp32-devkit-v4-game-board`
identify specific hardware or carrier revisions. The `v1` and `v4` labels are
not Frothy protocol or runtime generations, and a narrower workshop promise
around one board does not invalidate the other checked-in board targets.

## Maintained workshop board

The current workshop promise is narrower than the repo's total board surface:

- preflashed `esp32-devkit-v4-game-board` proto board in the room
- released CLI plus matching VSIX only
- the published recovery zip still targets `esp32-devkit-v1`
- no extra-board guarantee before the workshop

The room-side hardware checklist and recovery card for that board live in
`boards/esp32-devkit-v4-game-board/WORKSHOP.md`.

## Directory layout

```
boards/
  <name>/
    board.json      board metadata and build config (required)
    ffi.h           declares the board binding table (required)
    ffi.c           board-specific C bindings (required)
    lib/
      base.frothy   convenience words seeded into the Frothy base image (optional)
```

## Adding a new board

1. Copy an existing board directory (start with `boards/posix/`).
2. Edit `board.json`: set your board name, platform, chip, pins, peripherals.
3. Edit `ffi.c`: replace the bindings with your hardware-specific words.
4. Optionally write `lib/base.frothy` for higher-level convenience words that
   should ship in the board base image and survive `dangerous.wipe`.
5. Build: `cmake .. -DFROTH_BOARD=<name> -DFROTH_PLATFORM=<platform>`

## board.json

Every board directory must contain a `board.json`. This file is read by the
build system to set compile-time configuration, and by the editor to
determine what words and pins are available on the target.

```json
{
  "name": "My Board",
  "vendor": "Your Name",
  "platform": "esp-idf",
  "chip": "esp32",
  "description": "Short description of the board.",

  "config": {
    "cell_size_bits": 32,
    "heap_size": 4096,
    "ds_depth": 64,
    "rs_depth": 32,
    "slot_count": 128,
    "has_snapshots": true,
    "snapshot_block_size": 4096
  },

  "peripherals": ["gpio", "adc"],

  "pins": {
    "LED_BUILTIN": 2,
    "SDA": 21,
    "SCL": 22
  }
}
```

### Top-level fields

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Human-readable board name. |
| `vendor` | no | Board manufacturer or author. |
| `platform` | yes | Platform directory name. Must match a directory under `platforms/`. |
| `chip` | yes | Chip identifier (e.g. `esp32`, `esp32s3`, `rp2040`, `host`). |
| `description` | no | One-line summary, shown in editor UI and generated docs. |
| `config` | yes | Build-time configuration. See below. |
| `peripherals` | yes | Which generic FFI modules this board uses. See below. |
| `pins` | yes | Named pin constants. See below. |

### config

Each key maps directly to a CMake compile definition. These control the
size of internal data structures. Larger values use more RAM.

| Key | CMake define | Default | Notes |
|-----|-------------|---------|-------|
| `cell_size_bits` | `FROTH_CELL_SIZE_BITS` | 32 | Width of a Froth cell in bits. Valid values: 8, 16, 32, 64. |
| `heap_size` | `FROTH_HEAP_SIZE` | 4096 | Heap size in bytes. All quotations, patterns, and strings are allocated here. |
| `ds_depth` | `FROTH_DS_CAPACITY` | 256 | Maximum data stack depth in cells. |
| `rs_depth` | `FROTH_RS_CAPACITY` | 256 | Maximum return stack depth in cells. |
| `slot_count` | `FROTH_SLOT_TABLE_SIZE` | 128 | Maximum number of named slots (words). Includes stdlib and board words. |
| `has_snapshots` | `FROTH_HAS_SNAPSHOTS` | true | Enable save/restore/wipe. Requires the platform to implement snapshot storage. |
| `snapshot_block_size` | `FROTH_SNAPSHOT_BLOCK_SIZE` | 2048 | Size of each snapshot storage slot in bytes. |
| `frothy_object_capacity` | `FROTHY_OBJECT_CAPACITY` | 128 | Optional Frothy runtime object/free-span capacity for boards that ship a larger base library. |
| `frothy_payload_capacity` | `FROTHY_PAYLOAD_CAPACITY` | 16384 | Optional Frothy runtime payload arena size in bytes for boards that ship a larger base library. |

For resource-constrained targets, reduce `heap_size`, `ds_depth`,
`rs_depth`, and `slot_count`. The POSIX board uses generous defaults
suitable for desktop development. An ESP32 with 320KB SRAM can
comfortably use the defaults. Smaller chips (ESP32-C3, RP2040) may
need lower values depending on how much RAM your application needs
for other purposes.

### peripherals

A list of peripheral module names. The build system uses this list to
decide which generic FFI modules to compile from the platform directory.

For example, if a board declares `"peripherals": ["gpio", "adc"]` and
its platform is `esp-idf`, the build system will include `ffi_gpio.c`
and `ffi_adc.c` from `platforms/esp-idf/` (if they exist). This means
the board gets `gpio.mode`, `gpio.write`, `gpio.read`, and `adc.read`
words without the board author writing any C code for them.

Currently recognized peripheral names: `gpio`, `adc`, `dac`, `i2c`,
`spi`, `uart`, `timer`. Not all platforms implement all peripherals.

### pins

A map of named pin constants. At build time, these are converted to Froth
word definitions and embedded into the binary. For example:

```json
"pins": {
  "LED_BUILTIN": 2,
  "SDA": 21
}
```

generates the equivalent of:

```froth
2 'LED_BUILTIN def
21 'SDA def
```

These words are available in `lib/base.frothy` and in user code at the REPL.
This avoids hardcoding pin numbers in board Froth libraries and makes
code readable: `LED_BUILTIN 1 gpio.write` instead of `2 1 gpio.write`.

## ffi.h

Declares the board binding table. Every board needs this file, and in
new code should prefer the maintained Frothy-native export:

```c
#pragma once
#include "frothy_ffi.h"
FROTHY_FFI_DECLARE(frothy_board_bindings);
```

`FROTHY_FFI_DECLARE` forward-declares a null-terminated array of
`frothy_ffi_entry_t`. The Frothy boot path installs that table as base-image
native slots.

Retained boards such as `boards/posix/` and `boards/esp32-devkit-v1/` still
ship a legacy `froth_ffi_entry_t` export while they are being ported. New
board code should not start there.

## ffi.c

Contains the C callbacks that expose hardware to Frothy.

### Writing a binding

```c
#include "ffi.h"
#include "frothy_ffi.h"
#include <driver/gpio.h>  /* or whatever your platform SDK provides */

static const frothy_ffi_param_t led_on_params[] = {
  FROTHY_FFI_PARAM_INT("pin"),
};

static froth_error_t board_led_on(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count,
                                  frothy_value_t *out) {
  int32_t pin = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &pin));
  gpio_set_level(pin, 1);
  return frothy_ffi_return_nil(out);
}

FROTHY_FFI_TABLE_BEGIN(frothy_board_bindings)
  {
    .name = "led.on",
    .params = led_on_params,
    .param_count = FROTHY_FFI_PARAM_COUNT(led_on_params),
    .arity = 1,
    .result_type = FROTHY_FFI_VALUE_NIL,
    .help = "Turn on the onboard LED",
    .flags = FROTHY_FFI_FLAG_NONE,
    .callback = board_led_on,
    .stack_effect = "( pin -- )",
  },
FROTHY_FFI_TABLE_END
```

### The binding table

Collect all bindings into a table at the bottom of `ffi.c`:

```c
FROTHY_FFI_TABLE_BEGIN(frothy_board_bindings)
  { ... },
  { ... },
FROTHY_FFI_TABLE_END
```

If the board has no custom hardware and relies entirely on generic
platform modules, the table can be empty:

```c
#include "ffi.h"
FROTHY_FFI_TABLE_BEGIN(frothy_board_bindings)
FROTHY_FFI_TABLE_END
```

### Error handling

Return a Frothy value on success. To signal an error to the user, return a
regular Froth error code or raise a richer Frothy FFI error:

```c
static froth_error_t board_adc_read(frothy_runtime_t *runtime,
                                    const void *context,
                                    const frothy_value_t *args,
                                    size_t arg_count,
                                    frothy_value_t *out) {
  int32_t chan = 0;
  int result;

  (void)context;
  (void)arg_count;
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &chan));
  result = adc_read(chan);
  if (result < 0) {
    return frothy_ffi_raise(runtime, FROTH_ERROR_IO,
                            "adc", "adc.read", "adc_read failed");
  }
  return frothy_ffi_return_int(result, out);
}
```

Error codes 1-299 are reserved for the Froth kernel. Board-specific
errors should use codes 300 and above.

## lib/base.frothy

Optional. Board-level words written in Frothy rather than C.

### Boot order

The maintained Frothy base-image path seeds code in this order:

1. Frothy builtins (`save`, `restore`, `dangerous.wipe`, `words`, `see`, `core`, `slotInfo`, `boot`)
2. Generated board pin constants (from `board.json` `"pins"`)
3. Board FFI bindings (from `ffi.c`)
4. Board base library (`lib/base.frothy`, if present)
5. Mark boot complete
6. Restore snapshot (if one exists)
7. Run `boot()`
8. Enter REPL

`lib/base.frothy` runs during base-image seeding with board pins and board FFI
already installed, and its definitions survive `dangerous.wipe`.

### Example

```frothy
led.pin is LED_BUILTIN

to led.on [ gpio.write: led.pin, 1 ]
to led.off [ gpio.write: led.pin, 0 ]
to led.blink with wait [
  led.on:;
  ms: wait;
  led.off:
]
```

### Guidelines

- Prefix board-specific words with a namespace (`led.`, `board.`, etc.).
- Do not redefine stdlib words (`dup`, `swap`, `def`, `call`, etc.).
- Keep it short. This file runs on every boot and adds to startup time.
  On a microcontroller where boot-to-autorun latency matters (audio,
  control systems), every millisecond counts.
