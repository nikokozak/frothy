# Target Tiers and Tethered Mode

## Target tier model

Froth targets three tiers of hardware, each with a different strategy:

### Tier 1: 32-bit (ESP32, RP2040, STM32, etc.)

Full Froth. Interactive REPL, all features enabled. The entire image is under 35KB
of code with a few KB of RAM for stacks/heap. These targets have flash and RAM to
spare. Portability work here is writing platform backends (UART, timers, storage),
not shrinking the system.

Memory optimization (tuning stack depths, heap size, slot count via CMake) is
sufficient for all 32-bit targets.

### Tier 2: 16-bit (MSP430, larger AVR, etc.)

Memory optimization plus selective feature gating. May require tethered mode for
the most constrained parts. 16-bit cells give 13-bit signed payload (-4096 to
+4095). Harvard architecture targets (AVR) need platform-layer work for flash
reads (PROGMEM).

### Tier 3: 8-bit (ATtiny85, PIC, etc.)

Tethered mode only. The target runs a minimal firmware: executor, stacks, heap,
slot table, snapshot reader (for standalone boot), serial protocol handler, and
hardware primitives. Total target firmware: ~6-7KB code, few hundred bytes RAM.

The host (laptop) runs full Froth with a platform backend that forwards execution
commands over serial instead of executing locally. During development, the
experience is fully interactive. For deployment, the host snapshots the target
state to flash and disconnects.

## Tethered Forth model

Split the Forth system across two machines connected by serial (UART):

**Host (laptop):** outer interpreter. Text parsing, dictionary lookup, name
resolution, number parsing, compilation. All "heavy" text machinery.

**Target (MCU):** inner interpreter only. Token dispatch, execution, stacks,
heap, hardware access. No reader, no evaluator, no REPL, no string parsing.

**Serial protocol** bridges them. Commands:
- Push tagged cell onto target data stack
- Execute word at slot index N
- Read back top of stack / stack depth
- Allocate quotation body on target heap
- Register name at slot index
- Snapshot to flash (cold boot image)
- Error reporting back to host

**Existing Froth pieces that map onto this:**
- Snapshot writer/reader = the "freeze/thaw" step
- Executor = the standalone target runtime
- Platform abstraction = where the serial backend plugs in
- VM API (push, execute, allocate) = the protocol command set

**What's missing:** the serial protocol itself, and a host-side platform backend
that forwards to a connected target instead of executing locally.

## Architectural constraints

To keep the tethered path open:

1. Keep the executor a clean inner loop. Don't add operations that bypass the
   normal VM interface (push/pop/execute).
2. Keep the VM's internal API minimal and composable. The serial protocol should
   map directly onto existing VM operations.
3. Don't hardcode assumptions about flat address spaces in core code. The platform
   layer handles memory topology differences.
4. Don't add host-only dependencies (filesystem, etc.) to the core VM. Everything
   the executor touches must be portable to bare metal.

## Timeline

Not on the current roadmap. This is a future capability that requires:
- Feature gating infrastructure (for tier 2/3 builds)
- Harvard architecture platform layer (for AVR targets)
- Serial protocol design and implementation
- Host-side "target proxy" platform backend
- A 16-bit cell compilation and test pass

Current focus: tier 1 (32-bit) ecosystem and tooling.
