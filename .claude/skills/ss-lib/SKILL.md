# Skill: Writing Correct SS_Lib Code

## Trigger

Activate when code includes `ss_lib.h`, or user asks about SS_Lib, signal-slot in C, or writing event-driven C code with this library.

## Overview

SS_Lib is a lightweight signal-slot library for C (v2.2.4). Pure ANSI C, zero external dependencies. Designed for embedded systems, game engines, and resource-constrained environments. Supports dynamic and static memory allocation, optional thread safety, and ISR-safe signal emission.

## Mandatory Lifecycle

Every SS_Lib program must follow this sequence:

```c
ss_init_abi(SS_ABI_TAG);    // 1. Initialize (recommended; or ss_init() / ss_init_static())
ss_signal_register("sig");  // 2. Register signals
ss_connect("sig", fn, ud);  // 3. Connect slots
ss_emit_int("sig", 42);     // 4. Emit signals
ss_disconnect("sig", fn);   // 5. Disconnect slots
ss_cleanup();               // 6. Clean up everything
```

Calls before `ss_init()` fail (`SS_ERR_NULL_PARAM`, 0 or NULL); skipping `ss_cleanup()` leaks in dynamic mode. Every compile-time option (`SS_ENABLE_*`, `SS_USE_STATIC_MEMORY`, presets, limits) must be identical for the library and every file that includes `ss_lib.h`: pass them on the command line (`-D...`), never `#define` them in one source file. New code should initialize with `ss_init_abi(SS_ABI_TAG)`, which returns `SS_ERR_CONFIG_MISMATCH` when the caller's `ss_data_t` layout differs from the library's (plain `ss_init()` does not check).

## Core API Reference

### Init/Cleanup (7 functions)

```c
ss_error_t  ss_init(void);
ss_error_t  ss_init_static(void* memory_pool, size_t pool_size);
ss_error_t  ss_init_abi(size_t abi_tag);                      // call as ss_init_abi(SS_ABI_TAG)
ss_error_t  ss_init_static_abi(void* memory_pool, size_t pool_size, size_t abi_tag);
size_t      ss_get_required_pool_size(void);
void        ss_cleanup(void);
const char* ss_version(void);                                 // == SS_VERSION_STRING
```

