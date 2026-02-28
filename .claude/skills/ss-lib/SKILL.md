# Skill: Writing Correct SS_Lib Code

## Trigger

Activate when code includes `ss_lib.h`, or user asks about SS_Lib, signal-slot in C, or writing event-driven C code with this library.

## Overview

SS_Lib is a lightweight signal-slot library for C (v2.1.0). Pure ANSI C, zero external dependencies. Designed for embedded systems, game engines, and resource-constrained environments. Supports dynamic and static memory allocation, optional thread safety, and ISR-safe signal emission.

## Mandatory Lifecycle

Every SS_Lib program must follow this sequence:

```c
ss_init();                  // 1. Initialize (or ss_init_static() for embedded)
ss_signal_register("sig");  // 2. Register signals
ss_connect("sig", fn, ud);  // 3. Connect slots
ss_emit_int("sig", 42);     // 4. Emit signals
ss_disconnect("sig", fn);   // 5. Disconnect slots
ss_cleanup();               // 6. Clean up everything
```

Skipping `ss_init()` or `ss_cleanup()` causes undefined behavior or memory leaks.

## Core API Reference

### Init/Cleanup (3 functions)

```c
ss_error_t ss_init(void);
ss_error_t ss_init_static(void* memory_pool, size_t pool_size);
void       ss_cleanup(void);
```

- `ss_init()` — Initialize with dynamic memory. Returns `SS_OK` immediately if already initialized.
- `ss_init_static()` — Initialize with pre-allocated memory pool (embedded use).
- `ss_cleanup()` — Free all resources, unregister all signals. Safe to call if not initialized.

### Signals (4 functions)

```c
ss_error_t ss_signal_register(const char* signal_name);
ss_error_t ss_signal_register_ex(const char* signal_name, const char* description, ss_priority_t priority);
ss_error_t ss_signal_unregister(const char* signal_name);
int        ss_signal_exists(const char* signal_name);
```

- `ss_signal_register()` — Register a named signal. Returns `SS_ERR_ALREADY_EXISTS` if duplicate.
- `ss_signal_register_ex()` — Register with description and default priority.
- `ss_signal_unregister()` — Remove signal and disconnect all its slots.
- `ss_signal_exists()` — Returns 1 if signal exists, 0 otherwise.

### Connections (5 functions)

```c
ss_error_t ss_connect(const char* signal_name, ss_slot_func_t slot, void* user_data);
ss_error_t ss_connect_ex(const char* signal_name, ss_slot_func_t slot, void* user_data,
                         ss_priority_t priority, ss_connection_t* handle);
ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot);
ss_error_t ss_disconnect_handle(ss_connection_t handle);
ss_error_t ss_disconnect_all(const char* signal_name);
```

- `ss_connect()` — Connect slot at `SS_PRIORITY_NORMAL` (5).
- `ss_connect_ex()` — Connect with explicit priority and optional connection handle output.
- `ss_disconnect()` — Disconnect by function pointer. Only removes first match.
- `ss_disconnect_handle()` — Disconnect by opaque handle from `ss_connect_ex()`.
- `ss_disconnect_all()` — Disconnect all slots from a signal.

### Emission (8 functions)

```c
ss_error_t ss_emit(const char* signal_name, const ss_data_t* data);
ss_error_t ss_emit_void(const char* signal_name);
ss_error_t ss_emit_int(const char* signal_name, int value);
ss_error_t ss_emit_float(const char* signal_name, float value);
ss_error_t ss_emit_double(const char* signal_name, double value);
ss_error_t ss_emit_string(const char* signal_name, const char* value);
ss_error_t ss_emit_pointer(const char* signal_name, void* value);
ss_error_t ss_emit_from_isr(const char* signal_name, int value);  // Requires SS_ENABLE_ISR_SAFE
```

- Type-specific helpers (`ss_emit_int`, `ss_emit_float`, etc.) construct a stack-local `ss_data_t` and call `ss_emit()`.
- `ss_emit_from_isr()` — Lock-free, int-only, queues to ISR ring buffer (size `SS_ISR_QUEUE_SIZE`, default 16). Returns `SS_ERR_WOULD_OVERFLOW` if queue full.

### Deferred Emission (2 functions)

```c
ss_error_t ss_emit_deferred(const char* signal_name, const ss_data_t* data);
ss_error_t ss_flush_deferred(void);
```

- `ss_emit_deferred()` — Queue emission for later. String data is duplicated (`SS_STRDUP`). Queue size is `SS_DEFERRED_QUEUE_SIZE` (default 64).
- `ss_flush_deferred()` — Emit all queued signals. Frees duplicated strings. Snapshots count to avoid infinite loops if slots enqueue more.

