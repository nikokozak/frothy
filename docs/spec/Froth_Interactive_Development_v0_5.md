# Froth Interactive Development Specification

**Status:** Candidate profile specification
**Version:** 0.6 (2026-03-18)
**Profiles defined:** `FROTH-Interactive` (Direct/Link modes), `FROTH-REPL` (conventions)
**Scope:** Defines Froth’s on-device interaction model and optional host augmentation.
**Non-scope:** Does not modify FROTH-Core semantics.
**Changes from v0.5:** Link Mode updated from STX/ETX text framing to COBS binary framing (ADR-033). Interrupt semantics clarified for multiplexed console. See ADR-033, ADR-034.

## Foundational principle

**The device is the computer.** A person with a serial terminal and nothing else can write, test, modify, persist, and recover Froth programs. No host toolchain is required. Host-side tools augment the experience but never replace it.

This principle serves:

- **Longevity:** a Froth device found in 2045 can be reprogrammed with whatever serial terminal exists.
- **Autonomy:** the device is not a peripheral of the laptop.
- **Pedagogy:** the feedback loop (type → run → inspect → redefine) is unmediated.

---

## Modes

Froth defines two interaction modes. The device starts in **Direct Mode**. A host may request **Link Mode** via handshake. The device MUST remain usable in Direct Mode even when Link Mode is present.

### Direct Mode (default)

Direct Mode is the standard REPL.

**Behavior (normative):**
- The device reads bytes from the primary console stream and evaluates **complete expressions**.
- After each successfully evaluated top-level expression, the device prints the stack state (REPL Stack Visualization Protocol; see Section 5).
- Each top-level evaluation MUST be wrapped in an implicit `catch`:
  - On error, the error is printed and the VM returns to the prompt with stacks restored to their pre-eval depths.
- The device MUST remain in a usable state after any error (excluding hardware faults or explicit `wipe`).

**Example (bare terminal):**
```
Froth v0.5 | 187 slots | 32KB free
> 3 4 +
[7]
> : double ( x -- y ) x 2 * ;
ok
> 10 double
[20]
```


#### Recommended policy for temporary allocations (informative)

Froth intentionally avoids garbage collection. On embedded devices, the most common accidental “slow death” is unbounded heap growth caused by dynamic object construction (e.g., `q.pack`) inside long-running loops.

Recommended policies:

- If you implement **FROTH-Region**, encourage tooling and generators to use `mark`/`release` around temporary allocations.
- If you implement **FROTH-Region-Strict**, the system will fail fast when a program attempts dynamic allocation outside an active region. This is recommended on very small targets.
- Host tools operating in Link Mode MAY wrap multi-expression sends in an explicit region scope (e.g., send `mark` first, then definitions, then `release` only when discarding temporary code).

These policies keep the “device is the computer” workflow intact while making memory behavior more legible.


### Expression completeness and multi-line input

Direct Mode MUST handle multi-line input by accumulating bytes until the reader reports a complete expression.

**Completeness rules (normative):**
- Unclosed `[` … `]` quotations are incomplete.
- Unclosed `p[` … `]` pattern literals are incomplete.
- Unclosed `( ... )` comments are incomplete (if supported).
- Unclosed string literals (if supported) are incomplete.
- An implementation MAY add completeness rules but MUST NOT accept a syntactically incomplete expression as complete.

**UI recommendation:** While accumulating, the device SHOULD display a continuation prompt (e.g., `..`).

**Atomicity rule (normative):** If the completed expression is a definition that ultimately performs `def`, then the system MUST either:

- apply the definition entirely, or
- discard it entirely (no partial word).

This follows from Froth’s atomic `def` semantics.

### Link Mode (optional, host-augmented)

Link Mode adds reliable binary framing, structured responses, and definition-level tooling while preserving Direct Mode. The transport is defined in ADR-033 (FROTH-LINK/1).

#### Transport: COBS binary framing

Link Mode uses COBS (Consistent Overhead Byte Stuffing) encoding with `0x00` as the frame delimiter. This shares the serial line with Direct Mode without byte collisions: `0x00` never appears in console text, and raw bytes outside frames are ordinary Direct Mode traffic.

Wire format: `0x00` + COBS-encoded frame + `0x00`

Each frame has a 12-byte header (magic "FL", version, message type, request ID, payload length, CRC32) followed by a binary payload. The CRC covers the header (excluding the CRC field) and the payload.

#### Handshake

The host sends a `HELLO_REQ` frame (message type 0x01, no payload). The device replies with `HELLO_RES` (message type 0x02) containing cell width, heap size, slot count, version, and board name.

If the device does not support Link Mode, the `0x00` delimiters are ignored (no console meaning) and the COBS bytes pass harmlessly through the REPL.

#### Message types

| Type | Name | Direction | Purpose |
|------|------|-----------|---------|
| 0x01 | HELLO_REQ | host → device | Handshake |
| 0x02 | HELLO_RES | device → host | Device capabilities |
| 0x03 | EVAL_REQ | host → device | Evaluate source |
| 0x04 | EVAL_RES | device → host | Evaluation result |
| 0x07 | INFO_REQ | host → device | Query live state |
| 0x08 | INFO_RES | device → host | Heap, slots, version |
| 0x09 | RESET_REQ | host → device | Reset to stdlib baseline |
| 0x0A | RESET_RES | device → host | Post-reset state |
| 0xFF | ERROR | device → host | Protocol error |

