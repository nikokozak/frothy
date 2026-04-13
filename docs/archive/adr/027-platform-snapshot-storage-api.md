# ADR-027: Platform Snapshot Storage API

**Date**: 2026-03-11
**Status**: Accepted
**Spec sections**: Froth_Snapshot_Overlay_Spec_v0_5 §7 (A/B atomic scheme), §9 (save algorithm)

## Context

ADR-026 defined the snapshot serializer/deserializer and proved RAM round-trip. Stage 2 needs to persist snapshots to non-volatile storage across power cycles.

The platform layer currently provides only console I/O (`platform_emit`, `platform_key`, `platform_key_ready`). Snapshot persistence requires storage I/O, but the storage backend varies wildly across targets:

- **POSIX**: files (`fopen`/`fread`/`fwrite`)
- **ESP32**: NVS blobs or raw flash partitions
- **Battery-backed SRAM / FRAM**: direct memory-mapped access
- **Other MCUs**: SPI flash sectors, EEPROM

Files are the POSIX oddity, not the embedded norm. The platform abstraction must work for all of these without assuming a filesystem.

Three coupled decisions:
1. What API shape does the platform expose for storage?
2. How do boards opt in to persistence?
3. Who owns the slot capacity check?

## Decisions

### 1. Offset-based read/write/erase API

**Options considered:**

- **(A) File-oriented** (`platform_snap_open`, `platform_snap_close`, stream-based). Natural for POSIX, awkward for memory-mapped targets. Forces boards without filesystems to fake file semantics.
- **(B) Whole-buffer** (`platform_snap_read_all(slot, buf, max)`, `platform_snap_write_all(slot, buf, len)`). Simple but prevents reading just the header (50 bytes) without pulling the entire image.
- **(C) Offset-based** (`read(slot, offset, buf, len)`, `write(slot, offset, buf, len)`, `erase(slot)`). Works for all backends: files use `fseek`, flash uses base+offset, mapped memory uses `memcpy`.

**Decision: (C).** Three functions the platform must provide:

```c
froth_error_t platform_snap_read(uint8_t slot, uint32_t offset,
                                 uint8_t *buf, uint32_t len);
froth_error_t platform_snap_write(uint8_t slot, uint32_t offset,
                                  const uint8_t *buf, uint32_t len);
froth_error_t platform_snap_erase(uint8_t slot);
```

- `slot`: 0 = image A, 1 = image B.
- Reads/writes are absolute byte offsets within the slot's storage region.
- `platform_snap_erase` clears the entire slot (zeroes, `remove()`, NVS delete, sector erase — whatever is appropriate).
- Reads of nonexistent/empty slots MUST return an error (e.g., `FROTH_ERROR_IO`). The kernel treats a read error as "this slot is empty" during A/B selection. Bias toward explicit failure over silent zeroes.
- Platform implementations MUST NOT hold file handles or storage resources open across calls. Each `read`/`write`/`erase` is a self-contained operation (open, act, close). This keeps the API honest for targets where "open/close" has no meaning (memory-mapped, raw flash).

This lets the kernel read just the 50-byte header for A/B selection without loading the full payload.

### POSIX implementation notes

- Each slot maps to a file: `FROTH_SNAPSHOT_PATH_A` (slot 0) and `FROTH_SNAPSHOT_PATH_B` (slot 1), CMake-configurable, defaults `"froth_a.snap"` / `"froth_b.snap"`.
- `platform_snap_read`: `fopen("rb")`, `fseek` to offset, `fread`, `fclose`. Returns `FROTH_ERROR_IO` if file doesn't exist.
- `platform_snap_write`: `fopen("r+b")` (existing) or `fopen("w+b")` (new file), `fseek`, `fwrite`, `fclose`.
- `platform_snap_erase`: `remove()` the file.

### 2. Board opt-in via `FROTH_HAS_SNAPSHOTS` define

**Options considered:**

