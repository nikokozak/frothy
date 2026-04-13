# Platforms

A platform implements the hardware abstraction layer for a chip family and
SDK. Each platform provides console I/O, timing, fatal error handling, and
(optionally) persistent snapshot storage.

Froth separates **platforms** from **boards**. A platform covers a chip
family and its SDK (POSIX, ESP-IDF, Pico SDK, Zephyr). A board covers a
specific piece of hardware (ESP32 DevKit V1, a custom PCB). There will only
be a handful of platforms. Boards are what most contributors write.

If your chip family already has a platform, you do not need to be here.
See `boards/README.md` instead.

## Directory layout

```
platforms/
  <name>/
    platform.c      implements platform.h (required)
```

The directory name must match the `"platform"` field in any `board.json`
that targets this platform. For example, if a board sets
`"platform": "esp-idf"`, the build system expects to find
`platforms/esp-idf/platform.c`.

## Required functions

`platform.c` must implement every function declared in `src/platform.h`.
All functions return `froth_error_t` (an integer type where 0 means
success) unless otherwise noted.

### platform_init

```c
froth_error_t platform_init(void);
```

Called once during boot, before any I/O. Use this for signal handler
registration, UART peripheral setup, clock configuration, or anything
else the platform needs before it can send and receive bytes.

### platform_emit

```c
froth_error_t platform_emit(uint8_t byte);
```

Write one byte to the console output. On a hosted system this is stdout.
On a microcontroller this is typically the UART TX line or USB-CDC
endpoint connected to the user's terminal. Must not buffer indefinitely;
the byte should be transmitted or queued for transmission before
returning.

### platform_key

```c
froth_error_t platform_key(uint8_t *byte);
```

Read one byte from the console input. This call blocks until a byte is
available. On a hosted system, read from stdin. On a microcontroller,
read from the UART RX line or USB-CDC endpoint.

### platform_key_ready

```c
bool platform_key_ready(void);
```

Return true if at least one byte can be read from console input without
blocking. Used by the Froth `key?` word to allow non-blocking input
polling. On POSIX, `poll()` with a zero timeout works. On a
microcontroller, check the UART RX FIFO status register or equivalent.

### platform_fatal

```c
_Noreturn void platform_fatal(void);
```

Called when the boot sequence encounters an unrecoverable error (failed
primitive registration, corrupt stdlib, etc.). The caller will have
already emitted a diagnostic message to the console via `platform_emit`
before calling this function.

What this function does is platform-dependent:

- POSIX: `exit(1)`
- ESP-IDF: `esp_restart()` or an infinite loop that lets the watchdog fire
- Bare metal: infinite loop, or write to the software reset register

This function must not return.

## Snapshot storage (optional)

Froth can persist user-defined words and data across reboots using a
binary snapshot format. The snapshot system uses two storage slots (A and
B) to provide atomic save/restore: when saving, the system writes to
whichever slot is *not* the current active snapshot, then bumps a
generation counter. On restore, it reads whichever slot has the higher
valid generation. This means a power loss during save does not corrupt
the existing snapshot.

If a platform enables snapshot support (by defining `FROTH_HAS_SNAPSHOTS`
in its CMake configuration), it must implement three additional functions.
Each function takes a `slot` parameter (0 for slot A, 1 for slot B).

### platform_snapshot_read

```c
froth_error_t platform_snapshot_read(uint8_t slot, uint32_t offset,
                                     uint8_t *buf, uint32_t len);
```

Read `len` bytes starting at `offset` within the given slot into `buf`.
On POSIX this is a file read with `fseek` + `fread`. On a microcontroller
this might read from a dedicated flash partition or NVS region.

Return `FROTH_ERROR_IO` if the slot does not exist or the read fails.

### platform_snapshot_write

```c
froth_error_t platform_snapshot_write(uint8_t slot, uint32_t offset,
                                      const uint8_t *buf, uint32_t len);
```

Write `len` bytes from `buf` to `offset` within the given slot. On POSIX
this is `fseek` + `fwrite`, creating the file if it does not exist. On a
microcontroller, write to the corresponding flash partition.

Return `FROTH_ERROR_IO` on failure.

### platform_snapshot_erase

```c
froth_error_t platform_snapshot_erase(uint8_t slot);
```

Erase the entire contents of the given slot. On POSIX, delete the file.
On a microcontroller, erase the flash partition or NVS region.

If the slot is already empty, return `FROTH_OK` (not an error).

### Storage sizing

Each slot must be at least `FROTH_SNAPSHOT_BLOCK_SIZE` bytes (a
compile-time define, default 2048). The board's `board.json` can override
this value.

## Generic FFI modules

Platforms may contain shared FFI modules for common peripherals (GPIO, ADC,
I2C, etc.). These live alongside `platform.c` in the platform directory
and are compiled for any board on the platform that lists the peripheral
in its `board.json` `"peripherals"` array.

For example, a platform might provide `ffi_gpio.c` that implements GPIO
using the platform's native driver API. Any board on that platform that
lists `"gpio"` in its peripherals gets this module compiled in
automatically, without the board author writing any GPIO code.

When a platform provides a generic peripheral module, it must use the
standard Froth word names so that code written for one board is portable
to another:

| Word | Effect | Notes |
|------|--------|-------|
| `gpio.mode` | `( pin mode -- )` | 1 = output, 0 = input |
| `gpio.write` | `( pin val -- )` | |
| `gpio.read` | `( pin -- val )` | |
| `adc.read` | `( chan -- val )` | |
| `ms` | `( n -- )` | Delay n milliseconds |
| `us` | `( n -- )` | Delay n microseconds |

Platform-specific words beyond this set should use a namespace prefix
(e.g. `esp.restart`, `rp2.pio.init`). This keeps the core vocabulary
portable while allowing full access to platform capabilities.

## Reference: POSIX platform

The POSIX platform (`platforms/posix/platform.c`) is the simplest
implementation and a good reference when writing a new platform.

| Function | Implementation |
|----------|---------------|
| `platform_init` | `signal(SIGINT, handler)` to set up Ctrl-C interrupt |
| `platform_emit` | `fputc(byte, stdout)` |
| `platform_key` | `fgetc(stdin)` |
| `platform_key_ready` | `poll()` on stdin with zero timeout |
| `platform_fatal` | `exit(1)` |
| `platform_snapshot_read` | `fopen` + `fseek` + `fread` on a file per slot |
| `platform_snapshot_write` | `fopen` + `fseek` + `fwrite`, creating if needed |
| `platform_snapshot_erase` | `remove()`, ignoring ENOENT |
