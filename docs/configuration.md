# Configuration Guide

SS_Lib uses compile-time macros to control features, memory model, and behavior. All configuration happens through `include/ss_config.h` or via `-D` compiler flags.

The configuration must be identical for the library and every file that includes `ss_lib.h`: it changes struct layouts and which functions are declared. Pass it on the command line (or edit `ss_config.h`), never with a `#define` in one source file before the include. The snippets below show macro values; `#define X 1` means `-DX=1`. Initializing with `ss_init_abi(SS_ABI_TAG)` instead of `ss_init()` returns `SS_ERR_CONFIG_MISMATCH` when the caller and the library disagree on the `ss_data_t` layout (`SS_ENABLE_CUSTOM_DATA`); other differences are not detected.

## Memory Model

### Dynamic Memory (Default)

The default mode uses standard `malloc`/`free` for allocation. No limits on signal or slot count.

```c
#define SS_USE_STATIC_MEMORY 0  /* default */
```

### Static Memory

Pre-allocated arrays for signals and slots; the library context lives in static storage. `ss_init` (unless `SS_STATIC_CONTEXT` is 0), register, connect, emit, deferred emission, namespace, disconnect, unregister and cleanup never touch the heap. Suitable for embedded systems where heap fragmentation is unacceptable.

Functions whose job is to hand the caller an allocated object still allocate: `ss_data_create`, `ss_data_set_string`, `ss_data_set_custom`, `ss_get_signal_list` and `ss_batch_create`. The borrowing setters `ss_data_set_string_ref` and `ss_data_set_custom_ref` never allocate.

```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 32       /* maximum registered signals */
#define SS_MAX_SLOTS 128        /* maximum total slots across all signals */
```

When static memory is enabled, signal names are stored in fixed-size buffers:

```c
#define SS_MAX_SIGNAL_NAME_LENGTH 32  /* default in static mode */
```

In dynamic mode, signal names are heap-allocated and the default limit is 256 bytes. In both modes the limit includes the terminator, so names may be up to `SS_MAX_SIGNAL_NAME_LENGTH - 1` characters.

Static mode also has fixed buffers for descriptions and for queued payloads:

```c
#define SS_MAX_DESCRIPTION_LENGTH 32  /* per-signal description, incl. terminator */
#define SS_DEFERRED_PAYLOAD_SIZE 32   /* string/custom bytes per deferred or batch entry */
```

Longer descriptions and payloads are rejected with `SS_ERR_WOULD_OVERFLOW`. Descriptions are only stored when `SS_ENABLE_INTROSPECTION` is on.

```c
#define SS_STATIC_CONTEXT 1  /* default: ss_init() keeps the context in .bss */
```

Set it to 0 when the application always passes its own pool to `ss_init_static()`: this drops the built-in context and saves its RAM. `ss_init()` (and `ss_init_static()` with a NULL or too small pool) then allocates the context with `SS_CALLOC`.

## Feature Flags

### Thread Safety

```c
#define SS_ENABLE_THREAD_SAFETY 1  /* default: 1 */
```

Compiles one recursive mutex (pthread recursive mutex on Unix, Critical Section on Windows), created by `ss_init()`. Thread safety is still **disabled at runtime by default** — call `ss_set_thread_safe(1)` after `ss_init()` to activate it, while no other thread uses the library. This design ensures zero locking overhead in single-threaded programs. Slots run with the mutex held and may call back into the library.

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