#### Console multiplexer

A console multiplexer classifies incoming bytes:
- `0x00` starts COBS frame accumulation (Link Mode)
- `0x03` outside a frame sets the interrupt flag (Direct Mode)
- All other bytes outside a frame go to the REPL (Direct Mode)

Bytes inside a COBS frame are data, not control characters. A `0x03` inside a frame is just a data byte.

#### Flow control

Stop-and-wait: the host sends one request frame, waits for the response frame before sending the next. During evaluation, the device may emit console text (e.g., from `emit` or `.`) as unframed bytes. The host parser separates these from the framed response using the `0x00` delimiters.

#### Unframed input during Link Mode

Unframed bytes (not inside `0x00` delimiters) are still accepted as Direct Mode input. This allows mixing manual REPL use with tool-driven frames on the same serial connection.

---

## Interrupt / “Ctrl-C” semantics

### Interrupt byte
Raw `0x03` (Ctrl-C) outside a COBS frame sets a VM-local **interrupt flag**. Inside a COBS frame, `0x03` is data (the console multiplexer owns this distinction).

On platforms with signal support (POSIX), the signal handler sets the interrupt flag. On platforms without signals (ESP32), the byte is detected by the console multiplexer in direct mode, by `platform_check_interrupt` at executor safe points, and by the `key` primitive in console context.

### Safe points
The VM SHOULD check the interrupt flag at safe points:

- between token executions in the quotation interpreter,
- at each iteration of `while`,
- before returning to the prompt.

When the interrupt flag is observed, the VM MUST clear it and behave as if it executed `throw ERR.INTERRUPT`.

This provides consistent “stop runaway code” behavior in both Direct Mode and Link Mode. From a host tool, interrupt is sent as a raw `0x03` byte outside any frame.

---

## Persistence and boot behavior

Persistence is defined by the companion profile: **FROTH-Snapshot v0.5** (overlay dictionary model).

This spec defines *when* persistence actions occur; the snapshot format and restore semantics are defined in `Froth_Snapshot_Overlay_Spec_v0_5`.

### Required persistence words
A conforming FROTH-Interactive implementation MUST provide:

- `save` `( -- )` — atomic save of the current overlay
- `restore` `( -- )` — restore overlay from snapshot
- `wipe` `( -- )` — erase snapshots and return to base-only state

### Boot sequence (normative)
On power-up:

1. Initialize Froth VM.
2. Register base words/primitives and FFI.
3. Load stdlib.
4. Open safe boot window (see below). If triggered, skip steps 5-6.
5. If a valid snapshot exists: restore overlay (FROTH-Snapshot v0.5).
6. If `autorun` is bound: execute `[ 'autorun call ] catch`.
7. Enter Direct Mode prompt.

### Safe boot / autorun rescue (recommended)
To rescue from a bad `autorun` (infinite loop), implementations SHOULD provide at least one of:

- a hardware safe-boot strap/pin to skip autorun,
- a short “serial break window” where Ctrl-C (0x03) skips restore and autorun,
- a watchdog escape to prompt.

---

## Required on-device introspection

A self-sufficient device environment requires introspection.

### Required
- `words ( -- )` — list bound slot names
- `see ( slot -- )` — display slot definition (quote tokens or `<primitive>`)
- `.s ( -- )` — print data stack without modification
- `. ( n -- )` — print signed integer (no heap allocation; uses a small fixed scratch buffer internally)

### Recommended
- `cr ( -- )` — emit a newline (recommended convenience)
- `info ( -- )` — version, slot count, free heap, snapshot status
- `slot-info ( slot -- )` — contract, version, origin (base/overlay), impl kind
- `free ( -- n )` — free heap bytes
- `used ( -- n )` — used heap bytes

---

## REPL Stack Visualization Protocol (summary)

A Direct Mode evaluation SHOULD conclude by printing the data stack in a consistent, parseable format:

- `[v0 v1 ... vN]` where v0 is bottom, vN is top.

Implementations SHOULD format non-number values compactly:

- `<q:N>` quotation reference
- `<s:name>` slot reference
- `<p:N>` pattern reference

(Full details are in the unified Froth spec’s REPL/inspection section.)

---

## Host tooling (informative)

Host tools communicate with the device over FROTH-LINK/1 (ADR-033) via a daemon process (ADR-035) that owns the serial connection. The reference implementation includes:

- Go CLI (`froth-cli`): info, send, reset, interrupt, build, flash, daemon management
- VS Code extension: send selection/file, interrupt/reset/save/wipe buttons, device info sidebar, console output streaming
- Daemon: serial port ownership, device reconnection, JSON-RPC 2.0 for client access

Host tooling MUST be optional and MUST not be required for normal operation. A person with a serial terminal and nothing else can write, test, and persist Froth programs.