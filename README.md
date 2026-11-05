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
    printf("Button clicked!\n");
}

int main(void) {
    /* ss_init() also works; ss_init_abi() additionally detects a library
     * built with different configuration flags */
    if (ss_init_abi(SS_ABI_TAG) != SS_OK) return 1;

    ss_signal_register("button_click");
    ss_connect("button_click", on_button_click, NULL);

    ss_emit_void("button_click");

    ss_cleanup();
    return 0;
}
```

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

`make install` builds with the Makefile's `FEATURES`, CMake with its `SS_*` options. pkg-config and the CMake target hand those flags to your program, which must be compiled with the same ones (see [Configuration](#configuration)). Versions follow [semantic versioning](https://semver.org/): programs link against `libss_lib.so.2` (Linux) or `libss_lib.2.dylib` (macOS), and only a new major version changes that name. Use an absolute `PREFIX`: on macOS the installed dylib records its installed path, so programs find it without `DYLD_*` variables.

### Using pkg-config

```bash
cc your_app.c $(pkg-config --cflags --libs ss_lib)
```

### Using CMake

```cmake
find_package(ss_lib 2 REQUIRED)   # accepts any 2.x
target_link_libraries(your_app ss_lib::ss_lib)
```

`#include <ss_lib.h>` works with both; `<ss_lib/ss_lib.h>` also works with CMake.

### Using Homebrew (macOS, Linux)

```bash
brew install dardevelin/ss-lib/ss-lib
```

Use the full name: Homebrew 7 does not load a third-party tap's formula by its short name until the tap is trusted (`brew trust dardevelin/ss-lib`). The formula has no dependencies: it builds with `make` and installs the static and shared library (thread safety and performance statistics enabled), `ss_lib.pc`, the CMake package and `ss_lib_single.h`. Compile with `pkg-config --cflags --libs ss_lib` or link `ss_lib::ss_lib` from `find_package(ss_lib 2)` so your program uses the same configuration.

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
- **Static Memory** — Fixed arrays; register, connect, emit, disconnect and cleanup never touch the heap
- **Thread-Safe** — Optional recursive mutex with runtime toggle; slots may call back into the library
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

Configuration must be identical for the library and every file that includes `ss_lib.h`, so pass it on the command line (`-DSS_USE_STATIC_MEMORY=1`) rather than with `#define` in a source file. `ss_init_abi(SS_ABI_TAG)` (recommended for new code instead of `ss_init()`) returns `SS_ERR_CONFIG_MISMATCH` when a program was built with a different `ss_data_t` layout than the library (`SS_ENABLE_CUSTOM_DATA`); other mismatches are not detected.

| Flag | Default | Description |
|------|---------|-------------|
| `SS_USE_STATIC_MEMORY` | 0 | Use static allocation (no heap) |
| `SS_MAX_SIGNALS` | 32 | Maximum signals (static mode) |
| `SS_MAX_SLOTS` | 128 | Maximum slots (static mode) |
| `SS_MAX_SIGNAL_NAME_LENGTH` | 256 (32 static) | Name buffer size, including the terminator |
| `SS_MAX_DESCRIPTION_LENGTH` | 32 | Description buffer size (static mode) |
| `SS_STATIC_CONTEXT` | 1 | Static mode: `ss_init()` keeps the context in `.bss`; 0 drops it (for `ss_init_static()` pools) |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Mutex protection infrastructure |
| `SS_ENABLE_INTROSPECTION` | 1 | Signal listing and discovery |
| `SS_ENABLE_CUSTOM_DATA` | 1 | Custom data types with cleanup |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Per-signal timing statistics |
| `SS_ENABLE_MEMORY_STATS` | 0 | Memory usage tracking |
| `SS_ENABLE_ISR_SAFE` | 0 | ISR-safe emission path |
| `SS_ENABLE_DEBUG_TRACE` | 0 | Debug trace output |
| `SS_DEFERRED_QUEUE_SIZE` | 64 | Deferred emission queue depth |
| `SS_DEFERRED_PAYLOAD_SIZE` | 32 | Bytes of string/custom data per deferred or batch entry (static mode) |
| `SS_BATCH_MAX_ENTRIES` | `SS_DEFERRED_QUEUE_SIZE` | Entries per batch |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR queue depth |
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