- **(A) Weak linking.** Platform storage functions declared with `__attribute__((weak))` stubs. If the board provides real implementations, they override. Risk: silent no-ops — a board author thinks persistence works but stubs resolve silently.
- **(B) Runtime capability query.** `platform_has_snap_storage()` returns bool. Requires every platform to define the function. Snapshot prims check at runtime — dead code remains linked.
- **(C) Compile-time `#define` with linker enforcement.** Board CMake sets `-DFROTH_HAS_SNAPSHOTS=1`. All snapshot code (`froth_snapshot_prims.c`, boot restore path, platform function declarations) guarded by `#ifdef FROTH_HAS_SNAPSHOTS`. If set but platform functions not defined → linker error tells the board author exactly what's missing.

**Decision: (C).** The opt-in mechanism is:

1. Board CMake sets `FROTH_HAS_SNAPSHOTS=1` (or omits it to opt out).
2. `platform.h` declares `platform_snap_read/write/erase` inside `#ifdef FROTH_HAS_SNAPSHOTS`.
3. `froth_snapshot_prims.c` compiles to empty if the define is absent.
4. `main.c` registers the snapshot primitive table only when defined:
   ```c
   #ifdef FROTH_HAS_SNAPSHOTS
   froth_ffi_register(&vm, froth_snapshot_prims);
   #endif
   ```
5. If a board sets the define but omits platform function definitions, the linker produces `undefined reference to platform_snap_write` — clear, immediate, no runtime surprises.

This mirrors how ESP-IDF handles optional features with `CONFIG_*` defines, and fits the existing Froth pattern of CMake-configurable values.

### 3. Slot capacity: kernel-owned check, compile-time default + optional runtime override

**Options considered:**

- **(A) Compile-time only.** `FROTH_SNAPSHOT_SLOT_SIZE` CMake var, default 2048. Kernel checks `header_size + payload_len <= FROTH_SNAPSHOT_SLOT_SIZE` before writing. Simple, but some targets discover storage capacity at runtime (NVS partition size, external flash geometry).
- **(B) Runtime only.** `platform_snap_slot_capacity()` function. Platform must always define it. Adds a required function that many simple platforms would just hardcode anyway.
- **(C) Compile-time default + optional runtime override.** `FROTH_SNAPSHOT_SLOT_SIZE` provides the default. If the platform also defines `platform_snap_slot_capacity()`, the kernel uses whichever is smaller. Boards with fixed storage use only the define. Boards with variable storage provide the function to report actual capacity.

**Decision: (C).** The kernel owns the safety check:

- `FROTH_SNAPSHOT_SLOT_SIZE` (CMake, default 2048) is always available.
- `platform_snap_slot_capacity()` is **optional** — declared only under a secondary define `FROTH_SNAP_RUNTIME_CAPACITY` that a board can set.
- The kernel's capacity check:
  ```c
  uint32_t capacity = FROTH_SNAPSHOT_SLOT_SIZE;
  #ifdef FROTH_SNAP_RUNTIME_CAPACITY
  uint32_t runtime_cap = platform_snap_slot_capacity();
  if (runtime_cap < capacity) capacity = runtime_cap;
  #endif
  if (HEADER_SIZE + payload_len > capacity) return FROTH_ERROR_SNAPSHOT_OVERFLOW;
  ```
- Board documentation should **highly recommend** providing `platform_snap_slot_capacity()` on targets where storage geometry is not fixed at compile time.

This way, small targets with unusual constraints can report exact capacity, large targets with fixed flash regions just set the CMake var, and the kernel never writes beyond what storage can hold.

## Consequences

- Platform layer gains 3 required functions (under `FROTH_HAS_SNAPSHOTS`) and 1 optional function (under `FROTH_SNAP_RUNTIME_CAPACITY`).
- Board contribution for persistence is: set one CMake define, implement three functions, done. Linker catches mistakes.
- Boards without storage support pay zero cost — no snapshot code compiled, no primitives registered, no storage functions required.
- The kernel never trusts the platform to enforce capacity — it always checks before writing.
- A/B selection logic, header parsing, CRC validation all live in kernel code, calling platform functions only for raw byte I/O.
- Future backends (FRAM, SPI flash, EEPROM) fit the offset-based API without changes to kernel code.

## References

- ADR-026: Snapshot persistence implementation (Stage 1)
- Spec: `docs/spec/Froth_Snapshot_Overlay_Spec_v0_5.md` §7, §9
- Existing platform pattern: `platform.h` / `platform_posix.c`
