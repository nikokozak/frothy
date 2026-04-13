# ADR-024: Native Address Profile (FROTH-Addr)

**Date**: 2026-03-07
**Status**: Accepted (design only — implementation deferred to post-workshop)
**Spec sections**: 3.1 (Values), FROTH-Base (bitwise ops, `@/!` mention in design intent), FROTH-Snapshot (no raw pointers persisted)

## Context

Froth uses 3-bit LSB tagging (ADR-004), which reduces the representable integer range to `CELL_BITS - 3` payload bits. On 32-bit cells, Numbers are ±268,435,455 (29-bit signed). On 16-bit cells, Numbers are ±4,096 (13-bit signed).

This is sufficient for the vast majority of Froth programs: loop counts, flags, pin numbers, byte values, field offsets, small lengths, and arithmetic. However, it cannot represent full-width machine addresses. On ESP32, memory-mapped peripheral registers live at addresses like `0x3FF44004` (30+ bits), which exceeds the 29-bit payload range. The RP2040 peripheral base is `0x40000000`, also out of range.

The spec's design intent section (line 54) states: "Froth is comfortable at the metal. If you want to poke registers directly, you can: expose `@/!` and bit ops." The `s.pack` word (FROTH-String) takes an `addr` parameter described as "a raw address (number)." Both assume that a Number can carry a machine address, which the tagging scheme prevents on the very targets Froth is designed for.

This creates a credibility gap: Froth claims to be an embedded-first language with direct hardware access, but cannot represent the addresses needed for that access.

### The core tension

Three design goals are in conflict:

1. **Bare-metal capability**: full-width addresses for register access and pointer math.
2. **Tagged-value infrastructure**: type safety and zero-cost type checks via tag bits.
3. **ATtiny-class scalability**: minimal memory overhead per stack slot.

Any two can coexist cleanly; all three cannot.

### Why this matters now (even though implementation is deferred)

The workshop target (Mar 15) uses FFI for all hardware access (`gpio.mode`, `gpio.write`, `ms`), which works fine with the current scheme. But the long-term language identity depends on resolving this: is Froth a scripting layer on top of C, or a tool that works *at* the hardware? The design decision should be made and documented before the language's public-facing spec implies capabilities it cannot deliver.

## Analysis of approaches

Two families of solution were analyzed in depth. The analysis involved consultation with multiple AI systems and independent review.

### Family A: Change the value representation (full-width Numbers everywhere)

#### A1. Parallel type tracking (separate type array)

Keep DS values untagged (full cell width). Maintain a parallel packed array of 3-bit type tags, one per stack position.

**Memory overhead**: 3 bits per slot. At 32 stack slots on a 32-bit target: 12 bytes (9% overhead vs. 100% for tagged unions). At 32 slots on a 16-bit target: 12 bytes (19% overhead).

**Pros**:
- Full machine-word integers. `0x3FF44004` is a plain value.
- Spec statement "Number is a machine word-sized signed integer" becomes literally true.
- Classic Forth-style address-as-number composes naturally with `+`, offsets, `@/!`.
- Architecturally clean: values are values, types are types.

**Cons**:
- Every push/pop/copy/perm/catch path must synchronize both the value stack and the tag sidecar. Code size and complexity increase across the entire VM.
- On byte-per-slot storage (simpler to implement), the overhead rises to 50% on 16-bit targets.
- Flash pressure on tiny MCUs: more instructions per operation.
- Does not solve the snapshot problem: if raw addresses are just Numbers, `save` cannot distinguish "safe integer 42" from "unsafe pointer 0x3FF44004." Stale pointers could be silently serialized and restored on different hardware.
- The CALL tag (ADR-009) is currently in the value-tag space; decoupling it is needed regardless, but this approach requires decoupling *all* internal token kinds from value tags.
- Blast radius: essentially a rewrite of the entire value-handling layer. Every file that touches values changes.

**Verdict**: The cleanest uniform runtime model, but a bad trade if the only real need is full-width addresses. The snapshot weakness is a genuine safety problem, not just a philosophical objection.

#### A2. Tagged union (struct per slot)

Each stack slot becomes `struct { froth_cell_tag_t tag; froth_cell_t value; }`.

**Pros**: Full integer range, clean C, no bit manipulation on values.

**Cons**: 2× memory per stack slot. On ATtiny with 2KB SRAM, this halves usable stack depth. Already rejected in ADR-004 for this reason. Same snapshot weakness as A1.

**Verdict**: Only viable if the target floor is raised to 32-bit MCUs. Not recommended.

#### A3. Configurable tag width (compile-time knob)

