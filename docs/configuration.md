# Configuration Guide

SS_Lib uses compile-time macros to control features, memory model, and behavior. All configuration happens through `include/ss_config.h` or via `-D` compiler flags.

## Memory Model

### Dynamic Memory (Default)

The default mode uses standard `malloc`/`free` for allocation. No limits on signal or slot count.

```c
#define SS_USE_STATIC_MEMORY 0  /* default */
```

### Static Memory

Pre-allocated arrays for signals and slots. Zero dynamic allocation after initialization. Suitable for embedded systems where heap fragmentation is unacceptable.

```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 32       /* maximum registered signals */
#define SS_MAX_SLOTS 128        /* maximum total slots across all signals */
```

When static memory is enabled, signal names are stored in fixed-size buffers:

```c
#define SS_MAX_SIGNAL_NAME_LENGTH 32  /* default in static mode */
```

In dynamic mode, signal names are heap-allocated and the default limit is 256 characters.

## Feature Flags

### Thread Safety

```c
#define SS_ENABLE_THREAD_SAFETY 1  /* default: 1 */
```

Compiles mutex infrastructure (pthread on Unix, Critical Section on Windows). Thread safety is still **disabled at runtime by default** — call `ss_set_thread_safe(1)` to activate it. This design ensures zero locking overhead in single-threaded programs.

Set to 0 to remove all mutex code from the build (saves code size on embedded targets).

### Introspection

```c
#define SS_ENABLE_INTROSPECTION 1  /* default: 1 */
```

Enables `ss_get_signal_count()`, `ss_get_signal_list()`, and `ss_free_signal_list()`. These functions allow runtime discovery of registered signals and their slot counts.

### Custom Data

```c
#define SS_ENABLE_CUSTOM_DATA 1  /* default: 1 */
```

Enables `SS_TYPE_CUSTOM` data type and `ss_data_set_custom()`/`ss_data_get_custom()`. Custom data supports a cleanup callback for automatic resource management.

### Performance Statistics

```c
#define SS_ENABLE_PERFORMANCE_STATS 0  /* default: 0 */
```

Enables per-signal timing statistics. When enabled and profiling is activated with `ss_enable_profiling(1)`, each emission records its duration.

### Memory Statistics

```c
#define SS_ENABLE_MEMORY_STATS 0  /* default: 0 */
```

Tracks signal/slot allocation counts and memory usage. Useful for monitoring resource consumption.

### ISR-Safe Emission

```c
#define SS_ENABLE_ISR_SAFE 0  /* default: 0 */
```

Enables `ss_emit_from_isr()` which uses a lock-free ring buffer for safe signal emission from interrupt service routines. Configure the queue depth:

```c
#define SS_ISR_QUEUE_SIZE 16  /* default */
```

### Debug Trace

```c
#define SS_ENABLE_DEBUG_TRACE 0  /* default: 0 */
```

Enables trace output via `ss_enable_trace(FILE*)`. When active, signal registration, connection, and emission events are logged.

## Limits

```c
#define SS_DEFAULT_MAX_SLOTS_PER_SIGNAL 100  /* runtime adjustable */
#define SS_DEFERRED_QUEUE_SIZE 64            /* deferred emission queue */
#define SS_CACHE_LINE_SIZE 64                /* alignment hint */
```

## Custom Allocators

Replace the default allocators to integrate with your own memory management:

```c
#define SS_MALLOC(size)        my_alloc(size)
#define SS_FREE(ptr)           my_free(ptr)
#define SS_CALLOC(count, size) my_calloc(count, size)
#define SS_STRDUP(str)         my_strdup(str)
```

These macros are used throughout both `ss_lib.c` and `ss_lib_c89.c`. Define them **before** including `ss_lib.h` or in `ss_config.h`.

Example with a pool allocator:

```c
#include "memory_pool.h"

#define SS_MALLOC(size)        pool_alloc(&app_pool, size)
#define SS_FREE(ptr)           pool_free(&app_pool, ptr)
#define SS_CALLOC(count, size) pool_calloc(&app_pool, count, size)
#define SS_STRDUP(str)         pool_strdup(&app_pool, str)

#include "ss_lib.h"
```

## Preset Configurations

### SS_MINIMAL_BUILD

Strips all optional features for the smallest possible footprint:

```c
#define SS_MINIMAL_BUILD
```

This sets:
- `SS_ENABLE_THREAD_SAFETY 0`
- `SS_ENABLE_INTROSPECTION 0`
- `SS_ENABLE_CUSTOM_DATA 0`
- `SS_ENABLE_PERFORMANCE_STATS 0`
- `SS_ENABLE_MEMORY_STATS 0`

### SS_EMBEDDED_BUILD

Optimized for embedded targets:

```c
#define SS_EMBEDDED_BUILD
```

This sets:
- `SS_USE_STATIC_MEMORY 1`
- `SS_ENABLE_THREAD_SAFETY 0`
- `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL 10`

## Platform-Specific Configuration

### Arduino/AVR (8-bit, ~2KB RAM)

```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 8
#define SS_MAX_SLOTS 16
#define SS_MAX_SIGNAL_NAME_LENGTH 16
#define SS_MINIMAL_BUILD
```

### STM32 ARM Cortex-M (32-bit, 64KB+ RAM)

```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 32
#define SS_MAX_SLOTS 64
#define SS_ENABLE_ISR_SAFE 1
#define SS_ENABLE_THREAD_SAFETY 0  /* Using RTOS primitives instead */
```

### Linux/Desktop

```c
/* Default configuration works well, optionally add: */
#define SS_ENABLE_PERFORMANCE_STATS 1
#define SS_ENABLE_MEMORY_STATS 1
```

### Compiler Flags

Instead of modifying `ss_config.h`, you can override settings via compiler flags:

```bash
gcc -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -DSS_ENABLE_ISR_SAFE=1 \
    -c src/ss_lib.c -Iinclude
```

## C89 Compatibility

SS_Lib includes a C89-compatible source file (`src/ss_lib_c89.c`) that provides full feature parity with the C11 version. Use it when targeting older toolchains:

```bash
gcc -std=c89 -pedantic -c src/ss_lib_c89.c -Iinclude
```

The C89 version uses:
- Block comments only (no `//`)
- Declarations at top of blocks
- `memset` instead of `= {0}` initializers
- The same `ss_config.h` configuration flags
