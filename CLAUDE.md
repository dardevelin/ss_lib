# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SS_Lib is a signal-slot library for C (v2.1.0) targeting embedded systems, game engines, and resource-constrained environments. Pure ANSI C with zero external dependencies. Supports dynamic and static memory allocation, optional thread safety, and ISR-safe signal emission.

## Build Commands

### Make (primary build system)

```bash
make all              # Build library, tests, and examples
make lib              # Build static library only
make shared           # Build shared library
make test             # Build and run tests
make test_embedded    # Run embedded example
make benchmark        # Run benchmarks
make benchmark-all    # Comprehensive benchmarks
make single-header    # Generate single-header version
make clean            # Remove build artifacts
```

**Sanitizers and analysis:**
```bash
make test-asan        # AddressSanitizer
make test-tsan        # ThreadSanitizer
make test-ubsan       # UndefinedBehaviorSanitizer
make test-msan        # MemorySanitizer (clang only)
make test-valgrind    # Valgrind memory checks
make coverage         # Code coverage report (lcov)
```

### CMake (secondary, for distribution/installation)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

CMake options: `-DSS_BUILD_SHARED=ON`, `-DSS_USE_STATIC_MEMORY=ON`, `-DSS_ENABLE_THREAD_SAFETY=OFF`, `-DSS_ENABLE_SANITIZERS=ON -DSANITIZER_TYPE=address`

## Architecture

### Source Layout

- `include/ss_config.h` — Compile-time configuration macros (feature flags, memory limits, custom allocators)
- `include/ss_lib.h` — Public API header
- `src/ss_lib.c` — Main implementation
- `src/ss_lib_c89.c` — C89-compatible version
- `tests/test_ss_lib.c` — Comprehensive test suite
- `tests/test_simple.c` — Basic functionality tests

### Core Design

**Signal Registry:** Hash table with linked-list collision resolution for O(1) signal lookup. Signals are registered by name (string) and looked up via hash.

**Slot Lists:** Doubly-linked lists per signal, sorted by priority at connection time. Connection handles are opaque `uintptr_t` values mapping directly to slot nodes.

**Data Payload:** Union-based `ss_data_t` type supporting int, float, double, string, pointer, and custom data. Type-specific emit helpers (`ss_emit_int`, `ss_emit_float`, etc.) avoid manual data construction.

**Two Memory Models:**
- Dynamic (default): standard malloc/free, grows as needed
- Static (`SS_USE_STATIC_MEMORY`): pre-allocated fixed-size pools, no heap fragmentation — for embedded use

**Thread Safety:** Single global mutex (pthread on Unix, Critical Section on Windows), opt-in via `SS_ENABLE_THREAD_SAFETY`. Lock-free ISR emission path available separately via `SS_ENABLE_ISR_SAFE`.

### Configuration System (`ss_config.h`)

Features are controlled via compile-time `#define` flags. Key flags:
- `SS_ENABLE_THREAD_SAFETY` (default: 1), `SS_ENABLE_INTROSPECTION` (1), `SS_ENABLE_CUSTOM_DATA` (1)
- `SS_ENABLE_PERFORMANCE_STATS` (0), `SS_ENABLE_MEMORY_STATS` (0), `SS_ENABLE_ISR_SAFE` (0)
- Preset configs: `SS_MINIMAL_BUILD` strips all optional features; `SS_EMBEDDED_BUILD` enables static memory and disables threading

Custom allocators can be injected via `SS_MALLOC`, `SS_FREE`, `SS_CALLOC`, `SS_STRDUP` macros.

## Coding Conventions

- **Naming:** functions `ss_verb_noun()`, types `ss_name_t`, enums `SS_ENUM_VALUE`, macros `SS_MACRO_NAME`
- **Extended API pattern:** `_ex` suffix for extended versions (e.g., `ss_connect` vs `ss_connect_ex`)
- **Error handling:** all functions return `ss_error_t`; `SS_OK` (0) for success, `SS_ERR_*` for errors
- **Style:** 4-space indentation, 100-char line limit, opening braces on same line
- **C standard:** C11 primary, C89 compatibility version exists in `src/ss_lib_c89.c`
- **Commit messages:** present tense, 72-char first line limit

## Build Configuration Notes

The default Makefile build enables all features (`SS_ENABLE_ISR_SAFE=1`, `SS_ENABLE_MEMORY_STATS=1`, `SS_ENABLE_PERFORMANCE_STATS=1`) for testing. Production builds should configure features explicitly via `ss_config.h` or `-D` flags.

## Platform Support

Linux (GCC, Clang), macOS (Clang), Windows (MinGW, MSVC), embedded ARM (Cortex-M), AVR/Arduino. CI runs multi-platform builds including ARM cross-compilation.