Make `FROTH_TAG_BITS` a compile-time option. ATtiny builds use 3 bits; ESP32 bare-metal builds use 1 bit (31-bit payload) or 0 bits (full width with sidecar).

**1-bit tagging analysis**: Numbers get 31 signed bits (max ~1 billion). ESP32 peripheral addresses at `0x3FF44004` ≈ 1.07 billion — just barely above 2^30. Addresses above `0x7FFFFFFF` are unreachable. Tantalizingly close but doesn't fully solve the problem.

**Pros**: Doesn't force one answer on all targets.

**Cons**: Two different Froth implementations with different edge behavior. Program portability between targets becomes murkier. All type-check macros become conditional. Maintenance tax.

**Verdict**: Complexity without full payoff. 1-bit tagging almost works but doesn't, and 0-bit degenerates to A1.

#### A4. Boxed overflow (heap-backed wide numbers)

When a number literal exceeds the payload range, allocate it as a heap object. Use a reserved tag (e.g., tag 6 after freeing CALL) as "BoxedInt." Payload bits become a heap offset to the full-width value.

**Pros**: Transparent to the user. Fast path for in-range numbers unchanged. Literals in quotations allocated once at definition time.

**Cons**: Introduces hidden allocation (compromises "no hidden allocation in hot paths"). Arithmetic ops gain branches for boxed operands. Philosophically confused: treats wide addresses as "numbers that didn't fit" rather than a semantically distinct concept. Same snapshot weakness as A1/A2: a boxed `0x3FF44004` is still just a number with no persistence safety.

**Verdict**: Least disruptive "full answer" but philosophically wrong. Addresses are not failed Numbers; they are a different kind of thing.

### Family B: Keep compact tags, add explicit address/handle story

#### B1. FFI-only hardware access (status quo)

All hardware interaction goes through C primitives. Froth passes logical identifiers (pin numbers, peripheral IDs); the C function maps to actual addresses internally.

**Pros**: Zero language changes. Works today. The SDK already wraps registers in HAL functions.

**Cons**: Loses the Forth interactive hardware exploration experience. Every new register needs a C function. Froth becomes a scripting layer, not a tool at the hardware. Not a credible long-term answer for a language that claims bare-metal capability.

**Verdict**: Acceptable stopgap for the workshop; not the long-term answer. Should not be the *only* story.

#### B2. Double-cell addresses

Classic Forth approach: represent 32-bit addresses as two cells on the stack. `2@`, `2!`, address composition words.

**Pros**: No representation changes. Classic Forth precedent (16-bit Forths routinely do this).

**Cons**: Awkward. Every address operation requires decomposition. Stack depth doubles for address work. Perm patterns for address manipulation are painful.

**Verdict**: Fallback option if everything else fails. Not recommended as the primary design.

#### B3. Base-offset addressing

Configurable base register. Froth works with offsets relative to a set base: `0x3FF40000 io-base!` then `0x4004 io@`.

**Pros**: Offsets fit in payload range easily. Practical for peripheral block access.

**Cons**: Not composable across multiple peripheral blocks. Less general.

**Verdict**: Could be a useful convenience layer *on top of* a more general address mechanism, but not sufficient alone.

#### B4. Explicit native address type (FROTH-Addr profile)

Keep the tagged fixnum core from ADR-004. Add a new first-class value type for native addresses/handles. Addresses are semantically distinct from Numbers: they represent machine locations, are non-persistable, and have their own operation vocabulary.

**Pros**:
- Best fit for small targets: the common path (fixnum arithmetic, flags, counters) stays cheap.
- Most ordinary Froth code only needs small integers. Full-width is a boundary concern.
- More honest on Harvard-architecture and quirky MCUs where "address" is genuinely not one universal integer domain.
- Native addresses are explicitly non-persistable, fitting the snapshot spec cleanly: `save` can refuse to serialize them rather than silently writing stale pointers.
- Room to grow: `Addr`, `ProgAddr`, `IOAddr`, opaque peripheral handles, DMA descriptors.
- Preserves all existing infrastructure. The tagged core doesn't change.

**Cons**:
- Loses "everything is just a number" simplicity.
- Needs dedicated words: width-specific loads/stores, offset addition.
- Needs a runtime representation for full-width values on small-cell builds (the hard part).
- The FFI API (ADR-019) needs widening to support pushing/popping address values.

**Verdict**: The best-rounded embedded design. Fixes the real problem without taxing every stack slot.

## Decision

**Option B4: Explicit native address type as an optional FROTH-Addr profile.**

The deciding factors:

1. **The problem is a boundary concern, not a general arithmetic concern.** Full-width values are needed for machine addresses and hardware handles, not for loop counters or array indices. Taxing every stack slot to solve a boundary problem is the wrong trade-off on tiny targets.

2. **Addresses are semantically different from Numbers.** A machine address is a capability — a handle to a location. Making it a distinct type means `+` on two addresses is a type error (meaningless), while `addr offset @32` is well-typed. The type system works *for* you.

3. **Snapshot safety.** The snapshot spec (Section 5) mandates "no raw pointers persisted." If addresses are just Numbers, the serializer cannot distinguish safe integers from unsafe pointers. A distinct NativeAddr type is non-persistable by construction. `save` fails loudly if overlay slots reference native addresses, rather than silently serializing stale pointers that corrupt state on restore.

4. **Preserves the existing infrastructure.** The tagged value core (ADR-004), the tagging macros, the FFI API, the evaluator, the executor — all stay as-is for FROTH-Core. FROTH-Addr is an optional, strictly additive profile.

5. **Honest embedded design.** On small MCUs, "address" is not one universal integer domain. Harvard architectures separate code and data addresses. MMIO, SRAM, and flash may occupy different address spaces with different access rules. A distinct address type reflects this reality rather than papering over it.

### Prerequisite: Decouple CALL from value-tag space

ADR-009 currently assigns tag 6 to `FROTH_CALL`, an internal token type used only inside quotation bodies to distinguish "invoke this slot" from "push this SlotRef." This is spending a value-layer tag on an executor-layer concern. The CALL/literal distinction should be encoded differently — for example:

- A flag bit in the quotation body cell (e.g., the sign bit or a dedicated high bit, with the payload in the remaining bits).
- A separate token-kind byte preceding each cell in the quotation body.
- A bitmap alongside the quotation body (1 bit per token: 0=literal, 1=call).

The specific encoding is a future ADR. The consequence relevant here is that freeing tag 6 makes room for `FROTH_NATIVE_ADDR` without consuming the last reserved tag (7).

## FROTH-Addr profile specification (design)

### Value type: NativeAddr

A **NativeAddr** is an opaque, non-persistable value representing a full-width machine address or hardware handle.

- NativeAddr values occupy one DS slot (tagged with `FROTH_NATIVE_ADDR`, tag TBD after CALL decoupling).
- NativeAddr values are **non-persistable**: `save` MUST refuse to serialize any overlay slot whose implementation transitively references a NativeAddr literal. Implementations SHOULD signal `ERR.SNAP.NONPERSIST`.
- NativeAddr equality is defined as bitwise equality of the underlying full-width value.
- NativeAddr values MUST NOT participate in Number arithmetic (`+`, `-`, `*`, `/mod`). Passing a NativeAddr to a Number-only word MUST signal `ERR.TYPE`.

### Representation: Fixed address pool (R1)

The VM maintains a fixed-size array of full-width address values (`uintptr_t` or `uint32_t`), compile-time sized:

```c
#ifndef FROTH_ADDR_POOL_SIZE
#define FROTH_ADDR_POOL_SIZE 32
#endif

typedef struct {
    uintptr_t values[FROTH_ADDR_POOL_SIZE];
    uint16_t count;      // next-free index (bump allocator)
    uint16_t watermark;  // for region-based reclamation
} froth_addr_pool_t;
```

**Memory cost**: `FROTH_ADDR_POOL_SIZE × sizeof(uintptr_t) + 4` bytes. At 32 entries on a 32-bit target: 132 bytes. On a 16-bit target with 16 entries: 36 bytes. On ATtiny: the profile is simply not enabled.

**Tag encoding**: NativeAddr cells use the freed tag slot (formerly CALL). The payload is the pool index (0..FROTH_ADDR_POOL_SIZE-1). With 29 payload bits, the pool index range is not a constraint.

**Lifetime**: Pool entries are reclaimed via FROTH-Region integration:
- `mark` records the current pool `count` alongside the heap watermark.
- `release` resets the pool `count` to the marked value, invalidating later entries.
- Constants defined at boot (e.g., peripheral base addresses) live forever in the pool. This is fine — they're small and stable.
- A future enhancement could add explicit `addr.free` or refcount semantics, but the bump-allocator-with-regions model is sufficient for the expected workload of "a handful of base addresses plus small offsets."

### Required words (FROTH-Addr)

#### Address creation

- `addr.pack ( hi lo -- addr )`
  Combine two Numbers into a NativeAddr: `value = (hi << 16) | (lo & 0xFFFF)`. Allocates a pool entry.
  On 32-bit cells: `hi` is the upper 16 bits, `lo` is the lower 16 bits.
  On 16-bit cells: `hi` and `lo` are each 8 bits (or implementation-defined split).