### Data Handling (14 functions)

```c
// Creation/destruction
ss_data_t* ss_data_create(ss_data_type_t type);
void       ss_data_destroy(ss_data_t* data);

// Setters
ss_error_t ss_data_set_int(ss_data_t* data, int value);
ss_error_t ss_data_set_float(ss_data_t* data, float value);
ss_error_t ss_data_set_double(ss_data_t* data, double value);
ss_error_t ss_data_set_string(ss_data_t* data, const char* value);
ss_error_t ss_data_set_pointer(ss_data_t* data, void* value);
ss_error_t ss_data_set_custom(ss_data_t* data, void* value, size_t size,
                              ss_cleanup_func_t cleanup);  // Requires SS_ENABLE_CUSTOM_DATA

// Getters
int         ss_data_get_int(const ss_data_t* data, int default_val);
float       ss_data_get_float(const ss_data_t* data, float default_val);
double      ss_data_get_double(const ss_data_t* data, double default_val);
const char* ss_data_get_string(const ss_data_t* data);
void*       ss_data_get_pointer(const ss_data_t* data);
void*       ss_data_get_custom(const ss_data_t* data, size_t* size);  // Requires SS_ENABLE_CUSTOM_DATA
```

- `ss_data_create()` — Heap-allocates a zeroed `ss_data_t`. Must be freed with `ss_data_destroy()`.
- `ss_data_set_string()` — **Copies** the string via `SS_STRDUP`. Frees previous string if any.
- `ss_data_set_custom()` — **Copies** the data via `memcpy` into a new allocation.
- `ss_data_destroy()` — Frees the data struct. Also frees copied strings and custom data (calling cleanup if set).
- Getters return default/NULL on type mismatch — they do not coerce types.

### Introspection (3 functions, requires `SS_ENABLE_INTROSPECTION`)

```c
size_t     ss_get_signal_count(void);
ss_error_t ss_get_signal_list(ss_signal_info_t** list, size_t* count);
void       ss_free_signal_list(ss_signal_info_t* list, size_t count);
```

- `ss_signal_info_t` contains: `name`, `slot_count`, `description`, `priority`.
- Must call `ss_free_signal_list()` after `ss_get_signal_list()`.

### Memory Stats (2 functions, requires `SS_ENABLE_MEMORY_STATS`)

```c
ss_error_t ss_get_memory_stats(ss_memory_stats_t* stats);
void       ss_reset_memory_stats(void);
```

### Performance Stats (3 functions, requires `SS_ENABLE_PERFORMANCE_STATS`)

```c
ss_error_t ss_get_perf_stats(const char* signal_name, ss_perf_stats_t* stats);
ss_error_t ss_enable_profiling(int enabled);
void       ss_reset_perf_stats(void);
```

### Error Handling (2 functions)

```c
const char* ss_error_string(ss_error_t error);
void        ss_set_error_handler(void (*handler)(ss_error_t error, const char* msg));
```

### Configuration (4 functions)

```c
void   ss_set_max_slots_per_signal(size_t max_slots);
size_t ss_get_max_slots_per_signal(void);
void   ss_set_thread_safe(int enabled);   // Requires SS_ENABLE_THREAD_SAFETY
int    ss_is_thread_safe(void);           // Requires SS_ENABLE_THREAD_SAFETY
```

### Namespace (3 functions)

```c
ss_error_t  ss_set_namespace(const char* namespace);
const char* ss_get_namespace(void);
ss_error_t  ss_emit_namespaced(const char* namespace, const char* signal_name, const ss_data_t* data);
```

- Convention: `namespace::signal_name`. `ss_emit_namespaced()` constructs this internally.

### Batch Operations (4 functions)

```c
ss_batch_t* ss_batch_create(void);
void        ss_batch_destroy(ss_batch_t* batch);
ss_error_t  ss_batch_add(ss_batch_t* batch, const char* signal_name, const ss_data_t* data);
ss_error_t  ss_batch_emit(ss_batch_t* batch);
```

- `ss_batch_add()` duplicates string data (same as deferred). Max entries = `SS_DEFERRED_QUEUE_SIZE` (default 64).
- `ss_batch_emit()` emits all entries and resets count. Frees duplicated strings.
- `ss_batch_destroy()` frees remaining duplicated strings and the batch struct.

### Debug (2 functions, requires `SS_ENABLE_DEBUG_TRACE`)

```c
void ss_enable_trace(FILE* output);
void ss_disable_trace(void);
```

## Slot Function Signature

Every slot callback must match:

```c
void my_slot(const ss_data_t* data, void* user_data);
```