- `ss_init()` — Plain function. Context on the heap (dynamic mode) or in static storage (`SS_USE_STATIC_MEMORY`, no heap; with `SS_STATIC_CONTEXT=0` the context is `SS_CALLOC`'d instead). Returns `SS_OK` if already initialized (state is NOT reset), `SS_ERR_ALREADY_EXISTS` while a cleanup requested from a slot is still pending.
- `ss_init_abi(SS_ABI_TAG)` / `ss_init_static_abi(pool, size, SS_ABI_TAG)` — Same as `ss_init()` / `ss_init_static()`, but first return `SS_ERR_CONFIG_MISMATCH` if the caller was compiled with a different `ss_data_t` layout (e.g. different `SS_ENABLE_CUSTOM_DATA`). Recommended for new code.
- `ss_init_static(pool, size)` — With a pool of at least `ss_get_required_pool_size()` bytes (any alignment), places the library context in it. Works in both memory modes; signals and slots still follow the mode. The pool must outlive `ss_cleanup()`, which never frees it. A NULL or smaller pool is ignored and the call behaves exactly like `ss_init()` (2.1 compatibility): it never returns `SS_ERR_NULL_PARAM` / `SS_ERR_BUFFER_TOO_SMALL`, so check the size yourself if the pool placement matters.
- `ss_cleanup()` — Releases signals, slots, pending deferred and ISR events. Safe if not initialized. Called from a slot, it is deferred until the outermost library call returns; meanwhile the library behaves as uninitialized and the remaining slots are skipped.
- `ss_init()` / `ss_cleanup()` are not thread-safe: call them while no other thread uses the library.
- Version macros: `SS_VERSION_MAJOR`, `SS_VERSION_MINOR`, `SS_VERSION_PATCH`, `SS_VERSION_STRING`.

### Signals (4 functions)

```c
ss_error_t ss_signal_register(const char* signal_name);
ss_error_t ss_signal_register_ex(const char* signal_name, const char* description, ss_priority_t priority);
ss_error_t ss_signal_unregister(const char* signal_name);
int        ss_signal_exists(const char* signal_name);
```

- `ss_signal_register()` — Register a named signal. Returns `SS_ERR_ALREADY_EXISTS` if duplicate, `SS_ERR_WOULD_OVERFLOW` if the (namespace-qualified) name does not fit `SS_MAX_SIGNAL_NAME_LENGTH` or the static signal array is full.
- `ss_signal_register_ex()` — Register with description and default priority. The description is stored only with `SS_ENABLE_INTROSPECTION`; in static mode it must fit `SS_MAX_DESCRIPTION_LENGTH` (default 32 incl. NUL) or registration fails with `SS_ERR_WOULD_OVERFLOW`.
- `ss_signal_unregister()` — Remove signal and disconnect all its slots. Safe from a slot of that signal: the remaining slots of that emission are skipped, memory is released when the emission ends, and the name can be registered again at once.
- `ss_signal_exists()` — Returns 1 if signal exists, 0 otherwise.
- All name-taking functions apply the default namespace (see Namespace).

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
- `ss_connect_ex()` — Connect with explicit priority and optional connection handle output. `SS_ERR_MAX_SLOTS` when the signal already has `max_slots_per_signal` live slots; static mode returns `SS_ERR_WOULD_OVERFLOW` when the `SS_MAX_SLOTS` pool is exhausted.
- `ss_disconnect()` — Disconnect by function pointer. Only removes first match.
- `ss_disconnect_handle()` — Disconnect by opaque handle from `ss_connect_ex()`. Handle 0 returns `SS_ERR_NULL_PARAM`.
- `ss_disconnect_all()` — Disconnect all slots from a signal.
- All of these are safe from inside slots, including on the signal being emitted. A slot disconnected during an emission is not called afterwards and is freed when the emission ends. A slot connected during an emission of the same signal may or may not be called by that emission (depends on its priority position): do not rely on either.

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
- Slots may call any library function, including emitting, unregistering or `ss_cleanup()`.
- `ss_emit_from_isr()` — int only. No locks, no allocation, no slot calls; claims a queue entry with atomic compare-and-swap, so it is safe with nested interrupts and from several threads, and usable before `ss_init()`. Returns `SS_ERR_NULL_PARAM` for NULL/empty names, `SS_ERR_WOULD_OVERFLOW` if the queue (`SS_ISR_QUEUE_SIZE`, default 16) is full or the name is too long.
- `ss_error_t ss_process_isr_queue(void)` (with `SS_ENABLE_ISR_SAFE`) — **Required** to deliver ISR events: call it from the main loop. Delivers them oldest first, resolving names against the default namespace at delivery. Returns the last emission error (e.g. `SS_ERR_NOT_FOUND`). `ss_cleanup()` discards queued events.
- On cores without compare-and-swap (Cortex-M0, AVR) define `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` (e.g. save PRIMASK + `__disable_irq()` / restore).

### Deferred Emission (2 functions)

```c
ss_error_t ss_emit_deferred(const char* signal_name, const ss_data_t* data);
ss_error_t ss_flush_deferred(void);
```

- `ss_emit_deferred()` — Queue emission for later (FIFO, `SS_DEFERRED_QUEUE_SIZE` entries, default 64, 16 with `SS_EMBEDDED_BUILD`). The queue stores a **copy** of the payload: string and custom data are copied, so the caller may free its data right after the call. The `custom_cleanup` function is NOT called for the copy (slots see the bytes, not your object's lifetime). In static mode string/custom payloads must fit `SS_DEFERRED_PAYLOAD_SIZE` (default 32) bytes. `SS_ERR_WOULD_OVERFLOW` when the queue is full, the name is too long, or the payload is too large.
- `ss_flush_deferred()` — Emit the entries queued when it starts, oldest first. Entries queued later (by slots) wait for the next flush, even if a slot runs a nested `ss_flush_deferred()`. Returns the last emission error.
- Pending entries are released by `ss_cleanup()`.

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

- `ss_data_create()` — Heap-allocates a zeroed `ss_data_t` (in static mode too). Must be freed with `ss_data_destroy()`.
- `ss_data_set_string()` — **Copies** the string (with `SS_MALLOC`, or `SS_STRDUP` if defined).
- `ss_data_set_custom()` — **Copies** the data via `memcpy` into a new allocation.
- Every `ss_data_set_*` first releases the payload the object previously owned (switching type no longer leaks). `ss_data_set_string`/`ss_data_set_custom` keep the old value if allocation fails (`SS_ERR_MEMORY`).
- `ss_data_destroy()` — Frees the owned string/custom payload (calling `custom_cleanup` first) and the struct.
- Getters return default/NULL on type mismatch — they do not coerce types.

**Ownership contract:**
- An object from `ss_data_create()` owns the string/custom data stored by the setters. Never assign `value.s_val` or `custom_data` directly on such an object.
- `custom_cleanup` receives the library's copy so it can release resources the copy *references*; it must NOT free the pointer it receives (the library frees it).
- A stack `ss_data_t` filled in by hand (as the `ss_emit_*` helpers do) borrows its pointers: pass it to `ss_emit*`, but never to `ss_data_destroy()` or `ss_data_set_*`.

### Introspection (3 functions, requires `SS_ENABLE_INTROSPECTION`)

```c
size_t     ss_get_signal_count(void);
ss_error_t ss_get_signal_list(ss_signal_info_t** list, size_t* count);
void       ss_free_signal_list(ss_signal_info_t* list, size_t count);
```

- `ss_signal_info_t` contains: `name`, `slot_count`, `description`, `priority`.
- `name` and `description` are copies owned by the caller and stay valid after signals change. Must call `ss_free_signal_list()` after `ss_get_signal_list()`. Allocates in both memory modes.

### Memory Stats (2 functions, requires `SS_ENABLE_MEMORY_STATS`)

```c
ss_error_t ss_get_memory_stats(ss_memory_stats_t* stats);
void       ss_reset_memory_stats(void);
```

- Dynamic mode: `total_bytes_allocated` = heap bytes the library holds, `peak_bytes_allocated` = its maximum. Static mode: both = size of the context. `signals_used` / `slots_used` count live ones. Reset sets the peak to the current value.

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

- The handler is called, after the library lock is released, for every failing call made while the library is initialized, including parameter errors (NULL arguments, handle 0, NULL/destroyed batch, too-long names). `msg` is the signal name when there is one, else `ss_error_string(err)`.

### Configuration (4 functions)

```c
void   ss_set_max_slots_per_signal(size_t max_slots);
size_t ss_get_max_slots_per_signal(void);
void   ss_set_thread_safe(int enabled);   // Requires SS_ENABLE_THREAD_SAFETY
int    ss_is_thread_safe(void);           // Requires SS_ENABLE_THREAD_SAFETY
```

### Namespace (3 functions)

```c
ss_error_t  ss_set_namespace(const char* ns);
const char* ss_get_namespace(void);
ss_error_t  ss_emit_namespaced(const char* ns, const char* signal_name, const ss_data_t* data);
```

- Convention: `ns::signal_name`.
- While a default namespace is set, **every** name without `"::"` is resolved as `"ns::name"` by all name-taking functions (register, unregister, exists, connect(_ex), disconnect(_all), emit*, emit_deferred, batch_add, get_perf_stats; ISR events at delivery). Names containing `"::"` are used as given. Register and connect while the same namespace is set, or use fully qualified names.
- `NULL` or `""` clears it. `SS_ERR_WOULD_OVERFLOW` if even `"ns::x"` would not fit. `ss_get_namespace()` returns NULL when unset; the pointer is valid until the next change. `ss_set_namespace(ss_get_namespace())` is a safe no-op.
- `ss_emit_namespaced(ns, name, data)` always emits `"ns::name"` and ignores the default namespace.

### Batch Operations (4 functions)

```c
ss_batch_t* ss_batch_create(void);
void        ss_batch_destroy(ss_batch_t* batch);
ss_error_t  ss_batch_add(ss_batch_t* batch, const char* signal_name, const ss_data_t* data);
ss_error_t  ss_batch_emit(ss_batch_t* batch);
```

- `ss_batch_create()` allocates in both memory modes.
- `ss_batch_add()` copies payloads exactly like `ss_emit_deferred` (custom cleanup not called for copies; static payload limit `SS_DEFERRED_PAYLOAD_SIZE`). Capacity `SS_BATCH_MAX_ENTRIES` (defaults to `SS_DEFERRED_QUEUE_SIZE`); `SS_ERR_WOULD_OVERFLOW` when full.
- `ss_batch_emit()` emits the entries, oldest first, and empties the batch; the batch can be reused. Returns `SS_ERR_NULL_PARAM` if the library is not initialized. Slots may add to the batch being emitted (delivered by the next `ss_batch_emit`) or destroy it (freed when `ss_batch_emit` returns; remaining entries dropped).
- `ss_batch_destroy()` releases remaining entries and the batch.
- A batch must not be used from several threads at once.

### Debug (2 functions, requires `SS_ENABLE_DEBUG_TRACE`)

```c
void ss_enable_trace(FILE* output);
void ss_disable_trace(void);
```

- Trace is off until `ss_enable_trace(stream)` names a stream; `ss_disable_trace()` stops it. `ss_lib.h` includes `<stdio.h>` itself in this configuration.

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

**`ss_emit_string()` does NOT copy the string.** It builds a stack `ss_data_t` whose `value.s_val` points at your string and emits it synchronously. Slots see your pointer only for the duration of their call.

**`ss_data_set_string()` DOES copy the string** into memory owned by the `ss_data_t` object.

**Rule:** with `ss_emit`/`ss_emit_string`, the string must outlive the emission, and a slot that wants to keep it must copy it (the pointer is only valid during the slot call, whatever the emitter used).

`ss_emit_deferred()` and `ss_batch_add()` copy string and custom payloads internally, so temporary strings are safe with them; slots receive a pointer into the queue's copy, valid only during the slot call.

## Priority System

```c
SS_PRIORITY_LOW      = 0
SS_PRIORITY_NORMAL   = 5   // Default for ss_connect()
SS_PRIORITY_HIGH     = 10
SS_PRIORITY_CRITICAL = 15
```

- Higher priority = executed first. Equal priorities run in connection order.
- Slots are sorted by priority at connection time (insertion into a singly-linked list).
- Priority cannot be changed after connection. Must disconnect and reconnect.

## Error Codes

```c
SS_OK = 0                // Success
SS_ERR_NULL_PARAM        // NULL parameter passed
SS_ERR_MEMORY            // Memory allocation failed
SS_ERR_NOT_FOUND         // Signal or slot not found
SS_ERR_ALREADY_EXISTS    // Signal already registered
SS_ERR_INVALID_TYPE      // Reserved, not currently returned
SS_ERR_BUFFER_TOO_SMALL  // Reserved, not currently returned (ss_init_static falls back instead)
SS_ERR_MAX_SLOTS         // Signal already has max_slots_per_signal live slots
SS_ERR_TIMEOUT           // Reserved, not currently returned
SS_ERR_WOULD_OVERFLOW    // Name, payload, description or queue/pool exceeds a fixed limit
SS_ERR_ISR_UNSAFE        // Reserved, not currently returned
SS_ERR_CONFIG_MISMATCH   // Caller and library built with different flags (ss_init_abi / ss_init_static_abi)
```

Most functions return `ss_error_t`; calls made before `ss_init()` return `SS_ERR_NULL_PARAM`. Always check return values, especially `ss_init()`, `ss_connect()`, and `ss_emit()`.

## Thread Safety

Thread safety is **compiled in** by default (`SS_ENABLE_THREAD_SAFETY 1`) but **disabled at runtime**. You must explicitly enable it:

```c
ss_init();
ss_set_thread_safe(1);  // Must call BEFORE any multi-threaded access
// ... use from multiple threads ...
ss_set_thread_safe(0);  // Disable before cleanup if desired
ss_cleanup();
```

- `ss_init()` creates one **recursive** mutex (pthread recursive mutex / Windows `CRITICAL_SECTION`); `ss_set_thread_safe()` only switches locking on or off. Change it only while no other thread uses the library.
- Every function touching shared state locks (signals, slots, deferred queue, namespace, error handler, profiling flag, max slots).
- Slots run with the mutex held. They may call back into the library on the same thread, but must NOT wait (join, condition variable, blocking queue) for another thread that itself calls the library: deadlock.
- `ss_init()` / `ss_cleanup()` are not thread-safe; a batch must not be shared between threads.
- Single lock: not designed for high-contention scenarios. `ss_is_thread_safe()` returns current state.

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
| `SS_ENABLE_ISR_SAFE` | 0 | ISR-safe emission + `ss_process_isr_queue` |

### Limits

| Macro | Default | Description |
|-------|---------|-------------|
| `SS_MAX_SIGNALS` | 32 | Max signals (static mode only) |
| `SS_MAX_SLOTS` | 128 | Max total slots (static mode only) |
| `SS_MAX_SIGNAL_NAME_LENGTH` | 256 (dynamic) / 32 (static) | Max signal name length |
| `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL` | 100 | Max slots per signal |
| `SS_DEFERRED_QUEUE_SIZE` | 64 (16 with `SS_EMBEDDED_BUILD`) | Deferred queue size |
| `SS_BATCH_MAX_ENTRIES` | `SS_DEFERRED_QUEUE_SIZE` | Entries per batch |
| `SS_DEFERRED_PAYLOAD_SIZE` | 32 | Static mode: max string/custom bytes per deferred/batch entry |
| `SS_MAX_DESCRIPTION_LENGTH` | 32 | Static mode: description buffer incl. NUL |
| `SS_STATIC_CONTEXT` | 1 | Static mode: 1 = `ss_init()` context in `.bss`; 0 = no built-in context (saves RAM with `ss_init_static` pools), `ss_init()` uses `SS_CALLOC` |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR emission queue size |
| `SS_ISR_CRITICAL_ENTER/EXIT` | undefined | Interrupt-disable hooks for cores without CAS |
| `SS_CACHE_LINE_SIZE` | 64 | Not used by the implementation |

### Presets

- **`SS_MINIMAL_BUILD`** — Disables: thread safety, introspection, custom data, performance stats, memory stats.
- **`SS_EMBEDDED_BUILD`** — Enables static memory, disables thread safety, sets max slots per signal to 10, deferred queue to 16.
- Define presets and flags on the command line for the library AND the application.

### Static Memory Mode Rules

- `ss_init()` uses static storage (unless `SS_STATIC_CONTEXT=0`); register, connect, emit, deferred, namespace, disconnect, unregister and cleanup never touch the heap.
- Still allocating (by design, they hand you an object): `ss_data_create`, `ss_data_set_string`, `ss_data_set_custom`, `ss_get_signal_list`, `ss_batch_create`. Avoid them where no heap exists; use the `ss_emit_*` helpers.
- Limits are enforced with `SS_ERR_WOULD_OVERFLOW` (names, descriptions, payloads, full pools), never silent truncation.

### Custom Allocators

Override when building the library (e.g. `-DSS_MALLOC(n)=my_malloc(n)` or a wrapper header included first):

```c
#define SS_MALLOC(size)        my_malloc(size)
#define SS_FREE(ptr)           my_free(ptr)
#define SS_CALLOC(count, size) my_calloc(count, size)
/* optional: SS_STRDUP(str); by default strings are copied with SS_MALLOC */
```

Overriding `SS_MALLOC`/`SS_FREE`/`SS_CALLOC` routes every allocation, including string copies, so `ss_data_destroy` / `ss_free_signal_list` free with the matching `SS_FREE`.

## Connection Handles

`ss_connection_t` is an opaque `uintptr_t` returned by `ss_connect_ex()`:

```c
ss_connection_t handle;
ss_connect_ex("sig", my_slot, NULL, SS_PRIORITY_HIGH, &handle);
// ... later ...
ss_disconnect_handle(handle);  // Disconnect without knowing signal name
```

Handles are monotonically increasing integers starting at 1. A handle of 0 is never assigned and can be used as "no handle" sentinel (`ss_disconnect_handle(0)` returns `SS_ERR_NULL_PARAM`).

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
/* Build library and application with -DSS_EMBEDDED_BUILD (never #define it here) */
#include "ss_lib.h"

int main(void) {
    if (ss_init_abi(SS_ABI_TAG) != SS_OK) return 1;   /* static mode: context in .bss, no heap */
    ss_signal_register("sensor");
    ss_connect("sensor", read_sensor, NULL);
    ss_emit_int("sensor", adc_read());
    ss_cleanup();
    return 0;
}

/* Alternative: put the context in your own buffer (any memory mode; with
 * -DSS_STATIC_CONTEXT=0 this saves the built-in .bss context) */
static unsigned char pool[2048];
/* ... */
if (sizeof(pool) < ss_get_required_pool_size()) { /* too small: would fall back to ss_init() */ }
if (ss_init_static_abi(pool, sizeof(pool), SS_ABI_TAG) != SS_OK) { /* ... */ }
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

// With nested resources: cleanup releases what the copy references, never the copy itself
typedef struct { int* items; size_t n; } List;
void list_cleanup(void* p) { free(((List*)p)->items); }   // NOT free(p)
```

### ISR Emission

```c
// Compile with -DSS_ENABLE_ISR_SAFE=1

// In ISR handler (no locks, no malloc):
void TIM2_IRQHandler(void) {
    ss_emit_from_isr("timer_tick", tick_count);  // int only
}

// In main loop — deliver queued ISR events (nothing is delivered otherwise):
for (;;) {
    ss_process_isr_queue();   // oldest first, slots run here in normal context
    /* ... */
}
```

### Deferred / Batch

```c
// Deferred: queue now, emit later
ss_emit_deferred("update", data);   // Payload copied; data may be destroyed now
ss_emit_deferred("refresh", NULL);
ss_flush_deferred();                 // Emits queued entries; slots' new entries wait

// Batch: group emissions
ss_batch_t* batch = ss_batch_create();
ss_batch_add(batch, "save", data1);  // Payload copied
ss_batch_add(batch, "log", data2);
ss_batch_emit(batch);                // Emits all, empties the batch
ss_batch_destroy(batch);             // Frees batch
```

### Namespace

```c
ss_set_namespace("audio");
ss_signal_register("volume_changed");                 // registers "audio::volume_changed"
ss_connect("volume_changed", on_volume, NULL);        // connects to "audio::volume_changed"
ss_emit_int("volume_changed", 7);                     // emits "audio::volume_changed"
ss_emit_int("ui::click", 1);                          // contains "::": used as given
ss_emit_namespaced("video", "frame", data);           // "video::frame", ignores default
ss_set_namespace(NULL);                               // clear
```

### Re-entrant Slot

```c
void one_shot(const ss_data_t* data, void* ud) {
    (void)data; (void)ud;
    ss_disconnect("ready", one_shot);   // safe: freed after this emission
}
void shutdown_slot(const ss_data_t* data, void* ud) {
    (void)data; (void)ud;
    ss_cleanup();                        // safe: deferred until the outermost call returns
}
```
