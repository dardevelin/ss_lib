# API Reference

## Overview

SS_Lib provides a signal-slot mechanism for C programs. This reference covers all public functions, data types, and configuration options.

## Error Codes

```c
typedef enum {
    SS_OK = 0,              /* Success */
    SS_ERR_NULL_PARAM,      /* NULL parameter passed */
    SS_ERR_MEMORY,          /* Memory allocation failed */
    SS_ERR_NOT_FOUND,       /* Signal or slot not found */
    SS_ERR_ALREADY_EXISTS,  /* Signal already registered */
    SS_ERR_INVALID_TYPE,    /* Invalid data type */
    SS_ERR_BUFFER_TOO_SMALL,/* Buffer too small */
    SS_ERR_MAX_SLOTS,       /* Maximum slots reached */
    SS_ERR_TIMEOUT,         /* Operation timed out */
    SS_ERR_WOULD_OVERFLOW,  /* Would overflow static buffer */
    SS_ERR_ISR_UNSAFE       /* Operation not safe in ISR context */
} ss_error_t;
```

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
```

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
typedef void (*ss_cleanup_func_t)(void* data);
typedef uintptr_t ss_connection_t;
```

---

## Core Functions

### ss_init

```c
ss_error_t ss_init(void);
```

Initialize the library with dynamic memory allocation.

**Returns:** `SS_OK` on success. Returns `SS_OK` if already initialized (idempotent). Returns `SS_ERR_MEMORY` if allocation fails.

### ss_init_static

```c
ss_error_t ss_init_static(void* memory_pool, size_t pool_size);
```

Initialize the library with a static memory pool. Requires `SS_USE_STATIC_MEMORY=1`.

**Parameters:**
- `memory_pool` — Pointer to pre-allocated memory buffer
- `pool_size` — Size of the memory pool in bytes

**Returns:** `SS_OK` on success, `SS_ERR_MEMORY` on failure.

### ss_cleanup

```c
void ss_cleanup(void);
```

Clean up all resources and reset the library. Frees all signals, slots, deferred queue entries, and the namespace. Safe to call when not initialized (no-op).

---

## Signal Management

### ss_signal_register

```c
ss_error_t ss_signal_register(const char* signal_name);
```

Register a new signal with default priority (`SS_PRIORITY_NORMAL`). Equivalent to `ss_signal_register_ex(signal_name, NULL, SS_PRIORITY_NORMAL)`.

**Parameters:**
- `signal_name` — Unique name for the signal (must be shorter than `SS_MAX_SIGNAL_NAME_LENGTH`)

**Returns:** `SS_OK` on success, `SS_ERR_ALREADY_EXISTS` if signal exists, `SS_ERR_NULL_PARAM` if name is NULL or empty, `SS_ERR_WOULD_OVERFLOW` if name too long.

### ss_signal_register_ex

```c
ss_error_t ss_signal_register_ex(const char* signal_name,
                                  const char* description,
                                  ss_priority_t priority);
```

Register a signal with metadata.

**Parameters:**
- `signal_name` — Unique name for the signal
- `description` — Human-readable description (can be NULL)
- `priority` — Default priority for the signal

**Returns:** Same as `ss_signal_register`.

### ss_signal_unregister

```c
ss_error_t ss_signal_unregister(const char* signal_name);
```

Unregister a signal and disconnect all connected slots.

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

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if signal doesn't exist, `SS_ERR_MAX_SLOTS` if slot limit reached, `SS_ERR_NULL_PARAM` if signal_name or slot is NULL.

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
- `priority` — Execution priority (higher values execute first)
- `handle` — Output connection handle for later disconnection (can be NULL)

**Returns:** Same as `ss_connect`.

### ss_disconnect

```c
ss_error_t ss_disconnect(const char* signal_name, ss_slot_func_t slot);
```

Disconnect the first matching slot from a signal. If the signal is currently emitting, the slot is marked for deferred removal.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if not connected.

### ss_disconnect_handle

```c
ss_error_t ss_disconnect_handle(ss_connection_t handle);
```

Disconnect a slot using its connection handle. Searches all signals for the matching handle. If the signal is currently emitting, the slot is marked for deferred removal.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if handle is invalid, `SS_ERR_NULL_PARAM` if handle is 0.

### ss_disconnect_all

```c
ss_error_t ss_disconnect_all(const char* signal_name);
```

Disconnect all slots from a signal. If the signal is currently emitting, all slots are marked for deferred removal.

