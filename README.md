# SS_Lib - Signal-Slot Library for C

[![Build](https://github.com/dardevelin/ss_lib/actions/workflows/build.yml/badge.svg)](https://github.com/dardevelin/ss_lib/actions/workflows/build.yml)
[![Benchmarks](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml/badge.svg)](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A lightweight, efficient signal-slot implementation for C, designed for embedded systems, game engines, and resource-constrained environments. Pure ANSI C with zero external dependencies.

## Quick Start

```c
#include "ss_lib.h"

void on_button_click(const ss_data_t* data, void* user_data) {
    printf("Button clicked!\n");
}

int main() {
    ss_init();

    ss_signal_register("button_click");
    ss_connect("button_click", on_button_click, NULL);

    ss_emit_void("button_click");

    ss_cleanup();
    return 0;
}
```

## Installation

### Using pkg-config

```bash
sudo make install
pkg-config --cflags --libs ss_lib
```

### Using CMake

```cmake
find_package(ss_lib REQUIRED)
target_link_libraries(your_app ss_lib::ss_lib)
```

### Single Header

```bash
make single-header
# Creates ss_lib_single.h — copy to your project

# In your code:
#define SS_IMPLEMENTATION
#include "ss_lib_single.h"
```

### Using Homebrew (macOS)

```bash
brew tap dardevelin/ss-lib
brew install ss-lib
```

### Using AUR (Arch Linux)

```bash
yay -S ss-lib
```

## Features

- **Zero Dependencies** — Pure ANSI C, no external libraries required
- **Static Memory** — No malloc/free at runtime for embedded systems
- **Thread-Safe** — Optional mutex protection with runtime toggle
- **ISR-Safe** — Lock-free signal emission from interrupt handlers
- **Namespaces** — Organize signals with namespace prefixes
- **Deferred Emission** — Queue signals for later processing
- **Batch Operations** — Group multiple emissions into a single batch
- **Configurable** — Compile-time feature flags for minimal footprint
- **Single Header** — Easy integration with a single `#include`
- **Performance Profiling** — Built-in per-signal timing statistics
- **Memory Diagnostics** — Track allocation usage in real-time
- **C89 Compatible** — Alternate C89 source for legacy toolchains

## Configuration

### Compile-Time Flags

| Flag | Default | Description |
|------|---------|-------------|
| `SS_USE_STATIC_MEMORY` | 0 | Use static allocation (no heap) |
| `SS_MAX_SIGNALS` | 32 | Maximum signals (static mode) |
| `SS_MAX_SLOTS` | 128 | Maximum slots (static mode) |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Mutex protection infrastructure |
| `SS_ENABLE_INTROSPECTION` | 1 | Signal listing and discovery |
| `SS_ENABLE_CUSTOM_DATA` | 1 | Custom data types with cleanup |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Per-signal timing statistics |
| `SS_ENABLE_MEMORY_STATS` | 0 | Memory usage tracking |
| `SS_ENABLE_ISR_SAFE` | 0 | ISR-safe emission path |
| `SS_ENABLE_DEBUG_TRACE` | 0 | Debug trace output |
| `SS_DEFERRED_QUEUE_SIZE` | 64 | Deferred emission queue depth |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR queue depth |

### Preset Configurations

```c
#define SS_MINIMAL_BUILD    /* Strips all optional features */
#define SS_EMBEDDED_BUILD   /* Static memory, no threading */
```

### Custom Allocators

```c
#define SS_MALLOC(size)       my_malloc(size)
#define SS_FREE(ptr)          my_free(ptr)
#define SS_CALLOC(count,size) my_calloc(count, size)
#define SS_STRDUP(str)        my_strdup(str)
```

### Platform Examples

#### Arduino/AVR (8-bit MCU)

```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 8
#define SS_MAX_SLOTS 16
#define SS_MINIMAL_BUILD
```

#### STM32/ARM Cortex-M

```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 32
#define SS_MAX_SLOTS 64
#define SS_ENABLE_ISR_SAFE 1
#define SS_ENABLE_THREAD_SAFETY 0
```

#### Linux Embedded

```c
#define SS_USE_STATIC_MEMORY 0
#define SS_ENABLE_THREAD_SAFETY 1
#define SS_ENABLE_PERFORMANCE_STATS 1
```

## API Overview

### Core

| Function | Description |
|----------|-------------|
| `ss_init()` | Initialize with dynamic memory |
| `ss_init_static(pool, size)` | Initialize with static memory pool |
| `ss_cleanup()` | Clean up all resources |

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
| `ss_emit_from_isr(signal, value)` | ISR-safe emission |

### Namespaces

| Function | Description |
|----------|-------------|
| `ss_set_namespace(ns)` | Set default namespace |
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
ss_signal_register("ui::slider");

/* Emit using namespace helper */
ss_emit_namespaced("ui", "button", data);
```

### Deferred Emission

```c
/* Queue signals during processing */
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
printf("Avg: %llu ns, Max: %llu ns\n", stats.avg_time_ns, stats.max_time_ns);
```

### Memory Diagnostics

```c
ss_memory_stats_t mem;
ss_get_memory_stats(&mem);
printf("Signals: %zu/%zu\n", mem.signals_used, mem.signals_allocated);
printf("Slots: %zu/%zu\n", mem.slots_used, mem.slots_allocated);
```

### Custom Allocators

```c
/* Define before including ss_lib.h or in ss_config.h */
#define SS_MALLOC(size)       pool_alloc(size)
#define SS_FREE(ptr)          pool_free(ptr)
#define SS_CALLOC(count,size) pool_calloc(count, size)
#define SS_STRDUP(str)        pool_strdup(str)
```

### Embedded System (Static Memory)

```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 16
#define SS_MAX_SLOTS 32
#define SS_ENABLE_THREAD_SAFETY 0
#define SS_ENABLE_ISR_SAFE 1
#include "ss_lib.h"

void ADC_ISR(void) {
    uint16_t value = ADC->DATA;
    ss_emit_from_isr("adc_ready", value);
}
```

### Thread Safety

```c
ss_init();
ss_set_thread_safe(1);  /* Enable at runtime */

/* All operations are now mutex-protected */
ss_signal_register("event");
ss_emit_void("event");

ss_set_thread_safe(0);  /* Disable when no longer needed */
```

## Building

### Standard Build

```bash
make          # Build library and tests
make test     # Run tests
make examples # Build example programs
```

### Embedded Build

```bash
gcc -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -c src/ss_lib.c -Iinclude
```

### CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

### Single Header

```bash
make single-header
```

## Testing

```bash
make test           # Run all tests
make test-asan      # AddressSanitizer
make test-tsan      # ThreadSanitizer
make test-ubsan     # UndefinedBehaviorSanitizer
make test-msan      # MemorySanitizer (clang only)
make test-valgrind  # Valgrind memory check
make coverage       # Generate coverage report
```

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

Memory usage (16 signals, 32 slots):
- Total allocation: 2.1 KB
- Per signal overhead: 48 bytes
- Per slot overhead: 24 bytes
```

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

## Contributing

1. Fork the repository
2. Create your feature branch
3. Ensure tests pass (`make test`)
4. Submit pull request

## License

MIT License - See [LICENSE](LICENSE) file for details.
