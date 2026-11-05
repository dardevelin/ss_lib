# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SS_Lib is a signal-slot library for C (v3.0.0) targeting embedded systems, game engines, and resource-constrained environments. Pure C with zero external dependencies. Supports dynamic and static memory allocation, optional thread safety, and ISR-safe signal emission. Versioning is semver: a minor release must keep every existing call compiling and every public struct layout unchanged.

## Build Commands

### Make (primary build system, used by CI)

```bash
make all              # library, tests (build/test_*), examples
make test             # test_ss_lib, test_simple, test_memory, test_isr,
                      #   test_reentrancy, test_threads
make test-all         # test + test-static + test-minimal + test-c89 + test-newest
                      #   + test-single (the suite against include/ss_lib_single.h)
                      #   + check-standards + check-abi (public layouts identical in
                      #     every configuration) + check-single-header + check-version
                      #   + check-install (make install into build/, use it via
                      #     pkg-config flags and find_package, uninstall)
                      #   (run before finishing)
make test-asan        # also test-ubsan, test-tsan, test-msan (clang), test-valgrind
make test_embedded    # embedded examples against an SS_EMBEDDED_BUILD library
make test-pauth       # arm64 only: library built with -march=armv8-a+pauth
                      #   -mbranch-protection=standard (see the GCC workaround
                      #   at the top of src/ss_lib.c); CI runs it on arm64
make cppcheck         # static analysis exactly as CI runs it
make single-header    # regenerate include/ss_lib_single.h
make benchmark        # also benchmark-all (benchmarks/run_benchmarks.sh)
make help
```

Every object is compiled with one `FEATURES` variable (default: ISR, memory and perf stats on). Variants are separate `BUILD_DIR`s, e.g. `make test BUILD_DIR=build/x FEATURES="-DSS_USE_STATIC_MEMORY=1"`, `IMPL=src/ss_lib_c89.c LIB_STD=-std=c89`, `SANITIZER=address|thread|undefined|memory`. `CC` from the environment is honoured.

**Tests** (no framework; plain `main()` + `assert()`; each test program undefines `NDEBUG` before `<assert.h>`, so the checks also run in CMake Release builds, and `make test` fails if a test program does not):
- `tests/test_ss_lib.c`: the suite, including regression tests named after the review findings (S1, S2...). Each test creates its bus(es) with `ss_bus_create()` / `ss_bus_create_in()` and destroys them with `ss_bus_destroy()`; buses share no state. To run one test, comment out the other calls in `main()`, then `make tests && ./build/test_ss_lib`.
- `tests/test_memory.c` (C89): `#include`s the implementation with `SS_MALLOC`/`SS_CALLOC`/`SS_FREE` routed to a tracking allocator; asserts no leaks, that every freed pointer came from those macros, and zero heap use in static mode.
- `tests/test_isr.c` (C89): `#include`s the implementation with `SS_ISR_CRITICAL_ENTER`/`EXIT` routed to a hook, so a test can "interrupt" `ss_process_isr_queue()` at an exact point.
- `tests/test_reentrancy.c`: seeded random programs whose slots unregister, clean up, disconnect, emit, queue deferred emissions, flush and run batches up to 4 emissions deep, checking every payload (GHSA-f5pm-2465-f895). It fails on 2.1.1 under ASan; replay one seed with `./build/test_reentrancy <seed>`. New code that lets a slot change library state must keep it passing under `make test-asan` and `make test-valgrind`.
- `tests/test_threads.c`: pthread stress test with thread safety on; meant for `make test-tsan`.

For bug fixes write the failing test first; use-after-free tests only fail under `make test-asan`.

### CMake (secondary, for distribution/installation)

```bash
cmake -B build/cmake -DSS_ENABLE_ISR_SAFE=ON && cmake --build build/cmake && ctest --test-dir build/cmake
```

ctest names: `test_basic`, `test_simple`, `test_memory`, `test_isr`, `test_reentrancy`, `test_single` and `test_reentrancy_single` (those two against the single header), `test_threads`. `CMAKE_C_STANDARD` defaults to 11 and can be overridden. Feature options are always passed as 0/1 and exported as PUBLIC compile definitions of `ss_lib::ss_lib`. The version comes from the header.

## Configuration Must Match Across Library and Callers

All configuration is compile-time macros in `include/ss_config.h`; they gate code and function declarations in `include/ss_lib.h`, but never a public type's layout or an enum value: no `#if` inside a public struct, union or enum body. `make check-abi` (`scripts/check_abi.sh`, in `test-all` and CI) enforces this by comparing every public struct's size and offsets and every enum value across all configurations. The `ss_bus_create()` / `ss_bus_create_in()` macros pass the caller's `SS_ABI_TAG` (`sizeof(ss_data_t)` and `SS_ABI_REVISION`; bump the revision when a public layout changes), so every bus creation returns `SS_ERR_CONFIG_MISMATCH` when caller and library disagree. Never `#define` configuration inside a source file (the examples take it from the command line). Public macros and signatures cannot be removed or turned into macros in a minor release: deprecate instead. Presets (`SS_MINIMAL_BUILD`, `SS_EMBEDDED_BUILD`) sit at the top of `ss_config.h` because other defaults depend on them.

## Architecture

Everything lives in `src/ss_lib.c`. All state is in buses (opaque `ss_bus_t`, `struct ss_bus`) that every stateful public function takes first and every internal function takes as `bus`; buses share nothing and there is no default bus. `ss_bus_create()` allocates the bus with `SS_CALLOC` in dynamic mode or claims one of `SS_STATIC_BUSES` built-in buses (`g_static_buses`, `.bss`, claimed without a lock) in static mode (`SS_ERR_WOULD_OVERFLOW` when all are used, never the heap); `ss_bus_create_in()` places it in a caller pool in either mode (NULL pool or too small is an error, no fallback). The only other global is the ISR queue. `docs/architecture.md` has the details.

