# API Reference

## Overview

SS_Lib provides a signal-slot mechanism for C programs. This reference covers all public functions, data types, and configuration options of version 3.0.0.

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
    SS_ERR_INVALID_TYPE,    /* Reserved, not currently returned */
    SS_ERR_BUFFER_TOO_SMALL,/* Reserved, not currently returned */
    SS_ERR_MAX_SLOTS,       /* Maximum slots per signal reached */
    SS_ERR_TIMEOUT,         /* Reserved, not currently returned */
    SS_ERR_WOULD_OVERFLOW,  /* Name, payload or queue exceeds a fixed limit */
    SS_ERR_ISR_UNSAFE,      /* Reserved, not currently returned */
    SS_ERR_CONFIG_MISMATCH  /* Caller and library built with different flags */
} ss_error_t;
```

Functions that need an initialized library return `SS_ERR_NULL_PARAM` (or 0 / NULL) when it is not initialized.

## Data Types

### ss_data_type_t

```c
typedef enum {
    SS_TYPE_VOID,       /* No data */
    SS_TYPE_INT,        /* Integer */
    SS_TYPE_FLOAT,      /* Float */
    SS_TYPE_DOUBLE,     /* Double */
    SS_TYPE_STRING,     /* Null-terminated string */
    SS_TYPE_POINTER,    /* Generic pointer */
    SS_TYPE_CUSTOM      /* Custom data with size (requires SS_ENABLE_CUSTOM_DATA) */
} ss_data_type_t;
```

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
    } value;
    /* If SS_ENABLE_CUSTOM_DATA: */
    size_t size;
    void* custom_data;
    ss_cleanup_func_t custom_cleanup;
} ss_data_t;

#define SS_DATA_INIT { SS_TYPE_VOID, 0, { 0 }, ... }  /* empty object */
```