- Board packages SHOULD provide named address constants as FFI words:
  ```c
  FROTH_FFI(gpio_base, "gpio.base", "( -- addr )", "GPIO peripheral base address") {
      return froth_push_addr(froth_vm, 0x3FF44000);
  }
  ```
  This is the expected primary way addresses enter Froth programs.

#### Memory access (width-specific)

All memory access words take `( addr offset -- ... )` form. The address and offset are combined internally (`effective = pool[addr.index] + offset`). The offset is an ordinary Number (fixnum), which keeps struct/register walking cheap — no new pool entry needed per field access.

- `@8  ( addr offset -- byte )`   Read 8-bit unsigned value from `addr + offset`.
- `@16 ( addr offset -- half )`   Read 16-bit unsigned value from `addr + offset`.
- `@32 ( addr offset -- value )`  Read 32-bit value from `addr + offset`. On 32-bit cells, the result is a NativeAddr if it exceeds fixnum range, or a Number if it fits. (Implementation may always return NativeAddr for consistency.)
- `!8  ( value addr offset -- )`  Write low 8 bits of `value` to `addr + offset`.
- `!16 ( value addr offset -- )`  Write low 16 bits of `value` to `addr + offset`.
- `!32 ( value addr offset -- )`  Write `value` (Number or NativeAddr) to `addr + offset`.

**Volatile semantics**: All memory access words MUST use volatile reads/writes. This is non-negotiable for MMIO register access. On targets where volatile is not meaningful (POSIX host), it's harmless.

#### Address arithmetic

- `addr+ ( addr offset -- addr' )` Add a Number offset to a NativeAddr, producing a new NativeAddr. Allocates a pool entry. Use sparingly — prefer `( addr offset @32 )` when possible.

- `addr.= ( addr1 addr2 -- flag )` Compare two NativeAddr values for equality.

#### Inspection

- `addr. ( addr -- )` Print the underlying address in hex (e.g., `0x3FF44004`). Useful for REPL exploration.

### FFI API additions

The FFI public API (ADR-019) needs two new functions:

```c
// Push a NativeAddr from C. Allocates a pool entry.
froth_error_t froth_push_addr(froth_vm_t *vm, uintptr_t addr);

// Pop a NativeAddr, returning the full-width value. Type-checked.
froth_error_t froth_pop_addr(froth_vm_t *vm, uintptr_t *addr);
```

These complement the existing `froth_push` (Number) and `froth_pop` (Number). The FFI author chooses the right one based on what they're working with.

### Interaction with FROTH-Checked

If FROTH-Checked is enabled alongside FROTH-Addr:
- The implementation MUST provide the built-in kind `K.ADDR`.
- Memory access words SHOULD have contracts requiring `K.ADDR` for the address argument and `K.NUMBER` for the offset.
- `addr+` SHOULD have a contract `k[addr number -- addr]`.

### Interaction with FROTH-Snapshot

NativeAddr values are **non-persistable by definition**:
- A NativeAddr literal inside a quotation body makes that quotation non-persistable.
- If an overlay-owned slot's implementation contains a NativeAddr literal, `save` MUST fail with `ERR.SNAP.NONPERSIST`.
- Addresses defined at boot via FFI (`gpio.base`) are base-layer primitives and are not overlay-owned, so they don't affect `save`.
- If a user defines `: my-gpio gpio.base ;` — this is persistable because `gpio.base` is a slot call, not a NativeAddr literal. The address is reconstructed at boot when `gpio.base` re-executes.

This is the correct behavior: addresses are *derived at runtime* from board-specific FFI words, not baked into snapshots.

### Example usage

```froth
\ Read GPIO output register on ESP32
gpio.base 0x04 @32 .        \ read GPIO_OUT_REG at base+4

\ Set bit 2 of GPIO output register
gpio.base 0x04 @32           \ read current value
4 or                         \ set bit 2
gpio.base 0x04 !32           \ write back

\ Walk a peripheral register block
gpio.base
  dup 0x00 @32 .  \ GPIO_OUT_REG
  dup 0x04 @32 .  \ GPIO_OUT_W1TS
  dup 0x08 @32 .  \ GPIO_OUT_W1TC
drop

\ Define a convenience word (persistable — no NativeAddr literals)
: gpio-out@  gpio.base 0x04 @32 ;
: gpio-out!  gpio.base 0x04 !32 ;
```

## Fallback representations (reference for future)

