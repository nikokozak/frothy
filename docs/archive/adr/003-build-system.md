# ADR-003: Build System — CMake, C11, System Compiler

**Date**: 2026-02-26
**Status**: Accepted
**Spec sections**: None (tooling decision)

## Context

We need to pick a build system, a C standard, and a compiler for host-native development. These choices also affect how easily we can integrate with ESP-IDF later (which uses CMake internally).

## Options Considered

### Build system

**Make**: simple, universally available, no learning curve. But manual dependency tracking is error-prone, and scaling to multiple targets (host, ESP32, RP2040) means maintaining parallel Makefiles or a hairy conditional one.

**CMake**: more setup upfront, but handles out-of-source builds, multiple targets, and toolchain files cleanly. ESP-IDF uses CMake as its build system, so the eventual port won't require switching tools. We're already familiar with it.

**PlatformIO**: nice for Arduino-style projects, but adds a layer of abstraction we don't need and would fight against when we want fine-grained control over the build.

### C standard

**C99**: maximum compatibility with vendor libraries and older toolchains. Missing a few useful C11 features.

**C11**: adds `_Static_assert` (great for compile-time checks on cell width, struct sizes), `_Alignof`, anonymous structs/unions, and `stdnoreturn.h`. All mainstream embedded compilers (GCC, Clang, even IAR) support C11 well at this point.

**C17/C23**: diminishing returns for our use case. C17 is basically C11 with defect fixes. C23 adds nice things but toolchain support on embedded targets is spotty.

### Compiler

For host development: whatever the system provides (Apple Clang on macOS, GCC on Linux). For ESP32: the ESP-IDF toolchain (Xtensa GCC or RISC-V GCC) — but that's a future concern.

## Decision

- **Build system**: CMake (minimum version 3.16 — old enough to be available everywhere, new enough for `target_sources` and modern CMake patterns).
- **C standard**: C11. The `_Static_assert` alone is worth it for a project where cell width is compile-time configurable — we want to catch size mismatches at build time, not at runtime. If we hit a vendor library that chokes on C11, we can isolate that library's headers behind an `extern "C"` / compatibility shim, but we're not going to hamstring the whole project for a hypothetical.
- **Compiler**: system default for host builds. No compiler-specific extensions in core VM code (i.e., stick to standard C11, no `__attribute__` in portable code unless behind a platform macro).

## Consequences

- Project structure needs a `CMakeLists.txt` at the root and probably under `src/`.
- We can use `_Static_assert` freely for things like: cell width matches expectations, struct sizes are what we think they are, tag bits fit in the cell width.
- ESP-IDF integration later should be straightforward since ESP-IDF already speaks CMake. We'll add a top-level `CMakeLists.txt` for the ESP-IDF component build alongside our host one.
- We commit to no compiler-specific extensions in the VM core. Platform-specific code (in `platform_*.c` files) can use target-appropriate extensions if needed.
- Anyone building this project needs CMake >= 3.16 and a C11-capable compiler. That's essentially every development machine shipped in the last decade.

## References

- ADR-001: cell width is compile-time configurable, which benefits from `_Static_assert`
- ADR-002: host-native first, so the immediate compiler is system Clang/GCC
- ESP-IDF build system docs: ESP-IDF uses CMake natively
