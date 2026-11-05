# Skill: Writing Correct SS_Lib Code

## Trigger

Activate when code includes `ss_lib.h`, or user asks about SS_Lib, signal-slot in C, or writing event-driven C code with this library.

## Overview

SS_Lib is a lightweight signal-slot library for C (v3.0.0). Pure ANSI C, zero external dependencies. Designed for embedded systems, game engines, and resource-constrained environments. Supports dynamic and static memory allocation, optional thread safety, and ISR-safe signal emission.

All state lives in a **bus** (`ss_bus_t*`, opaque). Every function that uses library state takes the bus as its first argument. Buses share nothing: the same signal name on two buses names two different signals. The library has no default or global bus.

## Mandatory Lifecycle

Every SS_Lib program must follow this sequence:

```c
ss_bus_t* bus = NULL;
if (ss_bus_create(&bus, 0) != SS_OK) return 1;   // 1. Create a bus (checks SS_ABI_TAG)
ss_signal_register(bus, "sig");                  // 2. Register signals
ss_connect(bus, "sig", fn, ud);                  // 3. Connect slots
ss_emit_int(bus, "sig", 42);                     // 4. Emit signals
ss_disconnect(bus, "sig", fn);                   // 5. Disconnect slots
ss_bus_destroy(bus);                             // 6. Destroy the bus and everything it holds
```

Calls with a NULL bus fail (`SS_ERR_NULL_PARAM`, 0 or NULL); skipping `ss_bus_destroy()` leaks in dynamic mode. Using a bus after `ss_bus_destroy()` returned is undefined, like memory after `free()`. Every compile-time option (`SS_ENABLE_*`, `SS_USE_STATIC_MEMORY`, presets, limits) must be identical for the library and every file that includes `ss_lib.h`: pass them on the command line (`-D...`), never `#define` them in one source file. Options never change a public type layout, so a mismatched feature flag is a compile or link error, but limits would silently disagree. The `ss_bus_create` macros pass the caller's `SS_ABI_TAG`, so every creation returns `SS_ERR_CONFIG_MISMATCH` when the caller's `ss_data_t` layout differs from the library's (e.g. a header from another version).

A program that wants one bus for everything keeps it in its own global:

```c
static ss_bus_t* bus;            /* the application's bus */

int app_init(void) { return ss_bus_create(&bus, 0) == SS_OK ? 0 : -1; }
void app_quit(void) { ss_bus_destroy(bus); bus = NULL; }
```

## Core API Reference

### Buses (7 functions/macros)

```c
typedef struct ss_bus ss_bus_t;                    /* opaque */
#define SS_BUS_THREAD_SAFE 0x1u                    /* needs SS_ENABLE_THREAD_SAFETY */

#define ss_bus_create(bus, flags)                  /* ss_bus_create_abi(bus, flags, SS_ABI_TAG) */
#define ss_bus_create_in(bus, pool, size, flags)   /* ss_bus_create_in_abi(..., SS_ABI_TAG) */
ss_error_t  ss_bus_create_abi(ss_bus_t** bus, unsigned int flags, size_t abi_tag);
ss_error_t  ss_bus_create_in_abi(ss_bus_t** bus, void* pool, size_t pool_size,
                                 unsigned int flags, size_t abi_tag);
size_t      ss_bus_pool_size(void);
void        ss_bus_destroy(ss_bus_t* bus);
const char* ss_version(void);                      // == SS_VERSION_STRING
```

- `ss_bus_create(&bus, flags)` — Dynamic mode: allocates the bus with `SS_CALLOC` (safe from any thread). Static mode (`SS_USE_STATIC_MEMORY`): takes one of `SS_STATIC_BUSES` built-in buses (default 1, in `.bss`), never the heap; returns `SS_ERR_WOULD_OVERFLOW` when all are in use. Claiming and releasing built-in buses is not locked: create and destroy them from one thread (normally at startup).
- `ss_bus_create_in(&bus, pool, size, flags)` — Places the bus in caller memory, in either mode. Any alignment; `ss_bus_pool_size()` bytes always suffice. A NULL pool returns `SS_ERR_NULL_PARAM`, a smaller one `SS_ERR_BUFFER_TOO_SMALL` (no fallback to the heap). The pool must outlive the bus and is never freed by the library. Signals and slots still follow the memory mode.
- Both: `flags` is 0 or `SS_BUS_THREAD_SAFE`. `SS_ERR_CONFIG_MISMATCH` when the caller's `SS_ABI_TAG` (`sizeof(ss_data_t) * 16 + SS_ABI_REVISION`) differs from the library's (another version, `-fshort-enums`, packing), or for a flag the library lacks (`SS_BUS_THREAD_SAFE` without `SS_ENABLE_THREAD_SAFETY`, or unknown bits). `*bus` is NULL on every failure; always check the result. FFI callers that cannot use macros call the `_abi` functions with the tag computed on their side.
- `ss_bus_destroy(bus)` — Releases signals, slots, pending deferred entries and queued ISR events. NULL is a no-op. Disable the interrupts that call `ss_emit_from_isr()` on the bus first. Called from a slot, it is deferred until the bus's outermost call returns; meanwhile the remaining slots are skipped and the bus refuses every call (`SS_ERR_NULL_PARAM`, 0 or NULL). Not thread-safe with respect to other threads using the same bus. Other buses are not affected.
- Version macros: `SS_VERSION_MAJOR`, `SS_VERSION_MINOR`, `SS_VERSION_PATCH`, `SS_VERSION_STRING`.

