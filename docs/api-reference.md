# API Reference

## Overview

SS_Lib provides a signal-slot mechanism for C programs. This reference covers all public functions, data types, and configuration options of version 3.0.0.

All library state lives in a **bus** (`ss_bus_t`). Every function that uses that state takes the bus as its first argument; the data functions, `ss_version()`, `ss_error_string()`, `ss_free_signal_list()` and `ss_emit_from_isr()` take none. Buses share nothing: the same signal name on two buses names two different signals, and each bus has its own slots, deferred queue, namespace, error handler, slot limit, connection handles, statistics and trace stream. The library has no default bus.

## Version

```c
#define SS_VERSION_MAJOR 3
#define SS_VERSION_MINOR 0
#define SS_VERSION_PATCH 0
#define SS_VERSION_STRING "3.0.0"

const char* ss_version(void);   /* returns SS_VERSION_STRING of the compiled library */
```

## Error Codes

```c
typedef enum {
    SS_OK = 0,              /* Success */
    SS_ERR_NULL_PARAM,      /* NULL parameter passed */
    SS_ERR_MEMORY,          /* Memory allocation failed */
    SS_ERR_NOT_FOUND,       /* Signal or slot not found */
    SS_ERR_ALREADY_EXISTS,  /* Signal already registered */
    SS_ERR_INVALID_TYPE,    /* Custom data queued without SS_ENABLE_CUSTOM_DATA */
    SS_ERR_BUFFER_TOO_SMALL,/* Pool too small for ss_bus_create_in() */
    SS_ERR_MAX_SLOTS,       /* Maximum slots per signal reached */
    SS_ERR_TIMEOUT,         /* Reserved, not currently returned */
    SS_ERR_WOULD_OVERFLOW,  /* Name, payload, queue or the built-in buses exceed a fixed limit */
    SS_ERR_ISR_UNSAFE,      /* Reserved, not currently returned */
    SS_ERR_CONFIG_MISMATCH  /* Caller's SS_ABI_TAG differs from the library's,
                               or a bus flag the library does not support */
} ss_error_t;
```

