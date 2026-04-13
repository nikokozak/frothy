# ADR-014: Compile-Time Embedded Standard Library

**Date**: 2026-03-04
**Status**: Accepted
**Spec sections**: 5.11 (perm examples), 11 (FROTH-Stdlib)

## Context

`perm` and `pat` are now implemented as C primitives. The standard shuffle words (`dup`, `swap`, `drop`, `over`, `rot`, `-rot`, `nip`, `tuck`) should be defined as Froth library words using `perm`, not as additional C primitives — that's the whole point of having a canonical stack-rewrite primitive.

These definitions need to be loaded into the VM before the REPL starts. The question is how to get `.froth` source files into the binary.

Constraints:
- Must work on bare-metal targets (ESP32, RP2040, ATTiny) with no filesystem.
- Community contributors should be able to write and share `.froth` library files without touching C.
- The source of truth for stdlib definitions should be a `.froth` text file, not C strings scattered through the codebase.
- The evaluator takes null-terminated `char*` input. Changing to pointer+length would require modifying both the evaluator and reader — not justified yet.
- The embedding mechanism should be cross-platform with no external tool dependencies beyond CMake itself.
- Newcomers should be able to contribute libraries by dropping `.froth` files in a folder.

## Options Considered

### Option A: C string literals in main.c

Hardcode definitions as `froth_evaluate_input("'dup [ 1 p[a a] perm ] def", &vm)` calls.

Trade-offs:
- Pro: zero infrastructure, works immediately
- Con: stdlib source is trapped in C code, not shareable as `.froth` files
- Con: escaping, line breaks, and readability degrade as definitions grow
- Con: contributors must edit C files

### Option B: Compile-time embedding via xxd

`xxd -i lib/stdlib.froth` generates a C header containing the file as `unsigned char[]` with a length variable.

Trade-offs:
- Pro: stdlib is a normal `.froth` file
- Pro: no filesystem needed at runtime
- Con: `xxd` is an external tool dependency — ships with vim, generally available on Linux/macOS, but not guaranteed on all platforms (Windows without WSL, minimal CI images, ESP-IDF Docker containers)
- Con: does not null-terminate the array — requires either using the length variable (which means changing the evaluator/reader interface) or post-processing the output
- Con: adds a tooling dependency that contributors must have installed

### Option C: Compile-time embedding via CMake file(READ)

CMake's built-in `file(READ ... HEX)` reads a file as a hex string. A CMake function reformats this into a C header containing a null-terminated `const char[]`. No external tools — pure CMake.

Trade-offs:
- Pro: stdlib is a normal `.froth` file
- Pro: no filesystem needed at runtime
- Pro: zero external tool dependencies — works everywhere CMake works
- Pro: null termination is trivially handled by appending `0x00` to the generated array
- Pro: cross-platform by definition (Windows, macOS, Linux, CI, ESP-IDF)
- Con: editing `stdlib.froth` requires a rebuild (~2s on POSIX, acceptable)
- Con: CMake string manipulation is ugly — but contained to one function, written once

### Option D: Runtime file loading

Load `.froth` files from disk at startup.

Trade-offs:
- Pro: no rebuild needed to test stdlib changes
- Con: requires a filesystem — does not work on bare-metal targets
- Con: two loading paths to maintain if also needed on embedded
- Con: not viable as the primary mechanism for an embedded-first language

## Decision

**Option C: Compile-time embedding via CMake `file(READ)`.**

The deciding factors:

1. **Zero external dependencies.** No `xxd`, no Python, no scripts. If CMake runs, this works. This is the strongest cross-platform guarantee we can offer.

2. **Null termination is trivial.** The CMake function controls the output format, so appending `0x00` to the array is one line. No need to change the evaluator or reader interface.

3. **Source of truth is `.froth`.** Contributors write Froth, not C. The file is readable, testable, and shareable.

4. **Replaceable.** The interface to the rest of the system is just `const char*`. If the build system changes or a package manager emerges, the embedding mechanism can be swapped without touching the evaluator.

5. **Rebuild cost is acceptable.** The stdlib is small. A 2-second rebuild is not a bottleneck. If it becomes one later, a runtime loading path for development (Option D) can be added incrementally.

## Implementation Detail

### CMake function

A CMake function `embed_froth_file(input output varname)`:
1. Reads the input `.froth` file as hex via `file(READ "${input}" hex HEX)`
2. Reformats the hex string into comma-separated `0xNN` bytes
3. Appends `0x00` for null termination
4. Writes a C header: `static const char ${varname}[] = { ... };`

### File layout

- `.froth` source files live in `src/lib/`
- Generated headers go to `${CMAKE_BINARY_DIR}/generated/`
- One generated header per `.froth` file (simpler CMake, cleaner dependencies)
- A single registry header `froth_stdlib.h` includes all generated headers and provides an initialization function

### Naming convention

`src/lib/core.froth` → variable name `froth_lib_core`, generated header `froth_lib_core.h`.

### Startup sequence

```c
int main() {
    froth_primitives_register(&froth_vm);
    froth_stdlib_load(&froth_vm);   // evaluates all embedded .froth libs
    froth_repl_start(&froth_vm);
    return 0;
}
```

## Consequences

### What becomes easier

- Adding new stdlib words: edit a `.froth` file in `src/lib/`, rebuild, done.
- Sharing libraries: `.froth` files are the canonical distribution format.
- Testing stdlib in isolation: feed the file to the evaluator in a test harness.
- Cross-platform builds: no tool installation beyond CMake.

### What becomes harder

- Nothing significant. The CMake function is ~10 lines, written once.

### Constraints on future decisions

- `froth_evaluate_input` signature should change to `const char*` — the evaluator doesn't modify its input.
- If the stdlib grows large or iteration speed matters, a runtime loading path (Option D) can supplement the embedded path without replacing it.
- A future package manager will need its own delivery mechanism (manifest files, flash partitions, etc.). This ADR covers boot-time stdlib embedding only — it does not constrain the package story.

## References

- Froth Language Spec v1.1, Sections 5.11, 11
- ADR-012 (perm TOS-right reading direction — defines the canonical shuffle patterns)
- ADR-013 (PatternRef byte encoding)
- CMake `file(READ)` documentation: https://cmake.org/cmake/help/latest/command/file.html#read
- Mecrisp Forth: embeds Forth source in flash at compile time (prior art)