**Returns:** `SS_OK` on success, `SS_ERR_NOT_FOUND` if signal doesn't exist.

---

## Signal Emission

### ss_emit

```c
ss_error_t ss_emit(const char* signal_name, const ss_data_t* data);
```

Emit a signal, invoking all connected slots in priority order. Slots marked as removed are skipped. Supports safe disconnection during emission (deferred removal).

**Parameters:**
- `signal_name` — Name of the signal to emit
- `data` — Data to pass to slots (can be NULL)

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

Emit a signal with a string value. The string pointer is passed directly (not copied) so it must remain valid during emission.

### ss_emit_pointer

```c
ss_error_t ss_emit_pointer(const char* signal_name, void* value);
```

Emit a signal with a pointer value.

### ss_emit_from_isr

```c
ss_error_t ss_emit_from_isr(const char* signal_name, int value);
```

ISR-safe signal emission. Requires `SS_ENABLE_ISR_SAFE=1`. Enqueues the signal name and value into a lock-free ring buffer. No mutex, no malloc.

**Returns:** `SS_OK` on success, `SS_ERR_WOULD_OVERFLOW` if ISR queue is full, `SS_ERR_NULL_PARAM` if signal_name is NULL.

---

## Namespace Support

### ss_set_namespace

```c
ss_error_t ss_set_namespace(const char* namespace);
```

Set the default namespace. Pass NULL to clear.

**Returns:** `SS_OK` on success, `SS_ERR_MEMORY` if string duplication fails.

### ss_get_namespace

```c
const char* ss_get_namespace(void);
```

Get the current default namespace.

**Returns:** Namespace string, or NULL if not set or library not initialized.

### ss_emit_namespaced

```c
ss_error_t ss_emit_namespaced(const char* namespace, const char* signal_name,
                               const ss_data_t* data);
```

Emit a signal with a namespace prefix. Constructs `"namespace::signal_name"` and calls `ss_emit`.

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if either parameter is NULL, `SS_ERR_WOULD_OVERFLOW` if combined name exceeds `SS_MAX_SIGNAL_NAME_LENGTH`.

---

## Deferred Emission

### ss_emit_deferred

```c
ss_error_t ss_emit_deferred(const char* signal_name, const ss_data_t* data);
```

Queue a signal for later emission. String data is duplicated to avoid dangling pointers. The signal is not emitted until `ss_flush_deferred()` is called.

**Returns:** `SS_OK` on success, `SS_ERR_WOULD_OVERFLOW` if deferred queue is full (`SS_DEFERRED_QUEUE_SIZE`).

### ss_flush_deferred

```c
ss_error_t ss_flush_deferred(void);
```

Emit all queued deferred signals in FIFO order. Frees any duplicated string data. Snapshots the count before processing to prevent infinite loops if slots enqueue more deferred emissions.

**Returns:** `SS_OK` if all emissions succeeded, or the last error code encountered.

---

## Batch Operations

### ss_batch_create

```c
ss_batch_t* ss_batch_create(void);
```

Create a new batch for grouping emissions.

**Returns:** Pointer to batch, or NULL if allocation fails.

### ss_batch_add

```c
ss_error_t ss_batch_add(ss_batch_t* batch, const char* signal_name,
                         const ss_data_t* data);
```

Add a signal emission to the batch. String data is duplicated.

**Returns:** `SS_OK` on success, `SS_ERR_WOULD_OVERFLOW` if batch is full.

### ss_batch_emit

```c
ss_error_t ss_batch_emit(ss_batch_t* batch);
```

Emit all signals in the batch. Frees duplicated string data and resets the batch count to 0 (batch can be reused).

**Returns:** `SS_OK` if all emissions succeeded, or the last error code.

### ss_batch_destroy

```c
void ss_batch_destroy(ss_batch_t* batch);
```

Free a batch and any remaining duplicated string data.

---

## Data Manipulation

### ss_data_create

```c
ss_data_t* ss_data_create(ss_data_type_t type);
```

Allocate and initialize a data object. The caller must free with `ss_data_destroy`.

**Returns:** Pointer to data, or NULL if allocation fails.

### ss_data_destroy

```c
void ss_data_destroy(ss_data_t* data);
```

Free a data object. If the type is `SS_TYPE_STRING`, frees the string. If `SS_TYPE_CUSTOM` with a cleanup function, calls the cleanup function before freeing.

### ss_data_set_int

```c
ss_error_t ss_data_set_int(ss_data_t* data, int value);
```

Set the data to integer type with the given value.

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