### Signals (4 functions)

```c
ss_error_t ss_signal_register(ss_bus_t* bus, const char* signal_name);
ss_error_t ss_signal_register_ex(ss_bus_t* bus, const char* signal_name,
                                 const char* description, ss_priority_t priority);
ss_error_t ss_signal_unregister(ss_bus_t* bus, const char* signal_name);
int        ss_signal_exists(ss_bus_t* bus, const char* signal_name);
```

- `ss_signal_register()` — Register a named signal on the bus. Returns `SS_ERR_ALREADY_EXISTS` if duplicate on that bus, `SS_ERR_WOULD_OVERFLOW` if the (namespace-qualified) name does not fit `SS_MAX_SIGNAL_NAME_LENGTH` or the bus's static signal array is full.
- `ss_signal_register_ex()` — Register with description and default priority. The description is stored only with `SS_ENABLE_INTROSPECTION`; in static mode it must fit `SS_MAX_DESCRIPTION_LENGTH` (default 32 incl. NUL) or registration fails with `SS_ERR_WOULD_OVERFLOW`.
- `ss_signal_unregister()` — Remove signal and disconnect all its slots. Safe from a slot of that signal: the remaining slots of that emission are skipped, memory is released when the emission ends, and the name can be registered again at once.
- `ss_signal_exists()` — Returns 1 if the signal exists on this bus, 0 otherwise.
- All name-taking functions apply the bus's default namespace (see Namespace).

### Connections (5 functions)

```c
ss_error_t ss_connect(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot,
                      void* user_data);
ss_error_t ss_connect_ex(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot,
                         void* user_data, ss_priority_t priority, ss_connection_t* handle);
ss_error_t ss_disconnect(ss_bus_t* bus, const char* signal_name, ss_slot_func_t slot);
ss_error_t ss_disconnect_handle(ss_bus_t* bus, ss_connection_t handle);
ss_error_t ss_disconnect_all(ss_bus_t* bus, const char* signal_name);
```

- `ss_connect()` — Connect slot at `SS_PRIORITY_NORMAL` (5).
- `ss_connect_ex()` — Connect with explicit priority and optional connection handle output. `SS_ERR_MAX_SLOTS` when the signal already has the bus's `max_slots_per_signal` live slots; static mode returns `SS_ERR_WOULD_OVERFLOW` when the bus's `SS_MAX_SLOTS` pool is exhausted.
- `ss_disconnect()` — Disconnect by function pointer. Only removes first match.
- `ss_disconnect_handle()` — Disconnect by opaque handle from `ss_connect_ex()`, on the bus that issued it. Handle 0 returns `SS_ERR_NULL_PARAM`.
- `ss_disconnect_all()` — Disconnect all slots from a signal.
- All of these are safe from inside slots, including on the signal being emitted. A slot disconnected during an emission is not called afterwards and is freed when the emission ends. A slot connected during an emission of the same signal may or may not be called by that emission (depends on its priority position): do not rely on either.

### Emission (8 functions)

```c
ss_error_t ss_emit(ss_bus_t* bus, const char* signal_name, const ss_data_t* data);
ss_error_t ss_emit_void(ss_bus_t* bus, const char* signal_name);
ss_error_t ss_emit_int(ss_bus_t* bus, const char* signal_name, int value);
ss_error_t ss_emit_float(ss_bus_t* bus, const char* signal_name, float value);
ss_error_t ss_emit_double(ss_bus_t* bus, const char* signal_name, double value);
ss_error_t ss_emit_string(ss_bus_t* bus, const char* signal_name, const char* value);
ss_error_t ss_emit_pointer(ss_bus_t* bus, const char* signal_name, void* value);
ss_error_t ss_emit_from_isr(ss_bus_t* bus, const char* signal_name, int value);  // Requires SS_ENABLE_ISR_SAFE
```

