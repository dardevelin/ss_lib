# SS_Lib - Signal-Slot Library for C

[![Build](https://github.com/dardevelin/ss_lib/actions/workflows/build.yml/badge.svg)](https://github.com/dardevelin/ss_lib/actions/workflows/build.yml)
[![Benchmarks](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml/badge.svg)](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A lightweight, efficient signal-slot implementation for C, designed for embedded systems, game engines, and resource-constrained environments. Pure ANSI C with zero external dependencies.

## Quick Start

```c
#include "ss_lib.h"
#include <stdio.h>

void on_button_click(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    printf("Button clicked!\n");
}

int main(void) {
    ss_bus_t* bus = NULL;

    /* A bus holds signals and slots. Creating it also checks that this
     * program and the library agree on the public layouts. */
    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    ss_signal_register(bus, "button_click");
    ss_connect(bus, "button_click", on_button_click, NULL);

    ss_emit_void(bus, "button_click");

    ss_bus_destroy(bus);
    return 0;
}
```

Every function that uses library state takes the bus first. Buses are independent, so a program can have one per subsystem, and a test can start from a fresh bus. Coming from 2.x? See [Migrating from 2.x to 3.0](docs/migration-3.0.md).

## Installation

### Release Downloads

Each [GitHub release](https://github.com/dardevelin/ss_lib/releases) has the source as `.tar.gz` and `.zip` (builds with make or CMake as shipped), `ss_lib_single.h`, and `SHA256SUMS` to verify them (`sha256sum -c SHA256SUMS --ignore-missing`). The release notes are the version's [CHANGELOG](CHANGELOG.md) section.

### Installing

Install with either build system; both install the headers, the library, `ss_lib.pc` for pkg-config and the CMake package for `find_package`, so programs can use either one:

```bash
sudo make install                       # static and shared library, PREFIX=/usr/local
# or
cmake -B build -DSS_BUILD_SHARED=ON && cmake --build build && sudo cmake --install build
```

`make install` builds with the Makefile's `FEATURES`, CMake with its `SS_*` options. pkg-config and the CMake target hand those flags to your program, which must be compiled with the same ones (see [Configuration](#configuration)). Versions follow [semantic versioning](https://semver.org/): programs link against `libss_lib.so.3` (Linux) or `libss_lib.3.dylib` (macOS), and only a new major version changes that name. Use an absolute `PREFIX`: on macOS the installed dylib records its installed path, so programs find it without `DYLD_*` variables.

### Using pkg-config

```bash
cc your_app.c $(pkg-config --cflags --libs ss_lib)
```

### Using CMake

```cmake
find_package(ss_lib 3 REQUIRED)   # accepts any 3.x
target_link_libraries(your_app ss_lib::ss_lib)
```

`#include <ss_lib.h>` works with both; `<ss_lib/ss_lib.h>` also works with CMake.

### Using Homebrew (macOS, Linux)

```bash
brew install dardevelin/ss-lib/ss-lib
```

Use the full name: Homebrew 7 does not load a third-party tap's formula by its short name until the tap is trusted (`brew trust dardevelin/ss-lib`). The formula has no dependencies. On macOS 15 and 26 (Apple silicon) and Linux (x86_64 and arm64) it installs a prebuilt bottle; elsewhere, or with `--build-from-source`, it builds with `make`. It installs the static and shared library (thread safety and performance statistics enabled), `ss_lib.pc`, the CMake package and `ss_lib_single.h`. Compile with `pkg-config --cflags --libs ss_lib` or link `ss_lib::ss_lib` from `find_package` so your program uses the same configuration. Programs not yet migrated to 3.0 can install the latest 2.2.x with `brew install dardevelin/ss-lib/ss-lib@2`: it is keg-only, so point `PKG_CONFIG_PATH` or `CMAKE_PREFIX_PATH` at `$(brew --prefix dardevelin/ss-lib/ss-lib@2)` and use `find_package(ss_lib 2)`.

### Single Header

```bash
make single-header
# Writes include/ss_lib_single.h — copy it to your project
```

```c
/* In exactly one C file: */
#define SS_IMPLEMENTATION
#include "ss_lib_single.h"

/* Everywhere else, include it without SS_IMPLEMENTATION */
```

Configuration macros must be identical in every file that includes the header; pass them on the command line.

## Features

- **Zero Dependencies** — Pure ANSI C, no external libraries required
- **Independent Buses** — All state lives in a bus (`ss_bus_t`); no global state, no default bus
- **Static Memory** — Fixed arrays in built-in buses or caller memory; creating a bus, register, connect, emit, disconnect and destroy never touch the heap
- **Thread-Safe** — A bus created with `SS_BUS_THREAD_SAFE` has its own recursive mutex; slots may call back into the library
- **ISR-Safe** — Lock-free, allocation-free queueing from interrupt handlers, delivered from the main loop
- **Namespaces** — `ns::name` prefixes and an optional default namespace
- **Deferred Emission** — Queue signals (with a copy of their data) for later processing
- **Batch Operations** — Group multiple emissions into a single batch
- **Configurable** — Compile-time feature flags for minimal footprint
- **Single Header** — Easy integration with a single `#include`
- **Performance Profiling** — Built-in per-signal timing statistics
- **Memory Diagnostics** — Signal/slot usage, heap bytes held and peak
- **C89 to C2y** — Pure C; builds warning-free as every standard from C89 to C2y, with an alternate C89 source for legacy toolchains

## Configuration

### Compile-Time Flags

Configuration must be identical for the library and every file that includes `ss_lib.h`, so pass it on the command line (`-DSS_USE_STATIC_MEMORY=1`) rather than with `#define` in a source file. No option changes a public type layout, so a program that disagrees with the library on a feature flag fails to compile or link instead of corrupting memory. `ss_bus_create()` also returns `SS_ERR_CONFIG_MISMATCH` when a program sees a different `ss_data_t` than the library, for example through a header from another version.

| Flag | Default | Description |
|------|---------|-------------|
| `SS_USE_STATIC_MEMORY` | 0 | Use static allocation (no heap) |
| `SS_MAX_SIGNALS` | 32 | Maximum signals (static mode) |
| `SS_MAX_SLOTS` | 128 | Maximum slots (static mode) |
| `SS_MAX_SIGNAL_NAME_LENGTH` | 256 (32 static) | Name buffer size, including the terminator |
| `SS_MAX_DESCRIPTION_LENGTH` | 32 | Description buffer size (static mode) |
| `SS_STATIC_BUSES` | 1 | Static mode: built-in buses in `.bss` for `ss_bus_create()`; 0 keeps none (buses only from `ss_bus_create_in()` pools) |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Mutex support, for buses created with `SS_BUS_THREAD_SAFE` |
| `SS_ENABLE_INTROSPECTION` | 1 | Signal listing and discovery |
| `SS_ENABLE_CUSTOM_DATA` | 1 | Custom data types with cleanup |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Per-signal timing statistics |
| `SS_ENABLE_MEMORY_STATS` | 0 | Memory usage tracking |
| `SS_ENABLE_ISR_SAFE` | 0 | ISR-safe emission path |
| `SS_ENABLE_DEBUG_TRACE` | 0 | Debug trace output |
| `SS_DEFERRED_QUEUE_SIZE` | 64 | Deferred emission queue depth |
| `SS_DEFERRED_PAYLOAD_SIZE` | 32 | Bytes of string/custom data per deferred or batch entry (static mode) |
| `SS_BATCH_MAX_ENTRIES` | `SS_DEFERRED_QUEUE_SIZE` | Entries per batch |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR queue depth, per bus |
| `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` | undefined | Interrupt disable/restore for targets without atomic compare-and-swap (Cortex-M0, AVR) |

See the [Configuration Guide](docs/configuration.md) for all options.

### Preset Configurations

```bash
-DSS_MINIMAL_BUILD    # Strips all optional features
-DSS_EMBEDDED_BUILD   # Static memory, no threading, 10 slots per signal, deferred queue of 16
```

### Custom Allocators

```bash
-D'SS_MALLOC(size)=my_malloc(size)' -D'SS_FREE(ptr)=my_free(ptr)' \
-D'SS_CALLOC(count,size)=my_calloc(count,size)'
```

Strings are copied with `SS_MALLOC`, so these three route every allocation. `SS_STRDUP(str)` may also be defined to override string duplication.

### Platform Examples

#### Arduino/AVR (8-bit MCU)

```bash
-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=8 -DSS_MAX_SLOTS=16 -DSS_MINIMAL_BUILD
```

#### STM32/ARM Cortex-M

```bash
-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=32 -DSS_MAX_SLOTS=64 \
-DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_THREAD_SAFETY=0
```

#### Linux Embedded

```bash
-DSS_USE_STATIC_MEMORY=0 -DSS_ENABLE_THREAD_SAFETY=1 -DSS_ENABLE_PERFORMANCE_STATS=1
```

## API Overview

### Buses

Every function that uses library state takes `ss_bus_t* bus` as its first argument; the tables below leave it out where it is the only change. `ss_version()`, `ss_error_string()`, `ss_free_signal_list()`, the data functions and the batch functions other than `ss_batch_create()` take no bus.

| Function | Description |
|----------|-------------|
| `ss_bus_create(&bus, flags)` | Create a bus (heap in dynamic mode, a built-in bus in static mode); `flags` is 0 or `SS_BUS_THREAD_SAFE` |
| `ss_bus_create_in(&bus, pool, size, flags)` | Create a bus in caller memory, in either mode |
| `ss_bus_pool_size()` | Bytes `ss_bus_create_in()` needs, including alignment slack |
| `ss_bus_destroy(bus)` | Release the bus and everything it holds (deferred when called from a slot) |
| `ss_version()` | Runtime version string (`SS_VERSION_STRING`) |

Both create macros pass `SS_ABI_TAG` to `ss_bus_create_abi()` / `ss_bus_create_in_abi()`, which FFI callers can call directly.

### Signals

| Function | Description |
|----------|-------------|
| `ss_signal_register(name)` | Register a signal |
| `ss_signal_register_ex(name, desc, prio)` | Register with metadata |
| `ss_signal_unregister(name)` | Unregister and disconnect all |
| `ss_signal_exists(name)` | Check if signal exists |

### Connections

| Function | Description |
|----------|-------------|
| `ss_connect(signal, func, data)` | Connect slot to signal |
| `ss_connect_ex(signal, func, data, prio, handle)` | Connect with priority and handle |
| `ss_disconnect(signal, func)` | Disconnect specific slot |
| `ss_disconnect_handle(handle)` | Disconnect by handle |
| `ss_disconnect_all(signal)` | Disconnect all slots |

### Emission

| Function | Description |
|----------|-------------|
| `ss_emit(signal, data)` | Emit with custom data |
| `ss_emit_void(signal)` | Emit without data |
| `ss_emit_int(signal, value)` | Emit integer |
| `ss_emit_float(signal, value)` | Emit float |
| `ss_emit_double(signal, value)` | Emit double |
| `ss_emit_string(signal, str)` | Emit string |
| `ss_emit_pointer(signal, ptr)` | Emit pointer |
| `ss_emit_from_isr(signal, value)` | Queue an int event on the bus from an ISR (each bus has its own queue) |
| `ss_process_isr_queue(bus)` | Deliver the bus's queued ISR events (main loop) |

### Namespaces

| Function | Description |
|----------|-------------|
| `ss_set_namespace(ns)` | Set default namespace (NULL clears) |
| `ss_get_namespace(buf, size)` | Copy current namespace into `buf` (`""` when none) |
| `ss_emit_namespaced(ns, signal, data)` | Emit with namespace prefix |

### Deferred Emission

| Function | Description |
|----------|-------------|
| `ss_emit_deferred(signal, data)` | Queue signal for later |
| `ss_flush_deferred()` | Emit all queued signals |

### Batch Operations

| Function | Description |
|----------|-------------|
| `ss_batch_create(bus)` | Create a batch that emits on `bus` |
| `ss_batch_add(batch, signal, data)` | Add emission to batch |
| `ss_batch_emit(batch)` | Emit all in batch |
| `ss_batch_destroy(batch)` | Free batch |

### Data

| Function | Description |
|----------|-------------|
| `SS_DATA_INIT`, `ss_data_init(data)` | Empty data object (on the stack) |
| `ss_data_create(type)` | Allocate data object |
| `ss_data_clear(data)` | Free the payload the object owns |
| `ss_data_destroy(data)` | Clear and free a created data object |
| `ss_data_set_int/float/double/string/pointer(data, val)` | Set value (`string` copies) |
| `ss_data_set_string_ref(data, str)` | Set a borrowed string (no copy) |
| `ss_data_set_custom(data, ptr, size, cleanup)` | Set custom data (copies) |
| `ss_data_set_custom_ref(data, ptr, size)` | Set borrowed custom data (no copy) |
| `ss_data_get_int/float/double(data, default)` | Get with default |
| `ss_data_get_string/pointer/custom(data, ...)` | Get value |

### Introspection

| Function | Description |
|----------|-------------|
| `ss_get_signal_count()` | Number of registered signals |
| `ss_get_signal_list(list, count)` | List all signals |
| `ss_free_signal_list(list, count)` | Free signal list |

### Statistics

| Function | Description |
|----------|-------------|
| `ss_get_memory_stats(stats)` | Get memory usage |
| `ss_reset_memory_stats()` | Reset memory counters |
| `ss_get_perf_stats(signal, stats)` | Get signal timing |
| `ss_enable_profiling(enabled)` | Enable/disable profiling |
| `ss_reset_perf_stats()` | Reset timing counters |

### Error Handling

| Function | Description |
|----------|-------------|
| `ss_error_string(error)` | Error code to string |
| `ss_set_error_handler(handler, user_data)` | Set the bus's error callback, called with the failing bus, the error, the signal name and `user_data` |

The handler runs after the failing call releases the bus's lock, so it may call the library, and is never called once `ss_bus_destroy()` has been called on the bus:

```c
static void on_error(ss_bus_t* bus, ss_error_t error, const char* msg, void* user_data) {
    (void)bus;
    fprintf(stderr, "%s: %s: %s\n", (const char*)user_data, ss_error_string(error), msg);
}

ss_set_error_handler(bus, on_error, "audio bus");
```

### Configuration

| Function | Description |
|----------|-------------|
| `ss_set_max_slots_per_signal(max)` | Set slot limit |
| `ss_get_max_slots_per_signal()` | Get slot limit |

### Debug Trace

| Function | Description |
|----------|-------------|
| `ss_enable_trace(stream)` | Start trace output (`SS_ENABLE_DEBUG_TRACE`) |
| `ss_disable_trace()` | Stop trace output |

## Advanced Usage

### A Slot That Uses Its Bus

The slot signature has no bus parameter. A slot that needs its bus gets it through `user_data`:

```c
void on_saved(const ss_data_t* data, void* user_data) {
    ss_bus_t* bus = (ss_bus_t*)user_data;
    (void)data;
    ss_emit_string(bus, "status", "saved");
}

ss_connect(bus, "file_saved", on_saved, bus);
```

### Connection Handles

```c
ss_connection_t conn;
ss_connect_ex(bus, "data_changed", on_data, NULL, SS_PRIORITY_HIGH, &conn);

/* Later... */
ss_disconnect_handle(bus, conn);
```

### Signal Priorities

```c
/* Critical handlers execute first */
ss_connect_ex(bus, "alarm", on_critical, NULL, SS_PRIORITY_CRITICAL, NULL);
ss_connect_ex(bus, "alarm", on_log, NULL, SS_PRIORITY_LOW, NULL);
```

### Namespaces

```c
ss_signal_register(bus, "ui::button");

/* Emit using namespace helper */
ss_emit_namespaced(bus, "ui", "button", data);

/* Or set a default namespace: names without "::" resolve as "ui::name" */
ss_set_namespace(bus, "ui");
ss_signal_register(bus, "slider");      /* registers "ui::slider" */
ss_emit_void(bus, "slider");            /* emits "ui::slider" */
ss_emit_void(bus, "net::ping");         /* names with "::" are used as given */
ss_set_namespace(bus, NULL);            /* clear */
```

### Deferred Emission

```c
/* Queue signals during processing; the queue keeps a copy of the data */
ss_emit_deferred(bus, "update", data1);
ss_emit_deferred(bus, "render", data2);

/* Flush at end of frame */
ss_flush_deferred(bus);
```

### Batch Operations

A batch belongs to the bus it was created for; destroy it before the bus.

```c
ss_batch_t* batch = ss_batch_create(bus);
ss_batch_add(batch, "signal_a", data1);
ss_batch_add(batch, "signal_b", data2);
ss_batch_emit(batch);
ss_batch_destroy(batch);
```

### Performance Profiling

```c
ss_enable_profiling(bus, 1);

/* After running... */
ss_perf_stats_t stats;
ss_get_perf_stats(bus, "render_frame", &stats);
printf("Avg: %llu ns, Max: %llu ns\n",
       (unsigned long long)stats.avg_time_ns, (unsigned long long)stats.max_time_ns);
```

### Memory Diagnostics

```c
ss_memory_stats_t mem;
ss_get_memory_stats(bus, &mem);
printf("Signals: %zu/%zu\n", mem.signals_used, mem.signals_allocated);
printf("Slots: %zu/%zu\n", mem.slots_used, mem.slots_allocated);
```

### Embedded System (Static Memory)

```c
/* Built with: -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -DSS_MAX_SLOTS=32
 *             -DSS_ENABLE_THREAD_SAFETY=0 -DSS_ENABLE_ISR_SAFE=1
 * (the same flags for the library and this file) */
#include "ss_lib.h"

static ss_bus_t* bus;   /* set before the ADC interrupt is enabled */

void ADC_ISR(void) {
    uint16_t value = ADC->DATA;
    ss_emit_from_isr(bus, "adc_ready", value);   /* queued, no slot runs here */
}

int main(void) {
    ss_bus_create(&bus, 0);                 /* the built-in bus: no heap */
    ss_signal_register(bus, "adc_ready");
    ss_connect(bus, "adc_ready", on_adc_ready, NULL);
    enable_adc_interrupt();                 /* only once the bus exists */
    for (;;) {
        ss_process_isr_queue(bus);          /* slots run here */
    }
}
```

In static mode `ss_bus_create()` takes one of `SS_STATIC_BUSES` built-in buses (default 1) from `.bss`, and returns `SS_ERR_WOULD_OVERFLOW` when all are in use. `ss_bus_create_in(&bus, pool, size, 0)` places a bus in memory you provide instead.

### Thread Safety

```c
ss_bus_t* bus = NULL;
/* Decided at creation; needs a library built with SS_ENABLE_THREAD_SAFETY */
if (ss_bus_create(&bus, SS_BUS_THREAD_SAFE) != SS_OK) return 1;

/* Every call on this bus is now protected by its recursive mutex */
ss_signal_register(bus, "event");
ss_emit_void(bus, "event");
```

Slots run with the bus's mutex held and may call any library function; a slot must not wait for another thread that is itself calling the bus. A bus created without the flag has no mutex and must be used from one thread at a time. Buses share nothing, so threads with a bus each need no lock. A slot may call another bus, but cross-bus calls from slots must all go in one direction (A to B, never also B to A), or two threads can deadlock. Destroying a bus is not thread-safe with respect to other threads using it, and in static mode built-in buses must be created and destroyed from one thread.

## Building

### Standard Build

```bash
make          # Build library, tests and examples
make test     # Run tests
make examples # Build example programs
make help     # List all targets
```

The Makefile honours `CC` and compiles the library, tests, examples and benchmarks with the same `FEATURES` (default: `-DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1`).

### Embedded Build

```bash
gcc -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -c src/ss_lib.c -Iinclude
# compile every file that includes ss_lib.h with the same -D flags
```

### CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Options: `SS_BUILD_SHARED`, `SS_USE_STATIC_MEMORY` (with `SS_MAX_SIGNALS`, `SS_MAX_SLOTS`), `SS_ENABLE_THREAD_SAFETY`, `SS_ENABLE_PERFORMANCE_STATS`, `SS_ENABLE_ISR_SAFE`, `SS_ENABLE_SANITIZERS`, `SS_ENABLE_COVERAGE`, `SS_BUILD_TESTS`, `SS_BUILD_EXAMPLES`. The C standard defaults to C11; `-DCMAKE_C_STANDARD=90` (or 99, 17, 23) selects another. The feature definitions are exported with the `ss_lib::ss_lib` target, so consumers get the same configuration.

### Single Header

```bash
make single-header        # regenerate include/ss_lib_single.h
make check-single-header  # fail if it is stale or does not compile
```

## Testing

```bash
make test           # Run the test suite
make test-static    # ... with SS_USE_STATIC_MEMORY=1
make test-minimal   # ... with SS_MINIMAL_BUILD
make test-c89       # ... against src/ss_lib_c89.c built as C89
make test-newest    # ... built as the newest C standard the compiler supports
make test-single    # ... against the generated single header (dynamic and static)
make check-standards  # Compile as C89, C99, C11, C17, C23 and C2y with -Werror
make check-abi      # Public struct sizes, offsets and enum values identical in every configuration
make check-docs     # Every C code block of the README, docs and skills compiles
make test-all       # All of the above plus check-single-header, check-version, check-install
make test_embedded  # Embedded examples against an SS_EMBEDDED_BUILD library
make test-pauth     # arm64: library built with pointer authentication below Armv8.3
make cppcheck       # Static analysis, as CI runs it
make check-install  # make install into build/, build and run programs against it
                    # with pkg-config flags and find_package (if cmake is present), uninstall
make test-asan      # AddressSanitizer
make test-tsan      # ThreadSanitizer
make test-ubsan     # UndefinedBehaviorSanitizer
make test-msan      # MemorySanitizer (clang only)
make test-valgrind  # Valgrind memory check
make coverage       # Generate coverage report
```

`make test` includes regression tests named after the review findings (S1,
S2, ...) and `tests/test_reentrancy.c`, which runs 10,000 seeded random
programs whose slots unregister signals, destroy the bus, disconnect, emit and flush
while an emission is running. CI runs `test-all` on Linux (x86-64 and arm64)
and macOS, the sanitizers, valgrind and cppcheck on Linux, `test-pauth` on
Linux arm64, and the tests on Windows with MinGW and with MSVC (CMake).

## Benchmarks

Performance data from CI (dynamic memory, thread-safe):

### GitHub Actions - Ubuntu Latest (x86_64)

```
Basic operations:
- Signal registration: 1731 ns avg (60-18895 ns range)
- Slot connection: 2240 ns avg (2154-21079 ns range)
- Signal lookup: 185 ns avg (20-28442 ns range)
- Disconnect by handle: 39 ns avg (29-7534 ns range)

Signal emission:
- Empty signal: 30 ns avg (20-32640 ns range)
- With 1 slot: 31 ns avg (29-8435 ns range)
- With 5 slots: 34 ns avg (29-8706 ns range)
- With 10 slots: 39 ns avg (29-6984 ns range)
- Int data signal: 35 ns avg (29-7754 ns range)
- Priority slots: 39 ns avg (29-7294 ns range)
```

### GitHub Actions - macOS Latest (Apple Silicon)

```
Basic operations:
- Signal registration: 1060 ns avg (0-8000 ns range)
- Slot connection: 1116 ns avg (1000-20000 ns range)
- Signal lookup: 140 ns avg (0-146000 ns range)
- Disconnect by handle: 58 ns avg (0-6000 ns range)

Signal emission:
- Empty signal: 34 ns avg (0-39000 ns range)
- With 1 slot: 37 ns avg (0-31000 ns range)
- With 5 slots: 36 ns avg (0-42000 ns range)
- With 10 slots: 40 ns avg (0-10000 ns range)
- Int data signal: 36 ns avg (0-16000 ns range)
- Priority slots: 39 ns avg (0-4000 ns range)
```

### Embedded Reference (STM32F4 @ 168MHz, static memory, no threading)

```
Signal emission:
- Empty signal: ~89 ns
- With 1 slot: ~124 ns
- With 5 slots: ~287 ns

```

Code size on Cortex-M4 (arm-none-eabi-gcc -Os, `SS_EMBEDDED_BUILD` + `SS_MINIMAL_BUILD`, 16 signals, 32 slots, ISR queue): about 4.5 KB of text, and 3.9 KB of RAM for its one bus. RAM is one bus per `SS_STATIC_BUSES` in `.bss`, or a pool you pass to `ss_bus_create_in()` (`ss_bus_pool_size()` reports the size of a bus plus alignment slack; see the [Embedded Guide](docs/embedded-guide.md#memory-budget)).

View the [latest benchmark results](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml) or run locally:

```bash
make benchmark      # Quick benchmark
make benchmark-all  # Comprehensive suite
```

## Documentation

- [Getting Started](docs/getting-started.md)
- [API Reference](docs/api-reference.md)
- [Configuration Guide](docs/configuration.md)
- [Embedded Guide](docs/embedded-guide.md)
- [Performance Tuning](docs/performance.md)
- [Examples](docs/examples.md)
- [Architecture](docs/architecture.md)
- [Design Rationale](docs/design.md)
- [Migrating from 2.x to 3.0](docs/migration-3.0.md)

## LLM Skills (Claude Code)

This repository includes [Claude Code skills](https://docs.anthropic.com/en/docs/claude-code/skills) that teach LLMs to write and review SS_Lib code correctly. They live in `.claude/skills/` and activate automatically when relevant.

| Skill | Trigger | What it does |
|-------|---------|--------------|
| `ss-lib` | Code includes `ss_lib.h` or asks about SS_Lib | Complete API reference, lifecycle rules, string handling pitfalls, configuration, and canonical examples |
| `ss-lib-review` | Asks to review/debug SS_Lib code | 19-item checklist, 8 anti-patterns with corrections, feature-flag validation, memory model verification |

### Using the skills

**With Claude Code (CLI):** The skills activate automatically when you work on SS_Lib code in this repository. No setup needed — Claude Code reads `.claude/skills/` on its own.

**With other Claude Code projects that use SS_Lib:** Copy the `.claude/skills/ss-lib/` and `.claude/skills/ss-lib-review/` directories into your project's `.claude/skills/` folder.

**Invoking manually:** Type `/ss-lib` for API guidance or `/ss-lib-review` to review code for common mistakes.

## Security

Versions before 2.2.0 have use-after-free bugs when a slot unregisters its
signal, tears the library down (`ss_bus_destroy()` in 3.0), or queues a
deferred emission during a flush
([GHSA-f5pm-2465-f895](https://github.com/dardevelin/ss_lib/security/advisories/GHSA-f5pm-2465-f895)).
Upgrade to the latest release; if you copied `ss_lib_single.h` into your
project, replace it with the one from the latest release.

Report vulnerabilities privately, not in a public issue: see
[SECURITY.md](SECURITY.md).

## Contributing

1. Fork the repository
2. Create your feature branch
3. Ensure tests pass (`make test-all` and the sanitizer targets)
4. Submit pull request

See [CONTRIBUTING.md](CONTRIBUTING.md) for the rules on the C89 copy, the single header, versioning and releasing.

## License

MIT License - See [LICENSE](LICENSE) file for details.