Enables `ss_get_memory_stats()` and `ss_reset_memory_stats()`: live and allocated signal/slot counts, heap bytes held by the library and their peak (dynamic mode), or the size of the static context. See the [API Reference](api-reference.md#ss_get_memory_stats) for the exact definitions.

### ISR-Safe Emission

```c
#define SS_ENABLE_ISR_SAFE 0  /* default: 0 */
```

Enables `ss_emit_from_isr()`, which queues an integer event from an interrupt service routine without locks or allocation, and `ss_process_isr_queue()`, which delivers the queued events from the main loop. Configure the queue depth:

```c
#define SS_ISR_QUEUE_SIZE 16  /* default */
```

Queue entries are claimed with C11 `<stdatomic.h>`, GCC/Clang `__atomic` builtins or MSVC Interlocked functions. On targets without atomic compare-and-swap (Cortex-M0, AVR), define both of these to disable and restore interrupts, for example in `ss_config.h` (the CMSIS functions must be declared where `src/ss_lib.c` sees them). Both must also be compiler memory barriers, as the CMSIS functions are, so the compiler cannot move the queue's reads and writes out of the critical section:

```c
#define SS_ISR_CRITICAL_ENTER() uint32_t ss_irq_ = __get_PRIMASK(); __disable_irq()
#define SS_ISR_CRITICAL_EXIT()  __set_PRIMASK(ss_irq_)
```

### Debug Trace

```c
#define SS_ENABLE_DEBUG_TRACE 0  /* default: 0 */
```

Enables `ss_enable_trace(FILE*)` and `ss_disable_trace()`. Trace is off until `ss_enable_trace()` names a stream; then registration, connection and emission events are logged to it. The `SS_TRACE` macro in `ss_config.h` is deprecated: it is kept for source compatibility, but the library no longer uses it.

## Limits

```c
#define SS_DEFAULT_MAX_SLOTS_PER_SIGNAL 100  /* runtime adjustable */
#define SS_DEFERRED_QUEUE_SIZE 64            /* deferred emission queue (16 with SS_EMBEDDED_BUILD) */
#define SS_BATCH_MAX_ENTRIES 64              /* entries per batch; default SS_DEFERRED_QUEUE_SIZE */
#define SS_CACHE_LINE_SIZE 64                /* alignment hint */
```

The deferred queue is part of the library context and each entry holds a full name buffer, so `SS_DEFERRED_QUEUE_SIZE * SS_MAX_SIGNAL_NAME_LENGTH` is a large share of its size. Each `ss_batch_create()` allocates `SS_BATCH_MAX_ENTRIES` entries.

## Custom Allocators

Replace the default allocators to integrate with your own memory management:

```c
#define SS_MALLOC(size)        my_alloc(size)
#define SS_FREE(ptr)           my_free(ptr)
#define SS_CALLOC(count, size) my_calloc(count, size)
```

Strings are copied with `SS_MALLOC`, so these three route every allocation, including the strings that `ss_data_destroy()` and `ss_free_signal_list()` free. `SS_STRDUP(str)` is undefined by default; define it only to override string duplication (its result is freed with `SS_FREE`).

These macros are used by the library source (`ss_lib.c` / `ss_lib_c89.c`), so they must be defined where it is compiled: in `ss_config.h`, or with `-D` flags when building the library. Defining them in your own source file before including `ss_lib.h` has no effect on the library.

Example with a pool allocator, added near the top of `ss_config.h` (before the defaults):

```c
#include "memory_pool.h"

#define SS_MALLOC(size)        pool_alloc(&app_pool, size)
#define SS_FREE(ptr)           pool_free(&app_pool, ptr)
#define SS_CALLOC(count, size) pool_calloc(&app_pool, count, size)
```

## Preset Configurations

### SS_MINIMAL_BUILD

Presets override the options they control. Strips all optional features for the smallest possible footprint:

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
- `SS_DEFERRED_QUEUE_SIZE 16`, unless you define it yourself

It can be combined with `SS_MINIMAL_BUILD` and with explicit limits such as `SS_MAX_SIGNALS`.

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

On Cortex-M0/M0+ also define `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` (see [ISR-Safe Emission](#isr-safe-emission)).

### Linux/Desktop

```c
/* Default configuration works well, optionally add: */
#define SS_ENABLE_PERFORMANCE_STATS 1
#define SS_ENABLE_MEMORY_STATS 1
```

### Compiler Flags

Instead of modifying `ss_config.h`, you can override settings via compiler flags. Use the same flags for the library and for every file that includes `ss_lib.h`:

```bash
CONFIG="-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -DSS_ENABLE_ISR_SAFE=1"
gcc $CONFIG -Iinclude -c src/ss_lib.c
gcc $CONFIG -Iinclude -c app.c
```

The Makefile does this with one `FEATURES` variable (`make FEATURES="..."`); CMake exports its feature definitions with the `ss_lib::ss_lib` target. With the single header, define the macros identically in every file that includes it.

## C89 Compatibility

`src/ss_lib.c` is written in the common subset of C89 and C11, and `src/ss_lib_c89.c` is a copy with the same body (`make test-c89` builds it with `-std=c89 -Werror` against the test suite). Use it when targeting older toolchains:

```bash
gcc -std=c89 -pedantic -c src/ss_lib_c89.c -Iinclude
```

The same source also builds warning-free as C99, C11, C17, C23 and C2y: `make check-standards` compiles the library, a caller of `ss_lib.h` and the single header as each standard the compiler supports, and `make test-newest` runs the test suite built as the newest one.

Both files use:
- Block comments only (no `//`)
- Declarations at top of blocks
- `memset` instead of `= {0}` initializers
- The same `ss_config.h` configuration flags