- Type-specific helpers (`ss_emit_int`, `ss_emit_float`, etc.) construct a stack-local `ss_data_t` and call `ss_emit()`.
- Slots may call any library function, including emitting, unregistering or `ss_bus_destroy()` on their own bus. A slot that needs its bus gets it through `user_data` (the slot signature has no bus).
- `ss_emit_from_isr(bus, ...)` — int only. Each bus has its own ISR queue (`SS_ISR_QUEUE_SIZE` entries, default 16). No locks (also on a `SS_BUS_THREAD_SAFE` bus), no allocation, no slot calls; claims an entry of the bus's queue with atomic compare-and-swap, so it is safe with nested interrupts, from several threads and during any other call on the bus except `ss_bus_destroy()`. The ISR gets the bus from a global set **before the interrupts that emit are enabled**; disable them **before `ss_bus_destroy()`**, which drops the events still queued. Returns `SS_ERR_NULL_PARAM` for a NULL bus or NULL/empty names (nothing queued), `SS_ERR_WOULD_OVERFLOW` if the bus's queue is full or the name is too long.
- `ss_error_t ss_process_isr_queue(ss_bus_t* bus)` (with `SS_ENABLE_ISR_SAFE`) — **Required** to deliver ISR events: call it from the main loop. Delivers the events queued on `bus` (never another bus's), oldest first, resolving names against that bus's default namespace at delivery. Returns `SS_ERR_NULL_PARAM` for a NULL or closing bus, else the last emission error (e.g. `SS_ERR_NOT_FOUND`).
- On cores without compare-and-swap (Cortex-M0, AVR) define `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` (e.g. save PRIMASK + `__disable_irq()` / restore).

### Deferred Emission (2 functions)

```c
ss_error_t ss_emit_deferred(ss_bus_t* bus, const char* signal_name, const ss_data_t* data);
ss_error_t ss_flush_deferred(ss_bus_t* bus);
```

- `ss_emit_deferred()` — Queue emission on the bus's own queue for later (FIFO, `SS_DEFERRED_QUEUE_SIZE` entries per bus, default 64, 16 with `SS_EMBEDDED_BUILD`). The queue stores a **copy** of the payload: string and custom data are copied, so the caller may free its data right after the call. The `value.custom.cleanup` function is NOT called for the copy (slots see the bytes, not your object's lifetime). In static mode string/custom payloads must fit `SS_DEFERRED_PAYLOAD_SIZE` (default 32) bytes. `SS_ERR_WOULD_OVERFLOW` when the queue is full, the name is too long, or the payload is too large; `SS_ERR_INVALID_TYPE` for `SS_TYPE_CUSTOM` data in a library built without `SS_ENABLE_CUSTOM_DATA`.
- `ss_flush_deferred(bus)` — Emit the entries queued on that bus when it starts, oldest first. Entries queued later (by slots) wait for the next flush, even if a slot runs a nested `ss_flush_deferred()`. Returns the last emission error. Other buses' queues are not touched.
- Pending entries are released by `ss_bus_destroy()`.

### Data Handling (18 functions, no bus)

```c
// Initialization, release, creation/destruction
#define    SS_DATA_INIT  /* empty SS_TYPE_VOID object: ss_data_t d = SS_DATA_INIT; */
void       ss_data_init(ss_data_t* data);    // = SS_DATA_INIT, for memory of any content
void       ss_data_clear(ss_data_t* data);   // free an owned payload, become SS_TYPE_VOID
ss_data_t* ss_data_create(ss_data_type_t type);
void       ss_data_destroy(ss_data_t* data); // ss_data_clear + free the object

// Setters (each first releases the payload the object owns, if any)
ss_error_t ss_data_set_int(ss_data_t* data, int value);
ss_error_t ss_data_set_float(ss_data_t* data, float value);
ss_error_t ss_data_set_double(ss_data_t* data, double value);
ss_error_t ss_data_set_string(ss_data_t* data, const char* value);      // copies: owned
ss_error_t ss_data_set_string_ref(ss_data_t* data, const char* value);  // borrows
ss_error_t ss_data_set_pointer(ss_data_t* data, void* value);
// Requires SS_ENABLE_CUSTOM_DATA:
ss_error_t ss_data_set_custom(ss_data_t* data, const void* value, size_t size,
                              ss_cleanup_func_t cleanup);                // copies: owned
ss_error_t ss_data_set_custom_ref(ss_data_t* data, void* value, size_t size);  // borrows

typedef void (*ss_cleanup_func_t)(const void* data, size_t size);  // must not free data

// Getters
int         ss_data_get_int(const ss_data_t* data, int default_val);
float       ss_data_get_float(const ss_data_t* data, float default_val);
double      ss_data_get_double(const ss_data_t* data, double default_val);
const char* ss_data_get_string(const ss_data_t* data);
void*       ss_data_get_pointer(const ss_data_t* data);
void*       ss_data_get_custom(const ss_data_t* data, size_t* size);  // Requires SS_ENABLE_CUSTOM_DATA
```

- Data objects belong to no bus and take no lock: each object belongs to its caller.
- `ss_data_create()` — Heap-allocates an empty `ss_data_t` (in static mode too). Must be freed with `ss_data_destroy()`.
- `ss_data_set_string()` — **Copies** the string (with `SS_MALLOC`, or `SS_STRDUP` if defined); the object owns the copy.
- `ss_data_set_custom()` — **Copies** the data via `memcpy` into a new allocation; the object owns the copy.
- `ss_data_set_string_ref()` / `ss_data_set_custom_ref()` — **Borrow**: store your pointer, never allocate (usable without a heap), never free it.
- Every `ss_data_set_*` first releases the payload the object owns, if any (a borrowed payload is not freed). `ss_data_set_string`/`ss_data_set_custom` keep the old value if allocation fails (`SS_ERR_MEMORY`).
- `ss_data_clear()` — Frees an owned string/custom payload (calling `value.custom.cleanup` on the copy first) and makes the object `SS_TYPE_VOID`. `ss_data_destroy()` = `ss_data_clear()` + free of the struct.
- Getters return default/NULL on type mismatch — they do not coerce types.