- **Every public function** brackets its work with `bus_enter()` / `bus_leave()`: refuse a NULL bus, lock (if the bus has a mutex), refuse while closing, `call_depth` counting, deferred bus destruction, error-handler call after unlocking. New public functions must use this pair.
- **Thread safety:** per bus, chosen at creation: `SS_BUS_THREAD_SAFE` gives the bus its own recursive mutex (pthread recursive / CRITICAL_SECTION), held while slots run, so slots may call back into the library; without the flag the bus has no mutex. The flag without `SS_ENABLE_THREAD_SAFETY` is `SS_ERR_CONFIG_MISMATCH`. Cross-bus calls from slots must go in one direction only.
- **Storage interface** (`signal_create`, `signal_destroy`, `signal_next`, `live_next`, `slot_alloc`, `slot_release`) has a static-array and a heap implementation; outside it, code should not need `#if SS_USE_STATIC_MEMORY`. Static mode must never call the heap on core paths (`test_memory` enforces it).
- **Lookup is linear** (`strcmp` over the signal list/array). Slots are a singly-linked list per signal sorted by priority.
- **Nothing is freed while its signal emits.** Disconnects set `slot->removed` + `sig->dirty`; `sweep_removed_slots()` frees them when the signal is not emitting. Unregistering an emitting signal sets `sig->retired`; it is freed when the outermost emission ends. `ss_bus_destroy()` from a slot sets `closing` and the bus is destroyed when its `call_depth` returns to 0; after that the pointer is dangling. Any new code that removes slots or signals must go through `slot_remove` / `slots_remove_all` / this scheme.
- **Names** go through `resolve_name()` (storing: default namespace, length check) or `lookup_name()` (fast path when no namespace; too long means not found).
- **Deferred queue and batches** share one ring-buffer entry queue that copies payloads (strings, custom data) and pops each entry before emitting it (`queue_flush`), so slots may push or flush during a flush. Entries carry a push sequence number; a flush stops at entries pushed after it started.
- **ISR queue:** global fixed array shared by all buses, claimed with an atomic CAS shim (C11 / GCC builtins / MSVC / `SS_ISR_CRITICAL_ENTER/EXIT`), drained in sequence order by `ss_process_isr_queue(bus)` into the given bus; destroying a bus leaves queued events.

### Derived and parallel sources (keep in sync)

- `src/ss_lib.c` is written in the common subset of C89 and C11 (no `//`, declarations at block top, no variadic macros; trace calls use `SS_LIB_TRACE((bus, fmt, ...))`). `src/ss_lib_c89.c` is the C89 copy with an identical body: after editing `ss_lib.c` run `scripts/check_c89_sync.sh --fix`; CI runs the check.
- `include/ss_lib_single.h` is generated: run `make single-header` after changing the header, config or source; `make check-single-header` fails when it is stale.
- The version lives only in `include/ss_lib.h` (`SS_VERSION_*`, `@version`). Bump it there, add a `CHANGELOG.md` section, regenerate the single header; `make check-version` verifies. `scripts/version.sh` prints it for the Makefile, CMake (including the shared library's name, `libss_lib.so.MAJOR` / `libss_lib.MAJOR.dylib`; on macOS both installs give the dylib its absolute install path as install name) and scripts. `make install` and `cmake --install` install the same packages: `ss_lib.pc` and `lib/cmake/ss_lib` (CMake generates its files; `make install` fills `cmake/*.cmake.in`). `scripts/check_install.sh PREFIX` checks either, deriving the expected versions and the shared library name from the installed `ss_lib.pc`; CI runs it after each CMake install. Releases are tags `vX.Y.Z` on `main`; the release notes are the CHANGELOG section (`scripts/release_notes.sh`). The steps and the semver bump rules are in CONTRIBUTING.md under "Releasing".

### Project skills

`.claude/skills/ss-lib` (writing correct SS_Lib client code) and `.claude/skills/ss-lib-review` (reviewing and debugging SS_Lib usage). Load these when writing or reviewing code that uses the library.

## Coding Conventions

- **Naming:** functions `ss_verb_noun()`, types `ss_name_t`, enums `SS_ENUM_VALUE`, macros `SS_MACRO_NAME`. Extended variants use the `_ex` suffix (`ss_connect` / `ss_connect_ex`).
- **Errors:** return `ss_error_t` (`SS_OK` = 0). Errors are reported to the bus's handler by `bus_leave()`; don't call the handler elsewhere.
- **Style:** 4-space indentation, 100-character lines, opening brace on the same line. The library must stay warning-free with `-Wall -Wextra -pedantic -Werror` as every standard from C89 to the newest the compiler knows (C2y today) on gcc and clang: `scripts/check_standards.sh` (`make check-standards`) compiles the library, a caller of `ss_lib.h` and the single header in every configuration, also with `-Wstrict-prototypes` (an empty `()` means `(void)` only from C23). The project is C only; the header's `extern "C"` guards are kept for existing C++ callers but nothing is built or tested as C++.
- **Memory:** allocate only through `SS_MALLOC`/`SS_CALLOC`/`SS_FREE` and the internal `ss_strdup()`; never `malloc`/`strdup`/`strncpy` directly.
- **Performance:** emission is the hot path (`ss_emit` → `lookup_name` → `emit_locked`); measure before adding work there. `SS_INLINE` forces inlining except under `-Os`.
- **Commits:** present tense, first line at most 72 characters.