**Ownership:** the `owned` field records whether the object owns its string or custom payload. Callers read it but never write it.
- Only the copying setters, `ss_data_set_string()` and `ss_data_set_custom()`, set `owned`. The copy belongs to the object and is freed exactly once: by the next `ss_data_set_*` call, by `ss_data_clear()`, or by `ss_data_destroy()`.
- Everything else borrows and leaves `owned` at 0: direct field writes, `ss_data_set_string_ref()` / `ss_data_set_custom_ref()`, the `ss_emit_*` helpers, and the data slots receive (also when the emitter's object owns its payload, and for deferred and batch copies). The library never frees a borrowed payload, so misusing a borrowed object leaks at worst.
- Start every object from `SS_DATA_INIT` or `ss_data_init()`; `ss_data_create()` returns an initialized one. Release a stack object that used a copying setter with `ss_data_clear()` before it goes out of scope. Do not write the payload fields of an object by hand while it owns its payload: clear it first.
- `custom_cleanup` is called once with the library's copy of the custom data (read-only) and its size, just before the library frees it, so it can release resources the copy refers to. It must not free the pointer itself.

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

## Core Functions

### ss_init

```c
ss_error_t ss_init(void);
```

Initialize the library. In dynamic mode the library context is heap-allocated; with `SS_USE_STATIC_MEMORY` it lives in static storage and no heap is used. Not thread-safe: call it before other threads use the library.

In static mode with `SS_STATIC_CONTEXT=0` there is no built-in storage and the context is allocated with `SS_CALLOC`. For new code prefer `ss_init_abi(SS_ABI_TAG)`, which also checks that the caller was compiled with the same `ss_data_t` layout as the library (see [ABI check](#abi-check)).

**Returns:** `SS_OK` on success, also if already initialized. `SS_ERR_MEMORY` if allocation or mutex creation fails, `SS_ERR_ALREADY_EXISTS` if called while an `ss_cleanup()` requested from a slot is still pending.

### ss_init_static

```c
ss_error_t ss_init_static(void* memory_pool, size_t pool_size);
```

Initialize the library with its context placed in a caller-owned buffer. The buffer needs no particular alignment; if it holds at least `ss_get_required_pool_size()` bytes, the context lives in it, and it must outlive `ss_cleanup()`, which never frees it. Works in both memory modes; signals and slots still follow the mode (heap in dynamic mode, fixed arrays in static mode). For compatibility with 2.1, a NULL or smaller buffer is ignored and the call behaves exactly like `ss_init()`, so check the size yourself. `ss_init_static_abi(pool, size, SS_ABI_TAG)` adds the [ABI check](#abi-check).

**Parameters:**
- `memory_pool` — Caller-owned buffer
- `pool_size` — Size of the buffer in bytes

**Returns:** As `ss_init()` (`SS_OK` also if already initialized; the buffer is then unused). Never returns `SS_ERR_NULL_PARAM` or `SS_ERR_BUFFER_TOO_SMALL`.

```c
static unsigned char pool[4096];
if (ss_get_required_pool_size() > sizeof(pool)) {
    /* too small: ss_init_static() would fall back to ss_init() */
}
ss_init_static(pool, sizeof(pool));
```

### ss_get_required_pool_size

```c
size_t ss_get_required_pool_size(void);
```

Bytes needed by `ss_init_static()` for the current configuration, including alignment slack.

### ABI check

```c
#define SS_ABI_TAG ((size_t)sizeof(ss_data_t))
ss_error_t ss_init_abi(size_t abi_tag);
ss_error_t ss_init_static_abi(void* memory_pool, size_t pool_size, size_t abi_tag);
```

Opt-in versions of `ss_init()` and `ss_init_static()` that first compare the caller's `SS_ABI_TAG` with the library's. Always pass `SS_ABI_TAG`. `SS_ABI_TAG` describes the configuration-dependent ABI as the caller sees it (currently the size of `ss_data_t`, which `SS_ENABLE_CUSTOM_DATA` changes). A program built with different flags than the library gets `SS_ERR_CONFIG_MISMATCH` instead of memory corruption later; otherwise they behave and return exactly like `ss_init()` / `ss_init_static()`. Recommended for new code. Other flag differences are not detected, so build the library and every caller with the same configuration.

### ss_cleanup

```c
void ss_cleanup(void);
```

Clean up all resources and reset the library: releases signals, slots, pending deferred entries and queued ISR events, and resets runtime settings (thread safety, namespace, error handler, trace, slot limit). Never frees an `ss_init_static()` buffer. Safe to call when not initialized (no-op). Not thread-safe with respect to other threads using the library.

Called from inside a slot, the cleanup is deferred until the outermost library call returns. Meanwhile the remaining slots of emissions in progress are skipped and the library behaves as uninitialized (calls return `SS_ERR_NULL_PARAM`, 0 or NULL; `ss_init()` returns `SS_ERR_ALREADY_EXISTS`).

### ss_version

```c
const char* ss_version(void);
```

Runtime version string of the compiled library, equal to `SS_VERSION_STRING`.

---

## Signal Management

### ss_signal_register

```c
ss_error_t ss_signal_register(const char* signal_name);
```

Register a new signal with default priority (`SS_PRIORITY_NORMAL`). Equivalent to `ss_signal_register_ex(signal_name, NULL, SS_PRIORITY_NORMAL)`.

**Parameters:**
- `signal_name` — Unique name for the signal (must be shorter than `SS_MAX_SIGNAL_NAME_LENGTH`)

**Returns:** `SS_OK` on success, `SS_ERR_ALREADY_EXISTS` if signal exists, `SS_ERR_NULL_PARAM` if name is NULL or empty, `SS_ERR_WOULD_OVERFLOW` if the name is too long or (static mode) all `SS_MAX_SIGNALS` entries are in use, `SS_ERR_MEMORY` if allocation fails (dynamic mode).

### ss_signal_register_ex

```c
ss_error_t ss_signal_register_ex(const char* signal_name,
                                  const char* description,
                                  ss_priority_t priority);
```

Register a signal with metadata.

**Parameters:**
- `signal_name` — Unique name for the signal
- `description` — Human-readable description (can be NULL). Stored only when `SS_ENABLE_INTROSPECTION` is on (it is only read by `ss_get_signal_list()`). In static mode it must fit `SS_MAX_DESCRIPTION_LENGTH` bytes including the terminator.
- `priority` — Default priority for the signal

**Returns:** Same as `ss_signal_register`; also `SS_ERR_WOULD_OVERFLOW` if the description is too long (static mode).

### ss_signal_unregister

```c
ss_error_t ss_signal_unregister(const char* signal_name);
```

Unregister a signal and disconnect all connected slots. Safe to call from a slot of the same signal: the remaining slots of that emission are skipped, the memory is released when the emission ends, and the name can be registered again at once.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if signal doesn't exist.

### ss_signal_exists

```c
int ss_signal_exists(const char* signal_name);
```

Check if a signal is registered.

**Returns:** 1 if the signal exists, 0 otherwise. Returns 0 if the library is not initialized or name is NULL.

---

## Connection Management

### ss_connect

```c
ss_error_t ss_connect(const char* signal_name, ss_slot_func_t slot, void* user_data);
```

Connect a slot to a signal with default priority (`SS_PRIORITY_NORMAL`). Equivalent to `ss_connect_ex(signal_name, slot, user_data, SS_PRIORITY_NORMAL, NULL)`.

**Parameters:**
- `signal_name` — Name of the signal
- `slot` — Callback function
- `user_data` — User data passed to the callback (can be NULL)

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if signal doesn't exist, `SS_ERR_MAX_SLOTS` if the per-signal limit is reached (only live slots count), `SS_ERR_NULL_PARAM` if signal_name or slot is NULL, `SS_ERR_WOULD_OVERFLOW` if all `SS_MAX_SLOTS` entries are in use (static mode), `SS_ERR_MEMORY` if allocation fails (dynamic mode).

### ss_connect_ex

```c
ss_error_t ss_connect_ex(const char* signal_name, ss_slot_func_t slot,
                          void* user_data, ss_priority_t priority,
                          ss_connection_t* handle);
```

Connect a slot with priority and optional connection handle.

**Parameters:**
- `signal_name` — Name of the signal
- `slot` — Callback function
- `user_data` — User data passed to the callback
- `priority` — Execution priority (higher values execute first; equal priorities run in connection order)
- `handle` — Output connection handle for later disconnection (can be NULL)

**Returns:** Same as `ss_connect`.

### ss_disconnect

```c
ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot);
```

Disconnect the first matching slot from a signal. If the signal is currently emitting, the slot is marked, is not called again, and is freed when the emission ends.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if not connected.

### ss_disconnect_handle

```c
ss_error_t ss_disconnect_handle(ss_connection_t handle);
```

Disconnect a slot using its connection handle. Searches all signals for the matching handle. If the signal is currently emitting, the slot is marked, is not called again, and is freed when the emission ends.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if handle is invalid, `SS_ERR_NULL_PARAM` if handle is 0.

### ss_disconnect_all

```c
ss_error_t ss_disconnect_all(const char* signal_name);
```

Disconnect all slots from a signal. If the signal is currently emitting, the remaining slots of that emission are skipped and freed when it ends.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if signal doesn't exist.

---

## Signal Emission

### ss_emit

```c
ss_error_t ss_emit(const char* signal_name, const ss_data_t* data);
```

Emit a signal, invoking all connected slots in priority order. Slots may call any library function, including on the signal being emitted:
- Slots disconnected during the emission are not called; they are freed when it ends.
- A slot connected during the emission may or may not be called by it, depending on where its priority places it in the list; do not rely on either.
- `ss_signal_unregister()` or `ss_cleanup()` from a slot skips the remaining slots (see those functions).

**Parameters:**
- `signal_name` — Name of the signal to emit
- `data` — Data to pass to slots (can be NULL). Slots see it as borrowed: if the object owns its payload, they receive a copy with `owned` cleared, pointing to the same payload.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if signal doesn't exist.

### ss_emit_void

```c
ss_error_t ss_emit_void(const char* signal_name);
```

Emit a signal with `SS_TYPE_VOID` data.

### ss_emit_int

```c
ss_error_t ss_emit_int(const char* signal_name, int value);
```

Emit a signal with an integer value.

### ss_emit_float

```c
ss_error_t ss_emit_float(const char* signal_name, float value);
```

Emit a signal with a float value.

### ss_emit_double

```c
ss_error_t ss_emit_double(const char* signal_name, double value);
```

Emit a signal with a double value.

### ss_emit_string

```c
ss_error_t ss_emit_string(const char* signal_name, const char* value);
```

Emit a signal with a string value. The string pointer is passed directly (not copied), so it must remain valid during emission. Slots receive a borrowed pointer and must not free it.

### ss_emit_pointer

```c
ss_error_t ss_emit_pointer(const char* signal_name, void* value);
```

Emit a signal with a pointer value.

### ss_emit_from_isr

```c
ss_error_t ss_emit_from_isr(const char* signal_name, int value);
```

Queue an integer event from an interrupt handler. Requires `SS_ENABLE_ISR_SAFE=1`. No locks, no allocation, no slot calls: the name and value are stored in a fixed queue of `SS_ISR_QUEUE_SIZE` entries, claimed with atomic compare-and-swap, and delivered later by `ss_process_isr_queue()`. Safe with nested interrupts and from several threads. Usable before `ss_init()`.

Atomics come from C11 `<stdatomic.h>`, GCC/Clang `__atomic` builtins or MSVC Interlocked functions. On targets without compare-and-swap (Cortex-M0, AVR) define `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` (see [Configuration](configuration.md#isr-safe-emission)).

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if signal_name is NULL or empty, `SS_ERR_WOULD_OVERFLOW` if the queue is full or the name is longer than `SS_MAX_SIGNAL_NAME_LENGTH - 1`.

### ss_process_isr_queue

```c
ss_error_t ss_process_isr_queue(void);
```

Deliver queued ISR events, oldest first, from normal context. Requires `SS_ENABLE_ISR_SAFE=1`. Call it regularly from the main loop; slots run here. Names are resolved against the default namespace at delivery time. Events queued while it runs wait for the next call. `ss_cleanup()` discards queued events.

**Returns:** `SS_OK`, `SS_ERR_NULL_PARAM` if not initialized, or the last emission error (e.g. `SS_ERR_NOT_FOUND` for an unregistered signal).

---

## Namespace Support

### ss_set_namespace

```c
ss_error_t ss_set_namespace(const char* ns);
```

Set the default namespace. Pass NULL or `""` to clear. While it is set, every signal name without `"::"` is resolved as `"ns::name"` by all functions that take a signal name (register, unregister, exists, connect, disconnect, emit, deferred and batch emission, perf stats; ISR events at delivery). Names containing `"::"` are used as given. Deferred and batch entries are resolved when they are queued. No allocation.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if not initialized, `SS_ERR_WOULD_OVERFLOW` if even `"ns::x"` would not fit in `SS_MAX_SIGNAL_NAME_LENGTH`.

### ss_get_namespace

```c
const char* ss_get_namespace(void);
```

Get the current default namespace.

**Returns:** Namespace string (valid until the next change), or NULL if not set or library not initialized. Passing it back to `ss_set_namespace()` is safe (no-op). The string is the library's own buffer and is read without the lock, so with several threads read it only while no other thread can call `ss_set_namespace()`, or copy it then.

### ss_emit_namespaced

```c
ss_error_t ss_emit_namespaced(const char* ns, const char* signal_name,
                               const ss_data_t* data);
```

Emit `"ns::signal_name"`. Ignores the default namespace.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if either parameter is NULL or the library is not initialized, `SS_ERR_WOULD_OVERFLOW` if the combined name does not fit in `SS_MAX_SIGNAL_NAME_LENGTH`, `SS_ERR_NOT_FOUND` if the signal doesn't exist.

---

## Deferred Emission

### ss_emit_deferred

```c
ss_error_t ss_emit_deferred(const char* signal_name, const ss_data_t* data);
```

Queue a signal for later emission in a FIFO of `SS_DEFERRED_QUEUE_SIZE` entries. The signal is not emitted until `ss_flush_deferred()` is called, and its existence is only checked then.

The queue stores a copy of the payload: string and custom data are copied (the custom cleanup function is not called for the copy, which slots see as borrowed data), so the caller may free or clear its data right away. Pointer data is stored as the pointer value only. In static mode string and custom payloads must fit `SS_DEFERRED_PAYLOAD_SIZE` bytes (strings including the terminator); the copy is aligned like `malloc` memory (for any standard type, and `__int128` with GCC/Clang). Pending entries are released by `ss_cleanup()`.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if the name is NULL or empty or the library is not initialized, `SS_ERR_MEMORY` if the payload copy cannot be allocated (dynamic mode), `SS_ERR_WOULD_OVERFLOW` if the queue is full or the name or payload is too long.

### ss_flush_deferred

```c
ss_error_t ss_flush_deferred(void);
```

Emit the deferred signals that were queued when the flush started, in FIFO order, and release their payload copies. Each entry is removed from the queue before it is emitted; entries queued later (for example by slots) wait for the next flush, also when a slot runs a nested `ss_flush_deferred()`.

**Returns:** `SS_OK` if all emissions succeeded, or the last error code encountered (e.g. `SS_ERR_NOT_FOUND`).

---

## Batch Operations

A batch stores copies of its payloads like the deferred queue (same rules, including `SS_DEFERRED_PAYLOAD_SIZE` in static mode) and holds up to `SS_BATCH_MAX_ENTRIES` entries. A batch is owned by the caller and must not be used from several threads at once.

### ss_batch_create

```c
ss_batch_t* ss_batch_create(void);
```

Create a new batch for grouping emissions. Allocates from the heap in both memory modes.

**Returns:** Pointer to batch, or NULL if allocation fails.

### ss_batch_add

```c
ss_error_t ss_batch_add(ss_batch_t* batch, const char* signal_name,
                         const ss_data_t* data);
```

Add a signal emission to the batch. The name is resolved against the default namespace when added; the payload is copied.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if batch or name is NULL or the batch is being destroyed, `SS_ERR_MEMORY` if the payload copy cannot be allocated, `SS_ERR_WOULD_OVERFLOW` if the batch is full or the name or payload is too long.

### ss_batch_emit

```c
ss_error_t ss_batch_emit(ss_batch_t* batch);
```

Emit all signals in the batch in the order they were added and release their payload copies; the batch is then empty and can be reused. Slots may add to the batch being emitted (delivered by the next `ss_batch_emit()`) or destroy it (it is freed when `ss_batch_emit()` returns and the remaining entries are dropped).

**Returns:** `SS_OK` if all emissions succeeded, the last error code, or `SS_ERR_NULL_PARAM` if batch is NULL or the library is not initialized.

### ss_batch_destroy

```c
void ss_batch_destroy(ss_batch_t* batch);
```

Free a batch and any pending payload copies.

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
ss_emit("status", &d);
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
size_t ss_get_signal_count(void);
```

Get the number of registered signals.

### ss_get_signal_list

```c
ss_error_t ss_get_signal_list(ss_signal_info_t** list, size_t* count);
```

Get a snapshot of all registered signals. The list and its `name` and `description` strings are copies owned by the caller and stay valid after the signals change; free everything with `ss_free_signal_list`. Allocates in both memory modes.

**Parameters:**
- `list` — Output pointer to signal info array
- `count` — Output number of signals

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if an argument is NULL or the library is not initialized, `SS_ERR_MEMORY` if allocation fails.

### ss_free_signal_list

```c
void ss_free_signal_list(ss_signal_info_t* list, size_t count);
```

Free a signal list allocated by `ss_get_signal_list`.

---

## Memory Statistics

Requires `SS_ENABLE_MEMORY_STATS=1`.

### ss_get_memory_stats

```c
ss_error_t ss_get_memory_stats(ss_memory_stats_t* stats);
```

Get current memory usage statistics.

| Field | Dynamic mode | Static mode |
|-------|--------------|-------------|
| `signals_used` / `slots_used` | Live signals / slots | Live signals / slots |
| `signals_allocated` / `slots_allocated` | Signal / slot nodes held, including ones waiting to be freed after an emission | `SS_MAX_SIGNALS` / `SS_MAX_SLOTS` |
| `total_bytes_allocated` | Heap bytes held by the library (context, signals, slots, name and description strings) | `sizeof` the context |
| `peak_bytes_allocated` | Maximum of `total_bytes_allocated` | `sizeof` the context |
| `string_bytes` | Bytes of name and description strings | 0 |

Deferred payload copies, batches, data objects and signal lists are not counted.

**Returns:** `SS_OK`, or `SS_ERR_NULL_PARAM` if stats is NULL or the library is not initialized.

### ss_reset_memory_stats

```c
void ss_reset_memory_stats(void);
```

Reset `peak_bytes_allocated` to the current `total_bytes_allocated`.

---

## Performance Statistics

Requires `SS_ENABLE_PERFORMANCE_STATS=1`.

### ss_get_perf_stats

```c
ss_error_t ss_get_perf_stats(const char* signal_name, ss_perf_stats_t* stats);
```

Get performance statistics for a specific signal. Profiling must be enabled with `ss_enable_profiling(1)` for data to be collected.

### ss_enable_profiling

```c
ss_error_t ss_enable_profiling(int enabled);
```

Enable or disable performance profiling. When disabled (default), no timing overhead is added to emission. Only declared when `SS_ENABLE_PERFORMANCE_STATS` is 1.

### ss_reset_perf_stats

```c
void ss_reset_perf_stats(void);
```

Reset performance statistics for all signals.

---

## Error Handling

### ss_error_string

```c
const char* ss_error_string(ss_error_t error);
```

Convert an error code to a human-readable string.

### ss_set_error_handler

```c
void ss_set_error_handler(void (*handler)(ss_error_t error, const char* msg));
```

Set a custom error handler. It is called for every failing call made while the library is initialized, including parameter errors (NULL arguments, handle 0, NULL or destroyed batch, over-long names), after the library's lock is released, so it may call library functions. `msg` is the signal name when the call has one, otherwise `ss_error_string(error)`. Requires an initialized library; reset by `ss_cleanup()`.

---

## Configuration

### ss_set_max_slots_per_signal

```c
void ss_set_max_slots_per_signal(size_t max_slots);
```

Set the maximum number of slots per signal. Default is `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL` (100).

### ss_get_max_slots_per_signal

```c
size_t ss_get_max_slots_per_signal(void);
```

Get the current maximum slots per signal.

### ss_set_thread_safe

```c
void ss_set_thread_safe(int enabled);
```

Enable or disable locking at runtime. Requires `SS_ENABLE_THREAD_SAFETY=1` at compile time and an initialized library. The recursive mutex is created by `ss_init()`; this only switches its use. Thread safety is disabled by default even when compiled with `SS_ENABLE_THREAD_SAFETY=1`. Change the setting only while no other thread uses the library.

### ss_is_thread_safe

```c
int ss_is_thread_safe(void);
```

Check whether thread safety is currently enabled.

---

## Debug Trace

Requires `SS_ENABLE_DEBUG_TRACE=1`.

### ss_enable_trace

```c
void ss_enable_trace(FILE* output);
```

Start debug trace output (registration, connection, emission) to the given stream. Trace is off until this is called on an initialized library; `ss_cleanup()` turns it off. Passing NULL stops it.

### ss_disable_trace

```c
void ss_disable_trace(void);
```

Disable debug trace output.

---

## Configuration Macros

| Macro | Default | Description |
|-------|---------|-------------|
| `SS_USE_STATIC_MEMORY` | 0 | Use static memory allocation |
| `SS_MAX_SIGNALS` | 32 | Maximum signals (static mode) |
| `SS_MAX_SLOTS` | 128 | Maximum slots (static mode) |
| `SS_MAX_SIGNAL_NAME_LENGTH` | 256 (32 in static mode) | Name buffer size, including the terminator |
| `SS_MAX_DESCRIPTION_LENGTH` | 32 | Description buffer size, including the terminator (static mode) |
| `SS_STATIC_CONTEXT` | 1 | Static mode: 1 = `ss_init()` uses a context in `.bss`; 0 = no built-in context, `ss_init()` uses `SS_CALLOC` |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Enable mutex infrastructure |
| `SS_ENABLE_INTROSPECTION` | 1 | Enable signal listing |
| `SS_ENABLE_CUSTOM_DATA` | 1 | Enable custom data types |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Enable timing statistics |
| `SS_ENABLE_MEMORY_STATS` | 0 | Enable memory tracking |
| `SS_ENABLE_DEBUG_TRACE` | 0 | Enable debug trace output |
| `SS_ENABLE_ISR_SAFE` | 0 | Enable ISR-safe operations |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR queue depth |
| `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` | undefined | Disable/restore interrupts on targets without atomic compare-and-swap |
| `SS_DEFERRED_QUEUE_SIZE` | 64 (16 with `SS_EMBEDDED_BUILD`) | Deferred emission queue depth |
| `SS_DEFERRED_PAYLOAD_SIZE` | 32 | String/custom bytes per deferred or batch entry (static mode) |
| `SS_BATCH_MAX_ENTRIES` | `SS_DEFERRED_QUEUE_SIZE` | Entries per batch |
| `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL` | 100 | Default slot limit |
| `SS_CACHE_LINE_SIZE` | 64 | Cache line alignment hint |
| `SS_MALLOC(size)` | `malloc(size)` | Custom allocator |
| `SS_FREE(ptr)` | `free(ptr)` | Custom deallocator |
| `SS_CALLOC(count,size)` | `calloc(count,size)` | Custom calloc |
| `SS_STRDUP(str)` | undefined (copies with `SS_MALLOC`) | Optional string duplicator |
| `SS_MINIMAL_BUILD` | undefined | Strip all optional features |
| `SS_EMBEDDED_BUILD` | undefined | Static memory, no threading, 10 slots per signal, deferred queue of 16 |
| `SS_TRACE(...)` | defined | Deprecated, kept for source compatibility; not used by the library |

Configuration must be identical for the library and every caller; pass it on the command line. See the [Configuration Guide](configuration.md).

## Thread Safety

When `SS_ENABLE_THREAD_SAFETY` is enabled and `ss_set_thread_safe(1)` is called:
- Every function that touches shared state (signals, slots, deferred queue, namespace, error handler, profiling flag, slot limit, trace stream) takes one recursive mutex
- Signals can be emitted from multiple threads
- Slots run with the mutex held and may call back into the library; a slot must not wait for another thread that is itself calling the library
- Thread safety is disabled by default and must be enabled at runtime, after `ss_init()`, while no other thread uses the library
- `ss_init()` and `ss_cleanup()` are not thread-safe
- A batch must not be used from several threads at once
- `ss_emit_from_isr()` needs no lock

## Best Practices

1. **Initialize early** — Call `ss_init()` before any other library calls
2. **Check returns** — Always check `ss_error_t` return values
3. **Use handles** — Prefer connection handles for dynamic connections
4. **Set priorities** — Use priorities to ensure critical handlers run first
5. **Clean up** — Always call `ss_cleanup()` before program exit
6. **Validate names** — Keep signal names shorter than `SS_MAX_SIGNAL_NAME_LENGTH`