### Core

| Function | Description |
|----------|-------------|
| `ss_init()` | Initialize (context on the heap, or in static storage with `SS_USE_STATIC_MEMORY`) |
| `ss_init_abi(SS_ABI_TAG)` | `ss_init()` plus a check that caller and library were built with the same flags |
| `ss_init_static(pool, size)` | Initialize with the context in a caller-owned buffer (`ss_init_static_abi` checks flags) |
| `ss_get_required_pool_size()` | Bytes `ss_init_static()` needs |
| `ss_cleanup()` | Clean up all resources |
| `ss_version()` | Runtime version string (`SS_VERSION_STRING`) |

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
| `ss_emit_from_isr(signal, value)` | Queue an int event from an ISR |
| `ss_process_isr_queue()` | Deliver queued ISR events (main loop) |

### Namespaces

| Function | Description |
|----------|-------------|
| `ss_set_namespace(ns)` | Set default namespace (NULL clears) |
| `ss_get_namespace()` | Get current namespace |
| `ss_emit_namespaced(ns, signal, data)` | Emit with namespace prefix |

### Deferred Emission

| Function | Description |
|----------|-------------|
| `ss_emit_deferred(signal, data)` | Queue signal for later |
| `ss_flush_deferred()` | Emit all queued signals |

### Batch Operations

| Function | Description |
|----------|-------------|
| `ss_batch_create()` | Create a batch |
| `ss_batch_add(batch, signal, data)` | Add emission to batch |
| `ss_batch_emit(batch)` | Emit all in batch |
| `ss_batch_destroy(batch)` | Free batch |

### Data

| Function | Description |
|----------|-------------|
| `ss_data_create(type)` | Allocate data object |
| `ss_data_destroy(data)` | Free data object |
| `ss_data_set_int/float/double/string/pointer(data, val)` | Set value |
| `ss_data_set_custom(data, ptr, size, cleanup)` | Set custom data |
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
| `ss_set_error_handler(handler)` | Set custom error callback |

### Configuration

| Function | Description |
|----------|-------------|
| `ss_set_max_slots_per_signal(max)` | Set slot limit |
| `ss_get_max_slots_per_signal()` | Get slot limit |
| `ss_set_thread_safe(enabled)` | Enable/disable thread safety |
| `ss_is_thread_safe()` | Check thread safety state |

### Debug Trace

| Function | Description |
|----------|-------------|
| `ss_enable_trace(stream)` | Start trace output (`SS_ENABLE_DEBUG_TRACE`) |
| `ss_disable_trace()` | Stop trace output |

## Advanced Usage

### Connection Handles

```c
ss_connection_t conn;
ss_connect_ex("data_changed", on_data, NULL, SS_PRIORITY_HIGH, &conn);

/* Later... */
ss_disconnect_handle(conn);
```

### Signal Priorities

```c
/* Critical handlers execute first */
ss_connect_ex("alarm", on_critical, NULL, SS_PRIORITY_CRITICAL, NULL);
ss_connect_ex("alarm", on_log, NULL, SS_PRIORITY_LOW, NULL);
```

### Namespaces

```c
ss_signal_register("ui::button");

/* Emit using namespace helper */
ss_emit_namespaced("ui", "button", data);

/* Or set a default namespace: names without "::" resolve as "ui::name" */
ss_set_namespace("ui");
ss_signal_register("slider");      /* registers "ui::slider" */
ss_emit_void("slider");            /* emits "ui::slider" */
ss_emit_void("net::ping");         /* names with "::" are used as given */
ss_set_namespace(NULL);            /* clear */
```

### Deferred Emission