- `data` — Signal payload. May be NULL for `ss_emit_void()`. Always `const`.
- `user_data` — Pointer passed at `ss_connect()` time. Cast as needed.

## Data Types

```c
typedef enum {
    SS_TYPE_VOID,     // No data
    SS_TYPE_INT,      // int
    SS_TYPE_FLOAT,    // float
    SS_TYPE_DOUBLE,   // double
    SS_TYPE_STRING,   // const char* (null-terminated)
    SS_TYPE_POINTER,  // void*
    SS_TYPE_CUSTOM    // Custom data with size (requires SS_ENABLE_CUSTOM_DATA)
} ss_data_type_t;
```

## String Handling — Critical Distinction

**`ss_emit_string()` does NOT copy the string.** It stores the pointer directly:

```c
// ss_lib.c:643-647 — pointer stored, NOT copied
ss_error_t ss_emit_string(const char* signal_name, const char* value) {
    ss_data_t data = {0};
    data.type = SS_TYPE_STRING;
    data.value.s_val = value;  // Direct pointer assignment
    return ss_emit(signal_name, &data);
}
```

**`ss_data_set_string()` DOES copy the string** via `SS_STRDUP`:

```c
// ss_lib.c:740-755 — string duplicated
ss_error_t ss_data_set_string(ss_data_t* data, const char* value) {
    // ... frees previous string ...
    data->value.s_val = SS_STRDUP(value);  // Copied!
}
```

**Rule:** `ss_emit_string()` is safe only when the string outlives the emission. For temporary/local strings with deferred use, use `ss_data_create()` + `ss_data_set_string()` + `ss_emit()`.

Note: `ss_emit_deferred()` and `ss_batch_add()` both duplicate string data internally, so temporary strings are safe with those functions.

## Priority System

```c
SS_PRIORITY_LOW      = 0
SS_PRIORITY_NORMAL   = 5   // Default for ss_connect()
SS_PRIORITY_HIGH     = 10
SS_PRIORITY_CRITICAL = 15
```

- Higher priority = executed first.
- Slots are sorted by priority at connection time (insertion sort into linked list).
- Priority cannot be changed after connection. Must disconnect and reconnect.

## Error Codes

```c
SS_OK = 0                // Success
SS_ERR_NULL_PARAM        // NULL parameter passed
SS_ERR_MEMORY            // Memory allocation failed
SS_ERR_NOT_FOUND         // Signal or slot not found
SS_ERR_ALREADY_EXISTS    // Signal already registered
SS_ERR_INVALID_TYPE      // Invalid data type
SS_ERR_BUFFER_TOO_SMALL  // Buffer too small for operation
SS_ERR_MAX_SLOTS         // Maximum slots reached (static mode)
SS_ERR_TIMEOUT           // Operation timed out
SS_ERR_WOULD_OVERFLOW    // Operation would overflow buffer
SS_ERR_ISR_UNSAFE        // Operation not safe in ISR context
```

All functions return `ss_error_t`. Always check return values, especially `ss_init()`, `ss_connect()`, and `ss_emit()`.

## Thread Safety

Thread safety is **compiled in** by default (`SS_ENABLE_THREAD_SAFETY 1`) but **disabled at runtime**. You must explicitly enable it:

```c
ss_init();
ss_set_thread_safe(1);  // Must call BEFORE any multi-threaded access
// ... use from multiple threads ...
ss_set_thread_safe(0);  // Disable before cleanup if desired
ss_cleanup();
```

- `ss_set_thread_safe(1)` initializes the mutex (pthread on Unix, Critical Section on Windows).
- `ss_set_thread_safe(0)` destroys the mutex.
- Uses a single global mutex. Not designed for high-contention scenarios.
- `ss_is_thread_safe()` returns current state.

## Configuration (`ss_config.h`)

### Feature Flags (defaults)

| Flag | Default | Description |
|------|---------|-------------|
| `SS_USE_STATIC_MEMORY` | 0 | Use pre-allocated pools instead of malloc |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Compile thread-safety support |
| `SS_ENABLE_INTROSPECTION` | 1 | Signal listing/counting APIs |
| `SS_ENABLE_CUSTOM_DATA` | 1 | SS_TYPE_CUSTOM support |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Per-signal timing stats |
| `SS_ENABLE_MEMORY_STATS` | 0 | Memory usage tracking |
| `SS_ENABLE_DEBUG_TRACE` | 0 | Debug trace output |
| `SS_ENABLE_ISR_SAFE` | 0 | ISR-safe emission |

### Limits