Functions that take a bus return `SS_ERR_NULL_PARAM` (or 0 / NULL, or do nothing) when the bus is NULL or is being destroyed (see [ss_bus_destroy](#ss_bus_destroy)).

## Data Types

Every public struct has the same size and member offsets, and every enum the
same values, in every configuration: feature flags only decide which
functions are declared. `make check-abi` (`scripts/check_abi.sh`) verifies
this for each configuration the project tests.

### ss_data_type_t

```c
typedef enum {
    SS_TYPE_VOID,       /* No data */
    SS_TYPE_INT,        /* Integer */
    SS_TYPE_FLOAT,      /* Float */
    SS_TYPE_DOUBLE,     /* Double */
    SS_TYPE_STRING,     /* Null-terminated string */
    SS_TYPE_POINTER,    /* Generic pointer */
    SS_TYPE_CUSTOM      /* Custom data with size (value.custom) */
} ss_data_type_t;
```

`SS_TYPE_CUSTOM` is declared in every configuration. The custom data functions need `SS_ENABLE_CUSTOM_DATA`, and a library built without it refuses to queue custom data (`SS_ERR_INVALID_TYPE` from `ss_emit_deferred()` and `ss_batch_add()`). `ss_emit()` passes any object on to the slots unchanged.

### ss_priority_t

```c
typedef enum {
    SS_PRIORITY_LOW = 0,        /* Lowest priority */
    SS_PRIORITY_NORMAL = 5,     /* Default priority */
    SS_PRIORITY_HIGH = 10,      /* High priority */
    SS_PRIORITY_CRITICAL = 15   /* Highest priority */
} ss_priority_t;
```

### ss_data_t

```c
typedef struct ss_data {
    ss_data_type_t type;
    unsigned char owned;    /* nonzero: the payload is the library's copy */
    union {
        int i_val;
        float f_val;
        double d_val;
        const char* s_val;
        void* p_val;
        struct {
            void* ptr;
            size_t size;
            ss_cleanup_func_t cleanup;   /* only for an owned copy */
        } custom;                        /* SS_TYPE_CUSTOM */
    } value;
} ss_data_t;                             /* same layout in every configuration */

#define SS_DATA_INIT { SS_TYPE_VOID, 0, { 0 } }  /* empty object */
```

`sizeof(ss_data_t)` is 32 bytes on 64-bit hosts and 24 on 32-bit Arm (Cortex-M), whatever the configuration. Custom data is in `value.custom`; read it with `ss_data_get_custom()` rather than the fields.

**Ownership:** the `owned` field records whether the object owns its string or custom payload. Callers read it but never write it.
- Only the copying setters, `ss_data_set_string()` and `ss_data_set_custom()`, set `owned`. The copy belongs to the object and is freed exactly once: by the next `ss_data_set_*` call, by `ss_data_clear()`, or by `ss_data_destroy()`.
- Everything else borrows and leaves `owned` at 0: direct field writes, `ss_data_set_string_ref()` / `ss_data_set_custom_ref()`, the `ss_emit_*` helpers, and the data slots receive (also when the emitter's object owns its payload, and for deferred and batch copies). The library never frees a borrowed payload, so misusing a borrowed object leaks at worst.
- Start every object from `SS_DATA_INIT` or `ss_data_init()`; `ss_data_create()` returns an initialized one. Release a stack object that used a copying setter with `ss_data_clear()` before it goes out of scope. Do not write the payload fields of an object by hand while it owns its payload: clear it first.
- `value.custom.cleanup` is called once with the library's copy of the custom data (read-only) and its size, just before the library frees it, so it can release resources the copy refers to. It must not free the pointer itself.

### ss_signal_info_t

Available when `SS_ENABLE_INTROSPECTION` is enabled:

```c
typedef struct ss_signal_info {
    char* name;
    size_t slot_count;
    const char* description;
    ss_priority_t priority;
} ss_signal_info_t;
```

### ss_memory_stats_t

Available when `SS_ENABLE_MEMORY_STATS` is enabled:

```c
typedef struct ss_memory_stats {
    size_t signals_allocated;
    size_t signals_used;
    size_t slots_allocated;
    size_t slots_used;
    size_t total_bytes_allocated;
    size_t peak_bytes_allocated;
    size_t string_bytes;
} ss_memory_stats_t;
```

### ss_perf_stats_t

Available when `SS_ENABLE_PERFORMANCE_STATS` is enabled:

```c
typedef struct ss_perf_stats {
    uint64_t total_emissions;
    uint64_t total_time_ns;
    uint64_t avg_time_ns;
    uint64_t max_time_ns;
    uint64_t min_time_ns;
} ss_perf_stats_t;
```

### Function Pointer Types

```c
typedef void (*ss_slot_func_t)(const ss_data_t* data, void* user_data);
typedef void (*ss_cleanup_func_t)(const void* data, size_t size);
typedef uintptr_t ss_connection_t;
```

---

## Buses

### ss_bus_t

```c
typedef struct ss_bus ss_bus_t;   /* opaque */
```

A bus holds signals, slots, the deferred queue, the default namespace and the per-bus settings. Its layout depends on the configuration, so callers only ever hold a pointer. Where the bus itself lives depends on how it was created:

| Created by | Dynamic mode | Static mode |
|------------|--------------|-------------|
| `ss_bus_create()` | Heap (`SS_CALLOC`) | One of `SS_STATIC_BUSES` built-in buses in `.bss`; never the heap |
| `ss_bus_create_in()` | Caller's pool | Caller's pool |

Signals and slots follow the memory mode: heap nodes in dynamic mode, the bus's own fixed arrays in static mode.

### SS_BUS_THREAD_SAFE

```c
#define SS_BUS_THREAD_SAFE 0x1u
```

Creation flag: the bus gets its own recursive mutex, and every call on it locks that mutex. Needs a library built with `SS_ENABLE_THREAD_SAFETY=1`; otherwise creation returns `SS_ERR_CONFIG_MISMATCH`. A bus created without the flag has no mutex and must be used from one thread at a time. The choice is fixed for the bus's lifetime. See [Thread Safety](#thread-safety).

### ss_bus_create

```c
#define ss_bus_create(bus, flags) ss_bus_create_abi((bus), (flags), SS_ABI_TAG)
ss_error_t ss_bus_create_abi(ss_bus_t** bus, unsigned int flags, size_t abi_tag);
```

Create a bus: `ss_bus_t* bus; ss_bus_create(&bus, 0);`. The macro passes the caller's `SS_ABI_TAG`, so every creation runs the [ABI check](#abi-check). Callers that cannot use macros (FFI) call `ss_bus_create_abi()` with the tag value their bindings were built for.

In dynamic mode the bus is allocated with `SS_CALLOC`; creation is safe from any thread. In static mode it takes the first unused built-in bus and never uses the heap. Claiming and releasing built-in buses is not locked: create and destroy them from one thread, normally at startup.

**Parameters:**
- `bus` — Receives the new bus; set to NULL on every failure after the NULL check
- `flags` — 0 or `SS_BUS_THREAD_SAFE`
- `abi_tag` — `SS_ABI_TAG` as the caller sees it

**Returns**, checked in this order:
1. `SS_ERR_NULL_PARAM` if `bus` is NULL.
2. `SS_ERR_CONFIG_MISMATCH` if `abi_tag` differs from the library's `SS_ABI_TAG`, or `flags` has a bit the library does not support (`SS_BUS_THREAD_SAFE` without `SS_ENABLE_THREAD_SAFETY`, or any unknown bit).
3. `SS_ERR_WOULD_OVERFLOW` (static mode) if every built-in bus is in use; `SS_ERR_MEMORY` (dynamic mode) if the allocation fails.
4. `SS_ERR_MEMORY` if the mutex cannot be created.
5. `SS_OK`.

Failed creation reaches no error handler: there is no bus to hold one.

### ss_bus_create_in

```c
#define ss_bus_create_in(bus, pool, size, flags) \
    ss_bus_create_in_abi((bus), (pool), (size), (flags), SS_ABI_TAG)
ss_error_t ss_bus_create_in_abi(ss_bus_t** bus, void* pool, size_t pool_size,
                                unsigned int flags, size_t abi_tag);
size_t ss_bus_pool_size(void);
```

Create a bus in caller-owned memory, in either memory mode. The pool needs no particular alignment; `ss_bus_pool_size()` bytes (the bus plus alignment slack) always suffice. The pool must outlive the bus; `ss_bus_destroy()` never frees it, and after it the pool can hold a new bus. In static mode this is how to have more buses than `SS_STATIC_BUSES` without the heap.

**Returns**, checked in this order: `SS_ERR_NULL_PARAM` if `bus` is NULL; `SS_ERR_CONFIG_MISMATCH` as for `ss_bus_create()`; `SS_ERR_NULL_PARAM` if `pool` is NULL; `SS_ERR_BUFFER_TOO_SMALL` if the pool, after aligning its start, cannot hold the bus (the pool is not written); `SS_ERR_MEMORY` if the mutex cannot be created; `SS_OK`.

```c
static unsigned char* pool;
ss_bus_t* bus;
pool = malloc(ss_bus_pool_size());     /* or a static buffer known to be big enough */
if (ss_bus_create_in(&bus, pool, ss_bus_pool_size(), 0) != SS_OK) { /* ... */ }
/* ... */
ss_bus_destroy(bus);
free(pool);
```

`ss_bus_pool_size()` is a runtime value: it depends on the library's configuration.

### ABI check

```c
#define SS_ABI_REVISION 1   /* bumped when a public layout changes (1 to 15) */
#define SS_ABI_TAG ((size_t)(sizeof(ss_data_t) * 16u + SS_ABI_REVISION))
```

The create macros pass the caller's `SS_ABI_TAG`, and creation returns `SS_ERR_CONFIG_MISMATCH` when it differs from the library's. The tag combines `SS_ABI_REVISION` with `sizeof(ss_data_t)` as the caller's compilation sees it, and fits a 16-bit `size_t`. Configuration macros do not change public layouts, so the tag catches what they cannot: a header from another version, and compiler options that change layouts, such as `-fshort-enums` or structure packing. Feature flags must still be the same for the library and every caller, because they decide which functions are declared: a difference shows up as a compile or link error.

### ss_bus_destroy

```c
void ss_bus_destroy(ss_bus_t* bus);
```

Destroy a bus: release its signals, slots and pending deferred entries, destroy its mutex, and free the bus (heap) or release its place (built-in bus). A pool is left to its owner. NULL is a no-op. Not thread-safe with respect to other threads using the same bus. Destroy the batches created for the bus first.

Called from inside a slot, the destruction is deferred until the bus's outermost call returns. Meanwhile the remaining slots of emissions in progress on the bus are skipped, a flush stops, and the bus refuses every call: functions return `SS_ERR_NULL_PARAM` (or 0 / NULL, or do nothing), and another `ss_bus_destroy()` is a no-op. Other buses are not affected.

After the destruction completes, the bus must not be used, like memory after `free()`: the pointer is dangling, and a later call is undefined behavior. ISR events still queued stay in the shared ISR queue (see [ss_process_isr_queue](#ss_process_isr_queue)).

### ss_version

```c
const char* ss_version(void);
```

Runtime version string of the compiled library, equal to `SS_VERSION_STRING`.

---

## Signal Management

### ss_signal_register

```c
ss_error_t ss_signal_register(ss_bus_t* bus, const char* signal_name);
```

Register a new signal on the bus with default priority (`SS_PRIORITY_NORMAL`). Equivalent to `ss_signal_register_ex(bus, signal_name, NULL, SS_PRIORITY_NORMAL)`.

**Parameters:**
- `bus` — The bus
- `signal_name` — Name for the signal, unique on this bus (must be shorter than `SS_MAX_SIGNAL_NAME_LENGTH`)

**Returns:** `SS_OK` on success, `SS_ERR_ALREADY_EXISTS` if the signal exists on this bus, `SS_ERR_NULL_PARAM` if the bus is NULL or the name is NULL or empty, `SS_ERR_WOULD_OVERFLOW` if the name is too long or (static mode) all `SS_MAX_SIGNALS` entries of the bus are in use, `SS_ERR_MEMORY` if allocation fails (dynamic mode).

### ss_signal_register_ex

```c
ss_error_t ss_signal_register_ex(ss_bus_t* bus, const char* signal_name,
                                 const char* description, ss_priority_t priority);
```

Register a signal with metadata.

**Parameters:**
- `bus` — The bus
- `signal_name` — Name for the signal, unique on this bus
- `description` — Human-readable description (can be NULL). Stored only when `SS_ENABLE_INTROSPECTION` is on (it is only read by `ss_get_signal_list()`). In static mode it must fit `SS_MAX_DESCRIPTION_LENGTH` bytes including the terminator.
- `priority` — Default priority for the signal

**Returns:** Same as `ss_signal_register`; also `SS_ERR_WOULD_OVERFLOW` if the description is too long (static mode).

### ss_signal_unregister

```c
ss_error_t ss_signal_unregister(ss_bus_t* bus, const char* signal_name);
```

Unregister a signal and disconnect all connected slots. Safe to call from a slot of the same signal: the remaining slots of that emission are skipped, the memory is released when the emission ends, and the name can be registered again at once.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if the signal doesn't exist on this bus.

### ss_signal_exists

```c
int ss_signal_exists(ss_bus_t* bus, const char* signal_name);
```

Check if a signal is registered on the bus.

**Returns:** 1 if the signal exists, 0 otherwise. Returns 0 if the bus or name is NULL.

---

## Connection Management

Connection handles are per bus: two buses may hand out the same handle value, and a handle only means something to the bus that returned it.

### ss_connect

```c
ss_error_t ss_connect(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot,
                      void* user_data);
```

Connect a slot to a signal with default priority (`SS_PRIORITY_NORMAL`). Equivalent to `ss_connect_ex(bus, signal_name, slot, user_data, SS_PRIORITY_NORMAL, NULL)`. The slot signature has no bus parameter; a slot that needs its bus gets it through `user_data`.

**Parameters:**
- `bus` — The bus
- `signal_name` — Name of the signal
- `slot` — Callback function
- `user_data` — User data passed to the callback (can be NULL)

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if the signal doesn't exist, `SS_ERR_MAX_SLOTS` if the per-signal limit is reached (only live slots count), `SS_ERR_NULL_PARAM` if the bus, signal_name or slot is NULL, `SS_ERR_WOULD_OVERFLOW` if all `SS_MAX_SLOTS` entries of the bus are in use (static mode), `SS_ERR_MEMORY` if allocation fails (dynamic mode).

### ss_connect_ex

```c
ss_error_t ss_connect_ex(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot,
                         void* user_data, ss_priority_t priority,
                         ss_connection_t* handle);
```

Connect a slot with priority and optional connection handle.

**Parameters:**
- `bus` — The bus
- `signal_name` — Name of the signal
- `slot` — Callback function
- `user_data` — User data passed to the callback
- `priority` — Execution priority (higher values execute first; equal priorities run in connection order)
- `handle` — Output connection handle for later disconnection (can be NULL)

**Returns:** Same as `ss_connect`.

### ss_disconnect

```c
ss_error_t ss_disconnect(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot);
```

Disconnect the first matching slot from a signal. If the signal is currently emitting, the slot is marked, is not called again, and is freed when the emission ends.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if not connected.

### ss_disconnect_handle

```c
ss_error_t ss_disconnect_handle(ss_bus_t* bus, ss_connection_t handle);
```

Disconnect a slot using its connection handle. Searches all signals of the bus for the matching handle. If the signal is currently emitting, the slot is marked, is not called again, and is freed when the emission ends.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if no slot of this bus has the handle, `SS_ERR_NULL_PARAM` if the bus is NULL or the handle is 0.

### ss_disconnect_all

```c
ss_error_t ss_disconnect_all(ss_bus_t* bus, const char* signal_name);
```

Disconnect all slots from a signal. If the signal is currently emitting, the remaining slots of that emission are skipped and freed when it ends.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if the signal doesn't exist.

---

## Signal Emission

### ss_emit

```c
ss_error_t ss_emit(ss_bus_t* bus, const char* signal_name, const ss_data_t* data);
```

Emit a signal, invoking all its connected slots in priority order. Slots may call any library function, on this bus or another, including on the signal being emitted:
- Slots disconnected during the emission are not called; they are freed when it ends.
- A slot connected during the emission may or may not be called by it, depending on where its priority places it in the list; do not rely on either.
- `ss_signal_unregister()` or `ss_bus_destroy()` from a slot skips the remaining slots (see those functions).
- With thread-safe buses, cross-bus calls from slots must go in one direction only (see [Thread Safety](#thread-safety)).

**Parameters:**
- `bus` — The bus
- `signal_name` — Name of the signal to emit
- `data` — Data to pass to slots (can be NULL). Slots see it as borrowed: if the object owns its payload, they receive a copy with `owned` cleared, pointing to the same payload.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if the signal doesn't exist on this bus.

### ss_emit_void

```c
ss_error_t ss_emit_void(ss_bus_t* bus, const char* signal_name);
```

Emit a signal with `SS_TYPE_VOID` data.

### ss_emit_int

```c
ss_error_t ss_emit_int(ss_bus_t* bus, const char* signal_name, int value);
```

Emit a signal with an integer value.

### ss_emit_float

```c
ss_error_t ss_emit_float(ss_bus_t* bus, const char* signal_name, float value);
```

Emit a signal with a float value.

### ss_emit_double

```c
ss_error_t ss_emit_double(ss_bus_t* bus, const char* signal_name, double value);
```

Emit a signal with a double value.

### ss_emit_string

```c
ss_error_t ss_emit_string(ss_bus_t* bus, const char* signal_name, const char* value);
```

Emit a signal with a string value. The string pointer is passed directly (not copied), so it must remain valid during emission. Slots receive a borrowed pointer and must not free it.

### ss_emit_pointer

```c
ss_error_t ss_emit_pointer(ss_bus_t* bus, const char* signal_name, void* value);
```

Emit a signal with a pointer value.

### ss_emit_from_isr

```c
ss_error_t ss_emit_from_isr(const char* signal_name, int value);
```

Queue an integer event from an interrupt handler. Requires `SS_ENABLE_ISR_SAFE=1`. No locks, no allocation, no slot calls: the name and value are stored in a fixed queue of `SS_ISR_QUEUE_SIZE` entries, claimed with atomic compare-and-swap, and delivered later by `ss_process_isr_queue()`. Safe with nested interrupts and from several threads.

The ISR queue is global, shared by every bus, and takes no bus: it is usable before any bus exists, and an event goes to whichever bus next processes the queue.

Atomics come from C11 `<stdatomic.h>`, GCC/Clang `__atomic` builtins or MSVC Interlocked functions. On targets without compare-and-swap (Cortex-M0, AVR) define `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` (see [Configuration](configuration.md#isr-safe-emission)).

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if signal_name is NULL or empty, `SS_ERR_WOULD_OVERFLOW` if the queue is full or the name is longer than `SS_MAX_SIGNAL_NAME_LENGTH - 1`.

### ss_process_isr_queue

```c
ss_error_t ss_process_isr_queue(ss_bus_t* bus);
```

Deliver the queued ISR events, oldest first, to `bus`, from normal context. Requires `SS_ENABLE_ISR_SAFE=1`. Call it regularly from the main loop; slots run here. Names are resolved against the bus's default namespace at delivery time. Events queued while it runs wait for the next call. If a slot destroys the bus, delivery stops and the remaining events stay queued. Destroying a bus leaves queued events in the shared queue for the next bus that processes it.

**Returns:** `SS_OK`, `SS_ERR_NULL_PARAM` if the bus is NULL or being destroyed, or the last emission error (e.g. `SS_ERR_NOT_FOUND` for a signal not registered on the bus).

---

## Namespace Support

### ss_set_namespace

```c
ss_error_t ss_set_namespace(ss_bus_t* bus, const char* ns);
```

Set the bus's default namespace. Pass NULL or `""` to clear. While it is set, every signal name without `"::"` is resolved as `"ns::name"` by all functions that take a signal name on this bus (register, unregister, exists, connect, disconnect, emit, deferred and batch emission, perf stats; ISR events at delivery). Names containing `"::"` are used as given. Deferred and batch entries are resolved when they are queued. No allocation. Other buses keep their own namespace.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if the bus is NULL, `SS_ERR_WOULD_OVERFLOW` if even `"ns::x"` would not fit in `SS_MAX_SIGNAL_NAME_LENGTH`.

### ss_get_namespace

```c
const char* ss_get_namespace(ss_bus_t* bus);
```

Get the bus's current default namespace.

**Returns:** Namespace string (valid until the next change), or NULL if not set or the bus is NULL. Passing it back to `ss_set_namespace()` is safe (no-op). The string is the bus's own buffer and is read without the lock, so with several threads read it only while no other thread can call `ss_set_namespace()` on the bus, or copy it then.

### ss_emit_namespaced

```c
ss_error_t ss_emit_namespaced(ss_bus_t* bus, const char* ns, const char* signal_name,
                              const ss_data_t* data);
```

Emit `"ns::signal_name"`. Ignores the default namespace.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if the bus, `ns` or `signal_name` is NULL, `SS_ERR_WOULD_OVERFLOW` if the combined name does not fit in `SS_MAX_SIGNAL_NAME_LENGTH`, `SS_ERR_NOT_FOUND` if the signal doesn't exist.

---

## Deferred Emission

Each bus has its own deferred queue.

### ss_emit_deferred

```c
ss_error_t ss_emit_deferred(ss_bus_t* bus, const char* signal_name, const ss_data_t* data);
```

Queue a signal for later emission in the bus's FIFO of `SS_DEFERRED_QUEUE_SIZE` entries. The signal is not emitted until `ss_flush_deferred()` is called on the same bus, and its existence is only checked then.

The queue stores a copy of the payload: string and custom data are copied (the custom cleanup function is not called for the copy, which slots see as borrowed data), so the caller may free or clear its data right away. Pointer data is stored as the pointer value only. In static mode string and custom payloads must fit `SS_DEFERRED_PAYLOAD_SIZE` bytes (strings including the terminator); the copy is aligned like `malloc` memory (for any standard type, and `__int128` with GCC/Clang). Pending entries are released by `ss_bus_destroy()` without being emitted.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if the bus is NULL or the name is NULL or empty, `SS_ERR_MEMORY` if the payload copy cannot be allocated (dynamic mode), `SS_ERR_WOULD_OVERFLOW` if the queue is full or the name or payload is too long, `SS_ERR_INVALID_TYPE` for `SS_TYPE_CUSTOM` data in a library built without `SS_ENABLE_CUSTOM_DATA` (it cannot copy the payload).

### ss_flush_deferred

```c
ss_error_t ss_flush_deferred(ss_bus_t* bus);
```

Emit the deferred signals that were queued on the bus when the flush started, in FIFO order, and release their payload copies. Each entry is removed from the queue before it is emitted; entries queued later (for example by slots) wait for the next flush, also when a slot runs a nested `ss_flush_deferred()`. If a slot destroys the bus, the flush stops.

**Returns:** `SS_OK` if all emissions succeeded, the last error code encountered (e.g. `SS_ERR_NOT_FOUND`), or `SS_ERR_NULL_PARAM` if the bus is NULL.

---

## Batch Operations

A batch belongs to the bus it was created for: that bus resolves names against its default namespace when entries are added, runs the emission, and reports errors to its handler. A batch stores copies of its payloads like the deferred queue (same rules, including `SS_DEFERRED_PAYLOAD_SIZE` in static mode) and holds up to `SS_BATCH_MAX_ENTRIES` entries. A batch is owned by the caller and must not be used from several threads at once.

### ss_batch_create

```c
ss_batch_t* ss_batch_create(ss_bus_t* bus);
```

Create a new batch for `bus`. Allocates from the heap in both memory modes.

**Returns:** Pointer to batch, or NULL if the bus is NULL or being destroyed, or allocation fails (`SS_ERR_MEMORY` to the bus's error handler).

### ss_batch_add

```c
ss_error_t ss_batch_add(ss_batch_t* batch, const char* signal_name,
                        const ss_data_t* data);
```

Add a signal emission to the batch. The name is resolved against the bus's default namespace when added; the payload is copied.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if batch or name is NULL, the batch is being destroyed, or its bus is being destroyed, `SS_ERR_MEMORY` if the payload copy cannot be allocated, `SS_ERR_WOULD_OVERFLOW` if the batch is full or the name or payload is too long, `SS_ERR_INVALID_TYPE` for `SS_TYPE_CUSTOM` data in a library built without `SS_ENABLE_CUSTOM_DATA`.

### ss_batch_emit

```c
ss_error_t ss_batch_emit(ss_batch_t* batch);
```

Emit all signals in the batch on its bus, in the order they were added, and release their payload copies; the batch is then empty and can be reused. Slots may add to the batch being emitted (delivered by the next `ss_batch_emit()`) or destroy it (it is freed when `ss_batch_emit()` returns and the remaining entries are dropped). If a slot destroys the bus, the remaining entries stay in the batch.

**Returns:** `SS_OK` if all emissions succeeded, the last error code, or `SS_ERR_NULL_PARAM` if batch is NULL or its bus is being destroyed.

### ss_batch_destroy

```c
void ss_batch_destroy(ss_batch_t* batch);
```

Free a batch and any pending payload copies. NULL is a no-op. Destroy batches before their bus. `ss_batch_destroy()` itself never touches the bus, so a batch whose bus was destroyed from one of its slots can still be destroyed; no other batch call may follow.

---

## Data Manipulation

### ss_data_create

```c
ss_data_t* ss_data_create(ss_data_type_t type);
```

Allocate an empty data object of the given type (heap, in both memory modes). The caller must free it with `ss_data_destroy`.

**Returns:** Pointer to data, or NULL if allocation fails.

### ss_data_destroy

```c
void ss_data_destroy(ss_data_t* data);
```

`ss_data_clear()`, then free a data object created by `ss_data_create()`. A NULL `data` is ignored.

### ss_data_init

```c
void ss_data_init(ss_data_t* data);
#define SS_DATA_INIT /* ... */
```

Make `*data` an empty `SS_TYPE_VOID` object that owns nothing, whatever the memory held before; the same as `ss_data_t d = SS_DATA_INIT;`. It does not release a payload, so use it on memory that holds no object yet and `ss_data_clear()` on an object in use. A NULL `data` is ignored.

### ss_data_clear

```c
void ss_data_clear(ss_data_t* data);
```

Release the payload the object owns and make it an empty `SS_TYPE_VOID` object. For an owned custom payload, the cleanup function (if any) is called on the copy before it is freed. A borrowed payload is left alone, and a NULL `data` is ignored. Use it for stack objects filled by `ss_data_set_string()` or `ss_data_set_custom()`:

```c
ss_data_t d = SS_DATA_INIT;
ss_data_set_string(&d, buffer);   /* copies; the caller may reuse buffer */
ss_emit(bus, "status", &d);
ss_data_clear(&d);                /* frees the copy */
```

### ss_data_set_int

```c
ss_error_t ss_data_set_int(ss_data_t* data, int value);
```

Set the data to integer type with the given value, first releasing the string or custom payload the object owned, if any (a borrowed payload is not freed). The same applies to every `ss_data_set_*` function.

### ss_data_set_float

```c
ss_error_t ss_data_set_float(ss_data_t* data, float value);
```

Set the data to float type with the given value.

### ss_data_set_double

```c
ss_error_t ss_data_set_double(ss_data_t* data, double value);
```

Set the data to double type with the given value.

### ss_data_set_string

```c
ss_error_t ss_data_set_string(ss_data_t* data, const char* value);
```

Set the data to string type. The string is copied with `SS_MALLOC` (or `SS_STRDUP` if defined) and the object owns the copy; the previous owned payload is released. Pass NULL for value to set a NULL string. Allocates in both memory modes.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if data is NULL, `SS_ERR_MEMORY` if the copy fails (the old value is kept).

### ss_data_set_string_ref

```c
ss_error_t ss_data_set_string_ref(ss_data_t* data, const char* value);
```

Set the data to string type, borrowing `value` (which may be NULL): the string must stay valid while the object is used, and the library never frees it. Never allocates, so it works without a heap. Passing the object's own payload leaves it unchanged, still owned.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if data is NULL.

### ss_data_set_pointer

```c
ss_error_t ss_data_set_pointer(ss_data_t* data, void* value);
```

Set the data to pointer type with the given value. The library does not manage the pointer's lifetime.

### ss_data_set_custom

```c
ss_error_t ss_data_set_custom(ss_data_t* data, const void* value, size_t size,
                              ss_cleanup_func_t cleanup);
```

Set custom data. Requires `SS_ENABLE_CUSTOM_DATA=1`. Copies `size` bytes from `value` into a newly allocated buffer (heap, in both memory modes), which the object owns. The previous owned payload is released; for previous owned custom data, its cleanup function is called on the old copy, which is then freed.

**Parameters:**
- `data` — Data object
- `value` — Source data to copy (must not be NULL)
- `size` — Size in bytes to copy (must be > 0)
- `cleanup` — Optional function (can be NULL) called once with the library's copy and its size just before the copy is freed, to release resources the copy refers to. The copy is passed as `const void*`: the library frees it, the cleanup must not.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if data or value is NULL or size is 0, `SS_ERR_MEMORY` if allocation fails (the old value is kept).

### ss_data_set_custom_ref

```c
ss_error_t ss_data_set_custom_ref(ss_data_t* data, void* value, size_t size);
```

Set custom data, borrowing `size` bytes at `value`. Requires `SS_ENABLE_CUSTOM_DATA=1`. The data must stay valid while the object is used; the library never frees it and calls no cleanup. Never allocates, so it works without a heap. Passing the object's own payload (same pointer and size) leaves it unchanged, still owned. Deferred and batched emissions still copy it (see `ss_emit_deferred`).

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if data or value is NULL or size is 0.

### ss_data_get_int

```c
int ss_data_get_int(const ss_data_t* data, int default_val);
```

Get integer value. Returns `default_val` if data is NULL or not `SS_TYPE_INT`.

### ss_data_get_float

```c
float ss_data_get_float(const ss_data_t* data, float default_val);
```

Get float value. Returns `default_val` if data is NULL or not `SS_TYPE_FLOAT`.

### ss_data_get_double

```c
double ss_data_get_double(const ss_data_t* data, double default_val);
```

Get double value. Returns `default_val` if data is NULL or not `SS_TYPE_DOUBLE`.

### ss_data_get_string

```c
const char* ss_data_get_string(const ss_data_t* data);
```

Get string value. Returns NULL if data is NULL or not `SS_TYPE_STRING`.

### ss_data_get_pointer

```c
void* ss_data_get_pointer(const ss_data_t* data);
```

Get pointer value. Returns NULL if data is NULL or not `SS_TYPE_POINTER`.

### ss_data_get_custom

```c
void* ss_data_get_custom(const ss_data_t* data, size_t* size);
```

Get custom data pointer and optionally the size. Requires `SS_ENABLE_CUSTOM_DATA=1`.

**Parameters:**
- `data` — Data object
- `size` — Output for data size (can be NULL)

**Returns:** Pointer to custom data, or NULL if not `SS_TYPE_CUSTOM`.

---

## Introspection

Requires `SS_ENABLE_INTROSPECTION=1`.

### ss_get_signal_count

```c
size_t ss_get_signal_count(ss_bus_t* bus);
```

Get the number of signals registered on the bus (0 for a NULL bus).

### ss_get_signal_list

```c
ss_error_t ss_get_signal_list(ss_bus_t* bus, ss_signal_info_t** list, size_t* count);
```

Get a snapshot of all signals registered on the bus. The list and its `name` and `description` strings are copies owned by the caller and stay valid after the signals change, and after the bus is destroyed; free everything with `ss_free_signal_list`. Allocates in both memory modes.

**Parameters:**
- `bus` — The bus
- `list` — Output pointer to signal info array
- `count` — Output number of signals

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if an argument is NULL, `SS_ERR_MEMORY` if allocation fails.

### ss_free_signal_list

```c
void ss_free_signal_list(ss_signal_info_t* list, size_t count);
```

Free a signal list allocated by `ss_get_signal_list`.

---

## Memory Statistics

Requires `SS_ENABLE_MEMORY_STATS=1`. Statistics are per bus.

### ss_get_memory_stats

```c
ss_error_t ss_get_memory_stats(ss_bus_t* bus, ss_memory_stats_t* stats);
```

Get the bus's current memory usage statistics.

| Field | Dynamic mode | Static mode |
|-------|--------------|-------------|
| `signals_used` / `slots_used` | Live signals / slots | Live signals / slots |
| `signals_allocated` / `slots_allocated` | Signal / slot nodes held, including ones waiting to be freed after an emission | `SS_MAX_SIGNALS` / `SS_MAX_SLOTS` |
| `total_bytes_allocated` | Heap bytes held by the bus (the bus itself when `ss_bus_create()` allocated it, signals, slots, name and description strings) | `sizeof` the bus |
| `peak_bytes_allocated` | Maximum of `total_bytes_allocated` | `sizeof` the bus |
| `string_bytes` | Bytes of name and description strings | 0 |

Deferred payload copies, batches, data objects and signal lists are not counted.

**Returns:** `SS_OK`, or `SS_ERR_NULL_PARAM` if the bus or stats is NULL.

### ss_reset_memory_stats

```c
void ss_reset_memory_stats(ss_bus_t* bus);
```

Reset `peak_bytes_allocated` to the current `total_bytes_allocated`.

---

## Performance Statistics

Requires `SS_ENABLE_PERFORMANCE_STATS=1`.

### ss_get_perf_stats

```c
ss_error_t ss_get_perf_stats(ss_bus_t* bus, const char* signal_name,
                             ss_perf_stats_t* stats);
```

Get performance statistics for a signal of the bus. Profiling must be enabled on the bus with `ss_enable_profiling(bus, 1)` for data to be collected.

### ss_enable_profiling

```c
ss_error_t ss_enable_profiling(ss_bus_t* bus, int enabled);
```

Enable or disable performance profiling on the bus. When disabled (default), no timing overhead is added to emission. Only declared when `SS_ENABLE_PERFORMANCE_STATS` is 1.

### ss_reset_perf_stats

```c
void ss_reset_perf_stats(ss_bus_t* bus);
```

Reset performance statistics for all signals of the bus.

---

## Error Handling

### ss_error_string

```c
const char* ss_error_string(ss_error_t error);
```

Convert an error code to a human-readable string.

### ss_set_error_handler

```c
void ss_set_error_handler(ss_bus_t* bus, void (*handler)(ss_error_t error, const char* msg));
```

Set the bus's error handler (NULL removes it). It is called for every failing call on this bus, including parameter errors (NULL arguments, handle 0, a batch being destroyed, over-long names), after the bus's lock is released, so it may call library functions. `msg` is the signal name when the call has one, otherwise `ss_error_string(error)`. Errors of calls that have no bus to report to reach no handler: a NULL bus, a NULL batch, and failed bus creation. Errors on one bus never reach another bus's handler. The handler is released with the bus.

---

## Configuration

### ss_set_max_slots_per_signal

```c
void ss_set_max_slots_per_signal(ss_bus_t* bus, size_t max_slots);
```

Set the maximum number of live slots per signal on the bus. Default is `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL` (100).

### ss_get_max_slots_per_signal

```c
size_t ss_get_max_slots_per_signal(ss_bus_t* bus);
```

Get the bus's current maximum slots per signal (0 for a NULL bus).

---

## Debug Trace

Requires `SS_ENABLE_DEBUG_TRACE=1`.

### ss_enable_trace

```c
void ss_enable_trace(ss_bus_t* bus, FILE* output);
```

Start debug trace output (registration, connection, emission) of the bus to the given stream. A bus's trace is off until this is called. Passing NULL stops it.

### ss_disable_trace

```c
void ss_disable_trace(ss_bus_t* bus);
```

Disable the bus's debug trace output.

---

## Configuration Macros

| Macro | Default | Description |
|-------|---------|-------------|
| `SS_USE_STATIC_MEMORY` | 0 | Use static memory allocation |
| `SS_MAX_SIGNALS` | 32 | Maximum signals per bus (static mode) |
| `SS_MAX_SLOTS` | 128 | Maximum slots per bus (static mode) |
| `SS_MAX_SIGNAL_NAME_LENGTH` | 256 (32 in static mode) | Name buffer size, including the terminator |
| `SS_MAX_DESCRIPTION_LENGTH` | 32 | Description buffer size, including the terminator (static mode) |
| `SS_STATIC_BUSES` | 1 | Static mode: built-in buses for `ss_bus_create()`, in `.bss`; 0 = none (buses only from `ss_bus_create_in()` pools) |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Compile in the mutex support that `SS_BUS_THREAD_SAFE` needs |
| `SS_ENABLE_INTROSPECTION` | 1 | Enable signal listing |
| `SS_ENABLE_CUSTOM_DATA` | 1 | Custom data functions and queuing of `SS_TYPE_CUSTOM` |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Enable timing statistics |
| `SS_ENABLE_MEMORY_STATS` | 0 | Enable memory tracking |
| `SS_ENABLE_DEBUG_TRACE` | 0 | Enable debug trace output |
| `SS_ENABLE_ISR_SAFE` | 0 | Enable ISR-safe operations |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR queue depth |
| `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` | undefined | Disable/restore interrupts on targets without atomic compare-and-swap |
| `SS_DEFERRED_QUEUE_SIZE` | 64 (16 with `SS_EMBEDDED_BUILD`) | Deferred emission queue depth, per bus |
| `SS_DEFERRED_PAYLOAD_SIZE` | 32 | String/custom bytes per deferred or batch entry (static mode) |
| `SS_BATCH_MAX_ENTRIES` | `SS_DEFERRED_QUEUE_SIZE` | Entries per batch |
| `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL` | 100 | Default slot limit of a new bus |
| `SS_CACHE_LINE_SIZE` | 64 | Cache line alignment hint |
| `SS_MALLOC(size)` | `malloc(size)` | Custom allocator |
| `SS_FREE(ptr)` | `free(ptr)` | Custom deallocator |
| `SS_CALLOC(count,size)` | `calloc(count,size)` | Custom calloc |
| `SS_STRDUP(str)` | undefined (copies with `SS_MALLOC`) | Optional string duplicator |
| `SS_MINIMAL_BUILD` | undefined | Strip all optional features |
| `SS_EMBEDDED_BUILD` | undefined | Static memory, no threading, 10 slots per signal, deferred queue of 16 |
| `SS_TRACE(...)` | defined | Deprecated, kept for source compatibility; not used by the library |

No macro changes a public type layout or enum value. Configuration must still be identical for the library and every caller, because it decides which functions are declared and the library's limits; pass it on the command line. See the [Configuration Guide](configuration.md).

## Thread Safety

- Thread safety is chosen per bus, at creation: `ss_bus_create(&bus, SS_BUS_THREAD_SAFE)`. It needs `SS_ENABLE_THREAD_SAFETY=1` at compile time; without it the flag returns `SS_ERR_CONFIG_MISMATCH`.
- A thread-safe bus has its own recursive mutex. Every function that touches the bus's state (signals, slots, deferred queue, namespace, error handler, profiling flag, slot limit, trace stream) takes it, so the bus can be used from several threads.
- A bus created without the flag has no mutex: use it from one thread at a time. Buses share no state, so threads that each use their own bus need no lock at all.
- Slots run with their bus's mutex held and may call back into the library; a slot must not wait for another thread that is itself calling the same bus.
- **Cross-bus calls from slots go in one direction only.** A slot of bus A that calls bus B holds A's lock while it takes B's. If another thread's slot of B calls A at the same time, the two threads deadlock. Pick an order (for example "UI bus may call the model bus, never the reverse") and keep to it.
- `ss_bus_destroy()` is not thread-safe with respect to other threads using the same bus. In static mode, creating and destroying built-in buses is not locked: do it from one thread. In dynamic mode, `ss_bus_create()` only calls `SS_CALLOC` and is safe from any thread; `ss_bus_create_in()` is safe from any thread for distinct pools.
- A batch must not be used from several threads at once.
- `ss_emit_from_isr()` needs no lock.

## Best Practices

1. **Create the bus first** — Call `ss_bus_create()` before any other call that takes the bus, and check its result
2. **Check returns** — Always check `ss_error_t` return values
3. **Use handles** — Prefer connection handles for dynamic connections
4. **Set priorities** — Use priorities to ensure critical handlers run first
5. **Destroy the bus** — Call `ss_bus_destroy()` when done, after destroying its batches; never use the pointer afterwards
6. **Validate names** — Keep signal names shorter than `SS_MAX_SIGNAL_NAME_LENGTH`
7. **One global bus?** — A program that wants one keeps it in its own global (`static ss_bus_t* bus;`); see the [migration guide](migration-3.0.md#the-bus-argument) for a small wrapper

## Removed in 3.0

The 2.x functions that initialized and cleaned up the global context, sized its pool and switched locking on and off, and the macro that kept the context in static storage, no longer exist. They are replaced by `ss_bus_create()`, `ss_bus_create_in()` (which returns an error for a NULL or too small pool instead of falling back), `ss_bus_pool_size()`, `ss_bus_destroy()`, the `SS_BUS_THREAD_SAFE` flag and `SS_STATIC_BUSES`. The [migration guide](migration-3.0.md#creating-and-destroying-a-bus) maps each old name to its replacement.