Set the data to string type. The string is duplicated via `SS_STRDUP`. If the data previously held a string, the old string is freed first. Pass NULL for value to set a NULL string.

**Returns:** `SS_OK` on success, `SS_ERR_MEMORY` if duplication fails.

### ss_data_set_pointer

```c
ss_error_t ss_data_set_pointer(ss_data_t* data, void* value);
```

Set the data to pointer type with the given value. The library does not manage the pointer's lifetime.

### ss_data_set_custom

```c
ss_error_t ss_data_set_custom(ss_data_t* data, void* value, size_t size,
                               ss_cleanup_func_t cleanup);
```

Set custom data. Requires `SS_ENABLE_CUSTOM_DATA=1`. Copies `size` bytes from `value` into a newly allocated buffer. If the data previously held custom data, the old cleanup function is called and the old buffer freed.

**Parameters:**
- `data` — Data object
- `value` — Source data to copy (must not be NULL)
- `size` — Size in bytes to copy (must be > 0)
- `cleanup` — Optional cleanup function called on destroy (can be NULL)

**Returns:** `SS_OK` on success, `SS_ERR_NULL_PARAM` if data, value, or size is 0, `SS_ERR_MEMORY` if allocation fails.

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

Get a list of all registered signals. The caller must free the list with `ss_free_signal_list`.

**Parameters:**
- `list` — Output pointer to signal info array
- `count` — Output number of signals

**Returns:** `SS_OK` on success, `SS_ERR_MEMORY` if allocation fails.

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

Get current memory usage statistics. In dynamic mode, calculates string bytes on each call. In static mode, reports pool capacity.

### ss_reset_memory_stats

```c
void ss_reset_memory_stats(void);
```

Reset all memory statistics counters to zero.

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

Enable or disable performance profiling. When disabled (default), no timing overhead is added to emission. When `SS_ENABLE_PERFORMANCE_STATS` is 0, this function is a no-op stub.

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

Set a custom error handler. The handler is called internally when errors occur, providing the error code and a descriptive message.

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

Enable or disable thread safety at runtime. Requires `SS_ENABLE_THREAD_SAFETY=1` at compile time. When enabling, initializes the mutex. When disabling, destroys the mutex. Thread safety is disabled by default even when compiled with `SS_ENABLE_THREAD_SAFETY=1`.

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

Enable debug trace output to the specified file stream.

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
| `SS_MAX_SIGNAL_NAME_LENGTH` | 256 (32 in static mode) | Maximum signal name length |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Enable mutex infrastructure |
| `SS_ENABLE_INTROSPECTION` | 1 | Enable signal listing |
| `SS_ENABLE_CUSTOM_DATA` | 1 | Enable custom data types |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Enable timing statistics |
| `SS_ENABLE_MEMORY_STATS` | 0 | Enable memory tracking |
| `SS_ENABLE_DEBUG_TRACE` | 0 | Enable debug trace output |
| `SS_ENABLE_ISR_SAFE` | 0 | Enable ISR-safe operations |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR queue depth |
| `SS_DEFERRED_QUEUE_SIZE` | 64 | Deferred emission queue depth |
| `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL` | 100 | Default slot limit |
| `SS_CACHE_LINE_SIZE` | 64 | Cache line alignment hint |
| `SS_MALLOC(size)` | `malloc(size)` | Custom allocator |
| `SS_FREE(ptr)` | `free(ptr)` | Custom deallocator |
| `SS_CALLOC(count,size)` | `calloc(count,size)` | Custom calloc |
| `SS_STRDUP(str)` | `strdup(str)` | Custom string duplicator |
| `SS_MINIMAL_BUILD` | undefined | Strip all optional features |
| `SS_EMBEDDED_BUILD` | undefined | Static memory, no threading |

## Thread Safety

When `SS_ENABLE_THREAD_SAFETY` is enabled and `ss_set_thread_safe(1)` is called:
- All API functions are mutex-protected
- Signals can be emitted from multiple threads
- Slots can be disconnected during emission (deferred removal)
- Thread safety is disabled by default and must be enabled at runtime

## Best Practices

1. **Initialize early** — Call `ss_init()` before any other library calls
2. **Check returns** — Always check `ss_error_t` return values
3. **Use handles** — Prefer connection handles for dynamic connections
4. **Set priorities** — Use priorities to ensure critical handlers run first
5. **Clean up** — Always call `ss_cleanup()` before program exit
6. **Validate names** — Keep signal names shorter than `SS_MAX_SIGNAL_NAME_LENGTH`