```c
/* Queue signals during processing; the queue keeps a copy of the data */
ss_emit_deferred("update", data1);
ss_emit_deferred("render", data2);

/* Flush at end of frame */
ss_flush_deferred();
```

### Batch Operations

```c
ss_batch_t* batch = ss_batch_create();
ss_batch_add(batch, "signal_a", data1);
ss_batch_add(batch, "signal_b", data2);
ss_batch_emit(batch);
ss_batch_destroy(batch);
```

### Performance Profiling

```c
ss_enable_profiling(1);

/* After running... */
ss_perf_stats_t stats;
ss_get_perf_stats("render_frame", &stats);
printf("Avg: %llu ns, Max: %llu ns\n",
       (unsigned long long)stats.avg_time_ns, (unsigned long long)stats.max_time_ns);
```

### Memory Diagnostics

```c
ss_memory_stats_t mem;
ss_get_memory_stats(&mem);
printf("Signals: %zu/%zu\n", mem.signals_used, mem.signals_allocated);
printf("Slots: %zu/%zu\n", mem.slots_used, mem.slots_allocated);
```

### Embedded System (Static Memory)

```c
/* Built with: -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -DSS_MAX_SLOTS=32
 *             -DSS_ENABLE_THREAD_SAFETY=0 -DSS_ENABLE_ISR_SAFE=1
 * (the same flags for the library and this file) */
#include "ss_lib.h"

void ADC_ISR(void) {
    uint16_t value = ADC->DATA;
    ss_emit_from_isr("adc_ready", value);   /* queued, no slot runs here */
}

int main(void) {
    ss_init();
    ss_signal_register("adc_ready");
    ss_connect("adc_ready", on_adc_ready, NULL);
    for (;;) {
        ss_process_isr_queue();             /* slots run here */
    }
}
```

### Thread Safety

```c
ss_init();
ss_set_thread_safe(1);  /* After ss_init, before other threads use the library */

/* All operations are now protected by one recursive mutex */
ss_signal_register("event");
ss_emit_void("event");
```

Slots run with the mutex held and may call any library function; a slot must not wait for another thread that is itself calling the library. Change the setting only while no other thread uses the library. `ss_init()` and `ss_cleanup()` are not thread-safe.

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
programs whose slots unregister signals, clean up, disconnect, emit and flush
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

Code size on Cortex-M4 (arm-none-eabi-gcc -Os, `SS_EMBEDDED_BUILD` + `SS_MINIMAL_BUILD`): about 3.8 KB of text. RAM is the static context in `.bss`, or a pool you pass to `ss_init_static()` with `SS_STATIC_CONTEXT=0` (`ss_get_required_pool_size()` reports its size plus alignment slack; see the [Embedded Guide](docs/embedded-guide.md#memory-budget)).

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

## LLM Skills (Claude Code)

This repository includes [Claude Code skills](https://docs.anthropic.com/en/docs/claude-code/skills) that teach LLMs to write and review SS_Lib code correctly. They live in `.claude/skills/` and activate automatically when relevant.

| Skill | Trigger | What it does |
|-------|---------|--------------|
| `ss-lib` | Code includes `ss_lib.h` or asks about SS_Lib | Complete API reference, lifecycle rules, string handling pitfalls, configuration, and canonical examples |
| `ss-lib-review` | Asks to review/debug SS_Lib code | 14-item checklist, 5 anti-pattern corrections, feature-flag validation, memory model verification |

### Using the skills

**With Claude Code (CLI):** The skills activate automatically when you work on SS_Lib code in this repository. No setup needed — Claude Code reads `.claude/skills/` on its own.

**With other Claude Code projects that use SS_Lib:** Copy the `.claude/skills/ss-lib/` and `.claude/skills/ss-lib-review/` directories into your project's `.claude/skills/` folder.

**Invoking manually:** Type `/ss-lib` for API guidance or `/ss-lib-review` to review code for common mistakes.

## Security

Versions before 2.2.0 have use-after-free bugs when a slot unregisters its
signal, calls `ss_cleanup()`, or queues a deferred emission during a flush
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