| Macro | Default | Description |
|-------|---------|-------------|
| `SS_MAX_SIGNALS` | 32 | Max signals (static mode only) |
| `SS_MAX_SLOTS` | 128 | Max total slots (static mode only) |
| `SS_MAX_SIGNAL_NAME_LENGTH` | 256 (dynamic) / 32 (static) | Max signal name length |
| `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL` | 100 | Max slots per signal |
| `SS_DEFERRED_QUEUE_SIZE` | 64 | Deferred/batch queue size |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR emission queue size |
| `SS_CACHE_LINE_SIZE` | 64 | Cache line alignment |

### Presets

- **`SS_MINIMAL_BUILD`** — Disables: thread safety, introspection, custom data, performance stats, memory stats.
- **`SS_EMBEDDED_BUILD`** — Enables static memory, disables thread safety, sets max slots per signal to 10.

### Custom Allocators

Override by defining before including `ss_config.h`:

```c
#define SS_MALLOC(size)        my_malloc(size)
#define SS_FREE(ptr)           my_free(ptr)
#define SS_CALLOC(count, size) my_calloc(count, size)
#define SS_STRDUP(str)         my_strdup(str)
```

## Connection Handles

`ss_connection_t` is an opaque `uintptr_t` returned by `ss_connect_ex()`:

```c
ss_connection_t handle;
ss_connect_ex("sig", my_slot, NULL, SS_PRIORITY_HIGH, &handle);
// ... later ...
ss_disconnect_handle(handle);  // Disconnect without knowing signal name
```

Handles are monotonically increasing integers starting at 1. A handle of 0 is never assigned and can be used as "no handle" sentinel.

## Canonical Examples

### Basic Usage

```c
#include "ss_lib.h"
#include <stdio.h>

void on_event(const ss_data_t* data, void* user_data) {
    int val = ss_data_get_int(data, 0);
    printf("Event: %d\n", val);
}

int main(void) {
    if (ss_init() != SS_OK) return 1;

    ss_signal_register("event");
    ss_connect("event", on_event, NULL);
    ss_emit_int("event", 42);
    ss_disconnect_all("event");
    ss_signal_unregister("event");
    ss_cleanup();
    return 0;
}
```

### Priority + Handles

```c
ss_connection_t h1, h2;
ss_connect_ex("alarm", critical_handler, NULL, SS_PRIORITY_CRITICAL, &h1);
ss_connect_ex("alarm", logger, NULL, SS_PRIORITY_LOW, &h2);

ss_emit_void("alarm");  // critical_handler runs first, then logger

ss_disconnect_handle(h1);
ss_disconnect_handle(h2);
```

### Embedded Static Memory

```c
#define SS_EMBEDDED_BUILD
#include "ss_lib.h"

static uint8_t pool[4096];

int main(void) {
    ss_init_static(pool, sizeof(pool));
    ss_signal_register("sensor");
    ss_connect("sensor", read_sensor, NULL);
    ss_emit_int("sensor", adc_read());
    ss_cleanup();
    return 0;
}
```

### Custom Data

```c
typedef struct { int x, y; } Point;

void on_move(const ss_data_t* data, void* user_data) {
    size_t size;
    Point* p = (Point*)ss_data_get_custom(data, &size);
    if (p && size == sizeof(Point)) {
        printf("Moved to (%d, %d)\n", p->x, p->y);
    }
}

// Usage:
ss_data_t* d = ss_data_create(SS_TYPE_CUSTOM);
Point pt = {10, 20};
ss_data_set_custom(d, &pt, sizeof(Point), NULL);  // Copies pt
ss_emit("moved", d);
ss_data_destroy(d);
```

### ISR Emission

```c
// Compile with -DSS_ENABLE_ISR_SAFE=1

// In ISR handler (no locks, no malloc):
void TIM2_IRQHandler(void) {
    ss_emit_from_isr("timer_tick", tick_count);  // int only
}

// In main loop — process queued ISR events:
// (Application must poll/flush ISR queue manually)
```

### Deferred / Batch

```c
// Deferred: queue now, emit later
ss_emit_deferred("update", data);   // String data duplicated automatically
ss_emit_deferred("refresh", NULL);
ss_flush_deferred();                 // Emits all queued, frees duped strings

// Batch: group emissions
ss_batch_t* batch = ss_batch_create();
ss_batch_add(batch, "save", data1);  // String data duplicated automatically
ss_batch_add(batch, "log", data2);
ss_batch_emit(batch);                // Emits all, resets count
ss_batch_destroy(batch);             // Frees batch struct
```

### Namespace

```c
ss_set_namespace("audio");
ss_signal_register("audio::volume_changed");
ss_emit_namespaced("audio", "volume_changed", data);  // Emits "audio::volume_changed"
```