If the fixed pool proves insufficient, the following alternatives are documented for future consideration, in order of preference:

### Fallback F1: Heap-boxed native refs

Use the linear heap (same as QuoteRef/PatternRef) to store full-width address values. The tag payload becomes a heap offset instead of a pool index.

**When to consider**: If real workloads show significant address churn — linked-list walking, dynamic struct traversal, or DMA descriptor chains — that exhaust the fixed pool.

**Pros**: No pool size limit. Falls under existing FROTH-Region `mark`/`release` semantics naturally.

**Cons**: Heap allocation per address creation. Heap read per dereference. Philosophically uncomfortable given "no hidden allocation in hot paths," though for hardware register access the peripheral bus is the bottleneck, not the CPU.

**Migration path**: Replace `froth_addr_pool_t` with heap allocation in the `froth_push_addr` / `addr.pack` implementations. The tag encoding and word signatures stay identical. User code does not change.

### Fallback F2: Global sidecar tags (full-width payloads everywhere)

Approach A1 from the analysis: remove all LSB tagging, store full-width values on DS, and maintain a parallel packed type array.

**When to consider**: Only if the language identity fundamentally shifts away from "compact tagged fixnums" — e.g., if the ATtiny target is dropped, or if a compelling use case requires full-width arithmetic (not just addresses) throughout.

**Pros**: Uniform model. No special address type needed. Full machine-word Numbers everywhere.

**Cons**: Whole-VM rearchitecture. Memory overhead on every stack slot. Snapshot safety problem (no way to distinguish safe integers from unsafe pointers). Code size increase on flash-constrained targets.

**Migration path**: This is a rewrite, not a migration. It would require a new ADR superseding ADR-004, new `froth_types.h`, and changes to every file that touches values. Not recommended unless the design premises change fundamentally.

### Fallback F3: Double-cell addresses

Classic 16-bit Forth approach: addresses as two cells on the stack.

**When to consider**: Only as a last resort if the tag space cannot accommodate a NativeAddr type and neither F1 nor F2 is acceptable.

**Pros**: Zero representation changes. Works within the current system.

**Cons**: Awkward API. Stack depth doubles for address work. Perm patterns become painful. Every address operation is two operations.

**Migration path**: Define `2@`, `2!`, and address composition words in Froth or as FFI primitives. No changes to the value core.

## Spec consequences

The language spec should be updated:

1. **Section 3.1 (Values)**: Reword the Number definition. Currently: "a machine word-sized signed integer." This implies Numbers can hold any value that fits in the machine word, which is not true with tagged representation. Replace with language that describes Numbers as the VM's primary integer type with implementation-defined range, and note that full-width machine addresses are provided by the optional FROTH-Addr profile.

2. **Design intent (informative)**: The `@/!` mention should note that raw memory access requires the FROTH-Addr profile and is not available in FROTH-Core alone.

3. **FROTH-Base (Section 7)**: Arithmetic wrapping width `W` should be clarified as the *payload* width (implementation-defined), not necessarily the full cell width. This matches the actual implementation (ADR-011) and removes an ambiguity.

4. **New section**: FROTH-Addr profile specification, following the pattern of FROTH-String-Lite and FROTH-Region.

## Consequences

- The tagged value core (ADR-004) is preserved. No changes to existing value handling, arithmetic, or type checking.
- FROTH-Addr is optional and strictly additive. Targets that don't need native addresses (ATtiny, pure scripting) simply don't enable it. Zero cost when disabled.
- A prerequisite ADR is needed to decouple CALL from the value-tag space, freeing a tag for NativeAddr.
- The FFI API gains two functions (`froth_push_addr`, `froth_pop_addr`). Existing FFI bindings are unaffected.
- Board packages gain a natural place to define address constants as FFI words.
- The snapshot system gains a clear safety property: NativeAddr values are non-persistable by construction.
- The spec needs minor rewording to stop implying that Numbers are full machine words.
- Implementation is deferred to the post-workshop phase, targeting the ESP32 hardware deepening milestone.

## References

- ADR-004: Value tagging (3-bit LSB, preserved by this decision)
- ADR-009: CALL tag (to be superseded — CALL must move out of value-tag space)
- ADR-011: Wrapping arithmetic (payload width clarification)
- ADR-019: FFI public API (to be extended with addr push/pop)
- Spec Section 3.1: Value definition (to be reworded)
- Spec Section 7.1: Arithmetic wrapping width (to be clarified)
- Snapshot Spec Section 5: No raw pointers persisted (NativeAddr is non-persistable by construction)
- Spec design intent: `@/!` and bit ops mention (to be qualified)