**Ownership contract (3.0):**
- `data->owned` (read-only for callers) is set only by the copying setters `ss_data_set_string` / `ss_data_set_custom`. The library frees a payload only when `owned` is set, exactly once: on the next setter, `ss_data_clear()` or `ss_data_destroy()`.
- Everything else borrows: direct field writes, the `_ref` setters, the `ss_emit_*` helpers, and the data slots receive (`owned` is always 0 there, also for deferred/batch copies and when the emitter's object owns its payload). Misusing a borrowed object leaks at worst; it never frees memory the library does not own.
- Initialize every `ss_data_t` with `SS_DATA_INIT` or `ss_data_init()` (an uninitialized `owned` could be nonzero). A stack object filled by `ss_data_set_string`/`_custom` must be released with `ss_data_clear()` before it goes out of scope.
- Do not write `value.s_val` / `value.custom` by hand on an object that owns its payload: `ss_data_clear()` it first, or use the `_ref` setters.
- The cleanup is called once with the library's copy as `const void*` plus its size, just before the library frees that copy. It releases resources the copy *references*; it must NOT free the pointer (the `const` makes `free(data)` a compiler diagnostic).

### Introspection (3 functions, requires `SS_ENABLE_INTROSPECTION`)

```c
size_t     ss_get_signal_count(ss_bus_t* bus);
ss_error_t ss_get_signal_list(ss_bus_t* bus, ss_signal_info_t** list, size_t* count);
void       ss_free_signal_list(ss_signal_info_t* list, size_t count);   // no bus
```

- `ss_signal_info_t` contains: `name`, `slot_count`, `description`, `priority`.
- `name` and `description` are copies owned by the caller and stay valid after signals change (and after the bus is destroyed). Must call `ss_free_signal_list()` after `ss_get_signal_list()`. Allocates in both memory modes.

### Memory Stats (2 functions, requires `SS_ENABLE_MEMORY_STATS`)

```c
ss_error_t ss_get_memory_stats(ss_bus_t* bus, ss_memory_stats_t* stats);
void       ss_reset_memory_stats(ss_bus_t* bus);
```

- Per bus. Dynamic mode: `total_bytes_allocated` = heap bytes the bus holds (including the bus itself when `ss_bus_create` allocated it), `peak_bytes_allocated` = its maximum. Static mode: both = size of the bus. `signals_used` / `slots_used` count live ones. Reset sets the peak to the current value.

### Performance Stats (3 functions, requires `SS_ENABLE_PERFORMANCE_STATS`)

```c
ss_error_t ss_get_perf_stats(ss_bus_t* bus, const char* signal_name, ss_perf_stats_t* stats);
ss_error_t ss_enable_profiling(ss_bus_t* bus, int enabled);
void       ss_reset_perf_stats(ss_bus_t* bus);
```

### Error Handling (2 functions)

```c
const char* ss_error_string(ss_error_t error);
void        ss_set_error_handler(ss_bus_t* bus, void (*handler)(ss_error_t error, const char* msg));
```

- The handler is per bus. It is called, after the bus is unlocked, for every failing call on that bus, including parameter errors (NULL arguments, handle 0, a batch already being destroyed, too-long names). `msg` is the signal name when there is one, else `ss_error_string(err)`. Calls given no usable bus (a NULL bus, a NULL batch, a bus being destroyed) reach no handler. Errors on one bus never reach another bus's handler.

### Configuration (2 functions)

```c
void   ss_set_max_slots_per_signal(ss_bus_t* bus, size_t max_slots);
size_t ss_get_max_slots_per_signal(ss_bus_t* bus);
```

- Per bus; a new bus starts at `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL`. Thread safety is not a runtime setting: it is the `SS_BUS_THREAD_SAFE` creation flag.

### Namespace (3 functions)

```c
ss_error_t ss_set_namespace(ss_bus_t* bus, const char* ns);
ss_error_t ss_get_namespace(ss_bus_t* bus, char* buf, size_t size);
ss_error_t ss_emit_namespaced(ss_bus_t* bus, const char* ns, const char* signal_name,
                              const ss_data_t* data);
```

- Convention: `ns::signal_name`. The default namespace belongs to the bus.
- While a default namespace is set, **every** name without `"::"` is resolved as `"ns::name"` by all name-taking functions on that bus (register, unregister, exists, connect(_ex), disconnect(_all), emit*, emit_deferred, batch_add for batches of that bus, get_perf_stats; ISR events at delivery by `ss_process_isr_queue(bus)`). Names containing `"::"` are used as given. Register and connect while the same namespace is set, or use fully qualified names.
- `NULL` or `""` clears it. `SS_ERR_WOULD_OVERFLOW` if even `"ns::x"` would not fit. `ss_get_namespace(bus, buf, size)` copies the namespace into `buf` under the bus lock (safe while other threads set it): `""` when unset; `SS_ERR_NULL_PARAM` for a NULL bus or `buf`; `SS_ERR_BUFFER_TOO_SMALL` when it does not fit, and then `buf` holds `""` (never a truncated namespace). `char ns[SS_MAX_SIGNAL_NAME_LENGTH]` always fits. Save with `ss_get_namespace`, restore with `ss_set_namespace(bus, ns)`.
- `ss_emit_namespaced(bus, ns, name, data)` always emits `"ns::name"` and ignores the default namespace.

### Batch Operations (4 functions)

```c
ss_batch_t* ss_batch_create(ss_bus_t* bus);
void        ss_batch_destroy(ss_batch_t* batch);
ss_error_t  ss_batch_add(ss_batch_t* batch, const char* signal_name, const ss_data_t* data);
ss_error_t  ss_batch_emit(ss_batch_t* batch);
```

- A batch belongs to the bus it was created for: that bus supplies the default namespace when entries are added, runs the emission and reports errors. `ss_batch_create()` allocates in both memory modes; returns NULL for a NULL bus, a bus being destroyed, or no memory.
- `ss_batch_add()` copies payloads exactly like `ss_emit_deferred` (custom cleanup not called for copies; static payload limit `SS_DEFERRED_PAYLOAD_SIZE`). Capacity `SS_BATCH_MAX_ENTRIES` (defaults to `SS_DEFERRED_QUEUE_SIZE`); `SS_ERR_WOULD_OVERFLOW` when full.
- `ss_batch_emit()` emits the entries on the batch's bus, oldest first, and empties the batch; the batch can be reused. Returns `SS_ERR_NULL_PARAM` for a NULL batch or a bus being destroyed. Slots may add to the batch being emitted (delivered by the next `ss_batch_emit`) or destroy it (freed when `ss_batch_emit` returns; remaining entries dropped).
- `ss_batch_destroy()` releases remaining entries and the batch. Destroy batches before their bus. `ss_batch_destroy()` itself does not touch the bus, so a batch whose bus was destroyed from one of its slots can still be destroyed; no other batch call may follow.
- A batch must not be used from several threads at once.

### Debug (2 functions, requires `SS_ENABLE_DEBUG_TRACE`)

```c
void ss_enable_trace(ss_bus_t* bus, FILE* output);
void ss_disable_trace(ss_bus_t* bus);
```

- A bus's trace is off until `ss_enable_trace(bus, stream)` names a stream; `ss_disable_trace(bus)` stops it. `ss_lib.h` includes `<stdio.h>` itself in this configuration.

## Slot Function Signature

Every slot callback must match:

```c
void my_slot(const ss_data_t* data, void* user_data);
```

- `data` — Signal payload, borrowed and valid only during the call. Always `const`. May be NULL when the emitter passed NULL to `ss_emit()` (`ss_emit_void()` passes an `SS_TYPE_VOID` object).
- `user_data` — Pointer passed at `ss_connect()` time. Cast as needed. There is no bus parameter: a slot that calls the library passes its bus through `user_data` (directly, or in a struct with its other state).

## Data Types

```c
typedef enum {
    SS_TYPE_VOID,     // No data
    SS_TYPE_INT,      // int
    SS_TYPE_FLOAT,    // float
    SS_TYPE_DOUBLE,   // double
    SS_TYPE_STRING,   // const char* (null-terminated)
    SS_TYPE_POINTER,  // void*
    SS_TYPE_CUSTOM    // value.custom.ptr/size/cleanup; read with ss_data_get_custom()
} ss_data_type_t;
```

Every enumerator and `ss_data_t` member exists in every configuration (`ss_data_t` is 32 bytes on 64-bit, 24 on Cortex-M). The custom data functions need `SS_ENABLE_CUSTOM_DATA`; a library without it returns `SS_ERR_INVALID_TYPE` when custom data is queued (`ss_emit_deferred`, `ss_batch_add`).

## String Handling — Critical Distinction

**`ss_emit_string()` does NOT copy the string.** It builds a stack `ss_data_t` whose `value.s_val` points at your string and emits it synchronously. Slots see your pointer only for the duration of their call.

**`ss_data_set_string()` DOES copy the string** into memory owned by the `ss_data_t` object (release it with `ss_data_clear()` or `ss_data_destroy()`). **`ss_data_set_string_ref()` does NOT copy**: the object borrows your string.

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
SS_ERR_NULL_PARAM        // NULL parameter passed (also a NULL bus, or one being destroyed)
SS_ERR_MEMORY            // Memory allocation failed
SS_ERR_NOT_FOUND         // Signal or slot not found
SS_ERR_ALREADY_EXISTS    // Signal already registered
SS_ERR_INVALID_TYPE      // Custom data queued in a library built without SS_ENABLE_CUSTOM_DATA
SS_ERR_BUFFER_TOO_SMALL  // Pool too small for ss_bus_create_in(), or buf for ss_get_namespace()
SS_ERR_MAX_SLOTS         // Signal already has max_slots_per_signal live slots
SS_ERR_WOULD_OVERFLOW    // Name, payload, description, queue/pool or the built-in buses exceed a fixed limit
SS_ERR_CONFIG_MISMATCH   // Caller's SS_ABI_TAG differs, or a bus flag the library lacks
```

The values are fixed (0 to 7, then 9 and 11; 8 and 10 are unused), so they may be stored or compared as numbers.

Most functions return `ss_error_t`. Always check return values, especially `ss_bus_create()`, `ss_connect()`, and `ss_emit()`.

## Thread Safety

Thread safety is **compiled in** by default (`SS_ENABLE_THREAD_SAFETY 1`) and **chosen per bus at creation**:

```c
ss_bus_t* bus = NULL;
if (ss_bus_create(&bus, SS_BUS_THREAD_SAFE) != SS_OK) return 1;  // before threads use it
// ... use bus from multiple threads ...
// join the threads, then:
ss_bus_destroy(bus);
```

- A bus created with `SS_BUS_THREAD_SAFE` has its own **recursive** mutex (pthread recursive mutex / Windows `CRITICAL_SECTION`). A bus created without it has no mutex and must be used from one thread at a time. The flag in a library built without `SS_ENABLE_THREAD_SAFETY` (e.g. `SS_EMBEDDED_BUILD`, `SS_MINIMAL_BUILD`) returns `SS_ERR_CONFIG_MISMATCH`.
- Buses share no mutable state, so threads that each use their own bus need no flag and never contend.
- On a thread-safe bus every function touching its state locks (signals, slots, deferred queue, namespace, error handler, profiling flag, max slots).
- Slots run with their bus's mutex held. They may call back into the same bus on the same thread, but must NOT wait (join, condition variable, blocking queue) for another thread that itself calls that bus: deadlock.
- **Cross-bus calls from slots go in one direction only.** A slot of bus A that calls bus B holds A's lock while it takes B's; if another thread's slot of B calls A at the same time, both wait forever. Pick an order (e.g. "device" bus → "UI" bus) and never call back the other way from a slot.
- `ss_bus_destroy()` is not thread-safe with respect to other threads using the same bus: join them first. In static mode, built-in buses are claimed and released without a lock: create and destroy them from one thread. `ss_bus_create_in()` and dynamic `ss_bus_create()` may run in any thread.
- A batch must not be shared between threads. The ISR path takes no lock.

## Configuration (`ss_config.h`)

### Feature Flags (defaults)

| Flag | Default | Description |
|------|---------|-------------|
| `SS_USE_STATIC_MEMORY` | 0 | Use pre-allocated pools instead of malloc |
| `SS_ENABLE_THREAD_SAFETY` | 1 | Compile thread-safety support (`SS_BUS_THREAD_SAFE`) |
| `SS_ENABLE_INTROSPECTION` | 1 | Signal listing/counting APIs |
| `SS_ENABLE_CUSTOM_DATA` | 1 | Custom data functions and queuing of SS_TYPE_CUSTOM |
| `SS_ENABLE_PERFORMANCE_STATS` | 0 | Per-signal timing stats |
| `SS_ENABLE_MEMORY_STATS` | 0 | Memory usage tracking |
| `SS_ENABLE_DEBUG_TRACE` | 0 | Debug trace output |
| `SS_ENABLE_ISR_SAFE` | 0 | ISR-safe emission + `ss_process_isr_queue` |

### Limits

| Macro | Default | Description |
|-------|---------|-------------|
| `SS_MAX_SIGNALS` | 32 | Max signals per bus (static mode only) |
| `SS_MAX_SLOTS` | 128 | Max total slots per bus (static mode only) |
| `SS_MAX_SIGNAL_NAME_LENGTH` | 256 (dynamic) / 32 (static) | Max signal name length |
| `SS_DEFAULT_MAX_SLOTS_PER_SIGNAL` | 100 | Max slots per signal, a new bus's setting |
| `SS_DEFERRED_QUEUE_SIZE` | 64 (16 with `SS_EMBEDDED_BUILD`) | Deferred queue size per bus |
| `SS_BATCH_MAX_ENTRIES` | `SS_DEFERRED_QUEUE_SIZE` | Entries per batch |
| `SS_DEFERRED_PAYLOAD_SIZE` | 32 | Static mode: max string/custom bytes per deferred/batch entry |
| `SS_MAX_DESCRIPTION_LENGTH` | 32 | Static mode: description buffer incl. NUL |
| `SS_STATIC_BUSES` | 1 | Static mode: built-in buses for `ss_bus_create()` in `.bss`, each `ss_bus_pool_size()` bytes; 0 = none (buses only from `ss_bus_create_in()` pools) |
| `SS_ISR_QUEUE_SIZE` | 16 | ISR emission queue size, per bus (704 bytes per bus with static-mode defaults on 32-bit) |
| `SS_ISR_CRITICAL_ENTER/EXIT` | undefined | Interrupt-disable hooks for cores without CAS |
| `SS_CACHE_LINE_SIZE` | 64 | Not used by the implementation |

### Presets

- **`SS_MINIMAL_BUILD`** — Disables: thread safety, introspection, custom data, performance stats, memory stats.
- **`SS_EMBEDDED_BUILD`** — Enables static memory, disables thread safety, sets max slots per signal to 10, deferred queue to 16.
- Define presets and flags on the command line for the library AND the application.

### Static Memory Mode Rules

- Each bus holds its own signal array, slot array, deferred queue, ISR queue (with `SS_ENABLE_ISR_SAFE`) and namespace buffer: a second bus costs the same RAM again (`ss_bus_pool_size()`). `ss_bus_create()` takes a built-in bus (`SS_STATIC_BUSES`, default 1), `ss_bus_create_in()` uses your memory; register, connect, emit, deferred, ISR queueing and delivery, namespace, disconnect, unregister and destroy never touch the heap on any bus.
- Still allocating (by design, they hand you an object): `ss_data_create`, `ss_data_set_string`, `ss_data_set_custom`, `ss_get_signal_list`, `ss_batch_create`. Avoid them where no heap exists; use the `ss_emit_*` helpers, or a stack `ss_data_t d = SS_DATA_INIT;` filled with `ss_data_set_string_ref` / `ss_data_set_custom_ref` (no heap).
- Limits are enforced with `SS_ERR_WOULD_OVERFLOW` (names, descriptions, payloads, full pools, all built-in buses in use), never silent truncation.

### Custom Allocators

Override when building the library (e.g. `-DSS_MALLOC(n)=my_malloc(n)` or a wrapper header included first):

```c
#define SS_MALLOC(size)        my_malloc(size)
#define SS_FREE(ptr)           my_free(ptr)
#define SS_CALLOC(count, size) my_calloc(count, size)
/* optional: SS_STRDUP(str); by default strings are copied with SS_MALLOC */
```

Overriding `SS_MALLOC`/`SS_FREE`/`SS_CALLOC` routes every allocation, including string copies and dynamic-mode buses, so `ss_data_destroy` / `ss_free_signal_list` / `ss_bus_destroy` free with the matching `SS_FREE`.

## Connection Handles

`ss_connection_t` is an opaque `uintptr_t` returned by `ss_connect_ex()`:

```c
ss_connection_t handle;
ss_connect_ex(bus, "sig", my_slot, NULL, SS_PRIORITY_HIGH, &handle);
// ... later ...
ss_disconnect_handle(bus, handle);  // Disconnect without knowing signal name
```

Handles are monotonically increasing integers starting at 1, **per bus**: two buses hand out the same values, so pass a handle only to the bus that issued it. A handle of 0 is never assigned and can be used as "no handle" sentinel (`ss_disconnect_handle(bus, 0)` returns `SS_ERR_NULL_PARAM`).

## Canonical Examples

### Basic Usage

```c
#include "ss_lib.h"
#include <stdio.h>

void on_event(const ss_data_t* data, void* user_data) {
    int val = ss_data_get_int(data, 0);
    (void)user_data;
    printf("Event: %d\n", val);
}

int main(void) {
    ss_bus_t* bus = NULL;
    if (ss_bus_create(&bus, 0) != SS_OK) return 1;

    ss_signal_register(bus, "event");
    ss_connect(bus, "event", on_event, NULL);
    ss_emit_int(bus, "event", 42);
    ss_disconnect_all(bus, "event");
    ss_signal_unregister(bus, "event");
    ss_bus_destroy(bus);
    return 0;
}
```

### Priority + Handles

```c
ss_connection_t h1, h2;
ss_connect_ex(bus, "alarm", critical_handler, NULL, SS_PRIORITY_CRITICAL, &h1);
ss_connect_ex(bus, "alarm", logger, NULL, SS_PRIORITY_LOW, &h2);

ss_emit_void(bus, "alarm");  // critical_handler runs first, then logger

ss_disconnect_handle(bus, h1);
ss_disconnect_handle(bus, h2);
```

### A Slot That Uses Its Bus

```c
void on_saved(const ss_data_t* data, void* user_data) {
    ss_bus_t* bus = (ss_bus_t*)user_data;            // the bus, passed at connect time
    ss_emit_string(bus, "status", "saved");
    (void)data;
}
ss_connect(bus, "file_saved", on_saved, bus);
```

### Several Buses

```c
ss_bus_t* ui = NULL;
ss_bus_t* net = NULL;
if (ss_bus_create(&ui, 0) != SS_OK) return 1;
if (ss_bus_create(&net, 0) != SS_OK) { ss_bus_destroy(ui); return 1; }

ss_signal_register(ui, "update");     // two different signals with one name
ss_signal_register(net, "update");
ss_emit_void(net, "update");          // only net's slots run

ss_bus_destroy(net);
ss_bus_destroy(ui);
```

### Embedded Static Memory

```c
/* Build library and application with -DSS_EMBEDDED_BUILD (never #define it here) */
#include "ss_lib.h"

static ss_bus_t* bus;

int main(void) {
    if (ss_bus_create(&bus, 0) != SS_OK) return 1;    /* static mode: a built-in bus in .bss */
    ss_signal_register(bus, "sensor");
    ss_connect(bus, "sensor", read_sensor, NULL);
    ss_emit_int(bus, "sensor", adc_read());
    ss_bus_destroy(bus);
    return 0;
}

/* Alternative: put the bus in your own buffer (either memory mode; with
 * -DSS_STATIC_BUSES=0 no built-in bus takes .bss) */
static unsigned char pool[8192];
/* ... */
ss_error_t err = ss_bus_create_in(&bus, pool, sizeof(pool), 0);
if (err == SS_ERR_BUFFER_TOO_SMALL) { /* need ss_bus_pool_size() bytes */ }
```

### Custom Data

```c
typedef struct { int x, y; } Point;

void on_move(const ss_data_t* data, void* user_data) {
    size_t size;
    Point* p = (Point*)ss_data_get_custom(data, &size);
    (void)user_data;
    if (p && size == sizeof(Point)) {
        printf("Moved to (%d, %d)\n", p->x, p->y);
    }
}

// Usage:
ss_data_t* d = ss_data_create(SS_TYPE_CUSTOM);
Point pt = {10, 20};
ss_data_set_custom(d, &pt, sizeof(Point), NULL);  // Copies pt
ss_emit(bus, "moved", d);
ss_data_destroy(d);

// On the stack: borrow (no heap), or copy and clear
ss_data_t s = SS_DATA_INIT;
ss_data_set_custom_ref(&s, &pt, sizeof(Point));   // pt must outlive the use of s
ss_emit(bus, "moved", &s);
ss_data_set_custom(&s, &pt, sizeof(Point), NULL); // owned copy
ss_emit_deferred(bus, "moved", &s);               // the queue copies again
ss_data_clear(&s);                                // frees the owned copy

// With nested resources: cleanup releases what the copy references, never the copy itself
typedef struct { int* items; size_t n; } List;
void list_cleanup(const void* p, size_t size) {
    (void)size;
    free(((const List*)p)->items);   // NOT free(p): the library frees the copy
}
```

### ISR Emission

```c
// Compile with -DSS_ENABLE_ISR_SAFE=1

static ss_bus_t* bus;   // set by ss_bus_create() before the timer interrupt is enabled

// In ISR handler (no locks, no malloc):
void TIM2_IRQHandler(void) {
    ss_emit_from_isr(bus, "timer_tick", tick_count);  // int only, queued on this bus
}

// In main loop — deliver the bus's queued ISR events (nothing is delivered otherwise):
for (;;) {
    ss_process_isr_queue(bus);   // oldest first, slots run here in normal context
    /* ... */
}
```

### Deferred / Batch

```c
// Deferred: queue now, emit later
ss_emit_deferred(bus, "update", data);   // Payload copied; data may be destroyed now
ss_emit_deferred(bus, "refresh", NULL);
ss_flush_deferred(bus);                  // Emits queued entries; slots' new entries wait

// Batch: group emissions on one bus
ss_batch_t* batch = ss_batch_create(bus);
ss_batch_add(batch, "save", data1);      // Payload copied
ss_batch_add(batch, "log", data2);
ss_batch_emit(batch);                    // Emits all on bus, empties the batch
ss_batch_destroy(batch);                 // Frees batch (before ss_bus_destroy(bus))
```

### Namespace

```c
ss_set_namespace(bus, "audio");
ss_signal_register(bus, "volume_changed");            // registers "audio::volume_changed"
ss_connect(bus, "volume_changed", on_volume, NULL);   // connects to "audio::volume_changed"
ss_emit_int(bus, "volume_changed", 7);                // emits "audio::volume_changed"
ss_emit_int(bus, "ui::click", 1);                     // contains "::": used as given
ss_emit_namespaced(bus, "video", "frame", data);      // "video::frame", ignores default
ss_set_namespace(bus, NULL);                          // clear
```

### Re-entrant Slot

```c
void one_shot(const ss_data_t* data, void* ud) {
    ss_bus_t* bus = (ss_bus_t*)ud;
    (void)data;
    ss_disconnect(bus, "ready", one_shot);   // safe: freed after this emission
}
void shutdown_slot(const ss_data_t* data, void* ud) {
    ss_bus_t* bus = (ss_bus_t*)ud;
    (void)data;
    ss_bus_destroy(bus);   // safe: deferred until the bus's outermost call returns;
                           // the caller must not use bus after that call returns
}
```
