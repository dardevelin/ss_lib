# Skill: Reviewing and Debugging SS_Lib Code

## Trigger

Activate when user asks to review, debug, audit, or fix code that uses SS_Lib (`ss_lib.h`).

## Review Checklist

Work through each item. Report findings with: what's wrong, the consequence, and the fix.

### 1. Missing `ss_init()`

**Look for:** Any SS_Lib API call without a preceding `ss_init()`, `ss_init_static()` or their `_abi` variants.

**Consequence:** Every call fails: `ss_error_t` functions return `SS_ERR_NULL_PARAM`, others return 0/NULL or do nothing. The error handler is not called (it is only set on an initialized library). Exception: `ss_emit_from_isr()` queues before `ss_init()`, but nothing is delivered until `ss_process_isr_queue()` runs on an initialized library.

**Fix:** Call `ss_init_abi(SS_ABI_TAG)` (recommended; or `ss_init()` / `ss_init_static()`) at program startup before any other SS_Lib call.

### 2. Unregistered Signals

**Look for:** `ss_connect()` or `ss_emit*()` on a signal name that was never passed to `ss_signal_register()`.

**Consequence:** Returns `SS_ERR_NOT_FOUND`. Slots never fire. Easy to miss if return value is unchecked.

**Fix:** Always register signals before connecting or emitting. Check return values.

### 3. `ss_emit_string()` with Temporary Strings

**Look for:** `ss_emit_string()` called with stack-local buffers, `sprintf` results, or strings that are freed/modified after the call.

**Consequence:** `ss_emit_string()` stores the pointer directly — it does NOT copy. If the string is on the stack and the slot stores or defers the pointer, it becomes a dangling pointer.

**Fix:** The string must outlive the emission, and a slot that keeps the string must copy it: the pointer a slot receives is only valid during the slot call, whatever the emitter used (including deferred/batch, whose copies are freed right after each emission). `ss_emit_deferred()` and `ss_batch_add()` copy string payloads internally, so emitters may pass temporary buffers to them.

```c
// WRONG — stack buffer may be gone by the time deferred slot reads it
char buf[64];
snprintf(buf, sizeof(buf), "value: %d", x);
ss_emit_string("log", buf);  // OK only if all slots read synchronously and don't store the pointer

// SAFE for later delivery — the deferred queue copies the string
ss_emit_deferred("log", &(ss_data_t){ .type = SS_TYPE_STRING, .value.s_val = buf });  /* C99+ */
// SAFE in the slot — copy what you keep
void logger(const ss_data_t* d, void* ud) { log_append(strdup(ss_data_get_string(d))); }
```

### 4. Unchecked Error Codes

**Look for:** Calls to `ss_init()`, `ss_connect()`, `ss_emit()`, `ss_signal_register()`, or any `ss_error_t`-returning function where the return value is discarded.

**Consequence:** Silent failures. `ss_init()` failure means everything else will fail. `ss_connect()` failure on static memory means slot limit hit.

**Fix:** Always check at minimum `ss_init()` and `ss_connect()`. Use `ss_error_string()` for human-readable messages.

### 5. Thread Safety Not Enabled at Runtime

**Look for:** Multi-threaded code using SS_Lib without calling `ss_set_thread_safe(1)`.

**Consequence:** Data races on the library context. Thread safety is compiled in by default (`SS_ENABLE_THREAD_SAFETY 1`) but **off at runtime** until `ss_set_thread_safe(1)`. (`ss_init()` creates the recursive mutex; the call only turns locking on.)

**Fix:** Call `ss_set_thread_safe(1)` immediately after `ss_init()`, before spawning any threads that use SS_Lib. Also check:
- `ss_set_thread_safe()` toggled while other threads use the library (only allowed when they don't).
- `ss_init()` / `ss_cleanup()` racing with other threads (they are not thread-safe): join workers first.
- `SS_EMBEDDED_BUILD` / `SS_MINIMAL_BUILD` compile thread safety out.

```c
ss_init();
ss_set_thread_safe(1);  // Turns locking on
// NOW safe to use from multiple threads
```

### 5b. Slot Waits for Another Thread (Deadlock)

**Look for:** A slot that joins a thread, waits on a condition variable/semaphore/blocking queue, or otherwise blocks until another thread makes progress, where that other thread calls SS_Lib.

**Consequence:** Slots run with the library mutex held. The other thread blocks on the mutex, the slot blocks on the other thread: deadlock. (Calling back into the library from the slot on the same thread is fine; the mutex is recursive.)

**Fix:** Don't block in slots. Hand work to the other thread without waiting, or post with `ss_emit_deferred()` and flush outside the slot.

### 6. Leaked `ss_data_t`

**Look for:** `ss_data_create()` without matching `ss_data_destroy()`; a stack `ss_data_t` filled by `ss_data_set_string()` / `ss_data_set_custom()` that goes out of scope without `ss_data_clear()`.

**Consequence:** Memory leak. If the data holds a string (via `ss_data_set_string()`), the copied string also leaks. If custom data with a cleanup function, the cleanup never runs.

**Fix:** Always pair `ss_data_create()` with `ss_data_destroy()`, and a stack object that used a copying setter with `ss_data_clear()`. Consider using the type-specific emit helpers, or the borrowing `ss_data_set_string_ref()` / `ss_data_set_custom_ref()`, instead. (Reusing one object with several `ss_data_set_*` calls is fine: each setter releases the payload the object owns.)

### 6b. `ss_data_t` Ownership Violations

In 3.0 an `ss_data_t` records ownership in `owned`, set only by the copying setters (`ss_data_set_string`, `ss_data_set_custom`); the library frees only owned payloads. Hand-filled objects, `_ref` setters, the `ss_emit_*` helpers and the data slots receive all borrow, so most 2.x invalid frees are gone. What remains:

**Look for:**
- An `ss_data_t` used without `SS_DATA_INIT`, `ss_data_init()` or `ss_data_create()` (for example `ss_data_t d; d.type = ...;` then a setter or `ss_data_clear()`): `owned` holds garbage, so the library may free a pointer it never allocated.
- Direct assignment of `value.s_val` or `value.custom.ptr` on an object that currently owns its payload (after `ss_data_set_string`/`_custom`): the next setter, clear or destroy frees the caller's pointer, and the copy leaks. Clear first, or use the `_ref` setters.
- Writing `owned` by hand.
- A cleanup that frees the pointer it receives, or casts away the `const` to modify or free it.
- 2.x code that relied on `ss_data_destroy()` freeing a hand-written `s_val` on an `ss_data_create()` object: in 3.0 that string is borrowed and leaks.

**Consequence:** Invalid free / double free / leak. The cleanup receives the library's copy, which the library frees itself after the cleanup returns.

**Fix:** Initialize every object; set payloads with the setters (copying or `_ref`) rather than by hand; release owned payloads with `ss_data_clear()` / `ss_data_destroy()`; in the cleanup release only what the copy references.

### 7. Static Memory Overflow

**Look for:** In static memory builds (`SS_USE_STATIC_MEMORY`), more than `SS_MAX_SIGNALS` (default 32) signals, more than `SS_MAX_SLOTS` (default 128) total slots, descriptions of `SS_MAX_DESCRIPTION_LENGTH` (32 incl. NUL) or more, names of `SS_MAX_SIGNAL_NAME_LENGTH` (32 incl. NUL, including any `"ns::"` prefix) or more, deferred/batch string or custom payloads larger than `SS_DEFERRED_PAYLOAD_SIZE` (32) bytes.

**Consequence:** Returns `SS_ERR_WOULD_OVERFLOW` (nothing is truncated). `SS_ERR_MAX_SLOTS` means the per-signal limit (`max_slots_per_signal`, 10 with `SS_EMBEDDED_BUILD`), counted over live slots. Silently drops registrations/connections if return values are unchecked.

**Fix:** Size the limits for your application (on the command line, for library and app alike). Check all return values in embedded code. Also flag uses of `ss_data_create`, `ss_data_set_string/custom`, `ss_get_signal_list`, `ss_batch_create` on targets that must not use the heap: these allocate even in static mode.

### 8. ISR with Non-Integer Data

**Look for:** Attempting to pass string, pointer, or custom data from an ISR context.

**Consequence:** `ss_emit_from_isr()` only accepts `int`. There is no ISR-safe variant for other types. Using `ss_emit()` from ISR causes undefined behavior (mutex usage, malloc).

**Fix:** Pass only `int` from ISR. Use the int as an event code, and look up full data in the main loop.

### 8b. ISR Events Never Delivered

**Look for:** `ss_emit_from_isr()` without a regular `ss_process_isr_queue()` call in normal context (main loop, task).

**Consequence:** Events stay queued; once `SS_ISR_QUEUE_SIZE` (16) entries are pending, `ss_emit_from_isr()` returns `SS_ERR_WOULD_OVERFLOW` and events are lost. `ss_cleanup()` discards pending events.

**Fix:** Call `ss_process_isr_queue()` from the main loop. Also check: it is never called from an ISR (it takes the mutex and runs slots); on cores without compare-and-swap (Cortex-M0, AVR) `SS_ISR_CRITICAL_ENTER()`/`SS_ISR_CRITICAL_EXIT()` are defined, otherwise the build fails or uses the wrong atomics; names are resolved against the default namespace at delivery, not at enqueue.

### 9. Missing `ss_cleanup()`

**Look for:** Program exit paths (normal return, error returns, signal handlers) that skip `ss_cleanup()`.

**Consequence:** Memory leaks in dynamic mode. In long-running applications, repeated init/use cycles without cleanup cause unbounded memory growth.

**Fix:** Call `ss_cleanup()` on all exit paths. Consider `atexit(ss_cleanup)` as a safety net.

### 10. Invalid Connection Handles

**Look for:** Using a `ss_connection_t` after the slot was already disconnected, or using handle value 0.

**Consequence:** `ss_disconnect_handle()` returns `SS_ERR_NOT_FOUND`. Handle 0 is never assigned (handles start at 1).

**Fix:** Set handle to 0 after disconnecting. Check return value of `ss_disconnect_handle()`.

### 11. Custom Data Without Cleanup

**Look for:** `ss_data_set_custom()` with a struct that owns nested heap memory but a NULL cleanup function; or a cleanup that frees its argument; or code expecting the cleanup to run for deferred/batch copies.

**Consequence:** `ss_data_set_custom()` copies the struct (shallow `memcpy`); the next setter, `ss_data_clear()` or `ss_data_destroy()` calls the cleanup once on that copy and then frees it. Without a cleanup, nested allocations leak. A cleanup that frees its argument causes a double free (it is `const void*`, so `free(data)` draws a compiler diagnostic; look for casts that hide it). `ss_data_set_custom_ref()` has no cleanup at all. `ss_emit_deferred()` / `ss_batch_add()` store another shallow copy and never call the cleanup for it, so nested pointers must stay valid until the flush, and are owned by the caller's object.

**Fix:** Provide a cleanup that releases only what the copy references:

```c
void point_list_cleanup(const void* data, size_t size) {
    const PointList* pl = (const PointList*)data;
    (void)size;
    free(pl->points);  // Free nested allocation, NOT pl
}
ss_data_set_custom(d, &pl, sizeof(PointList), point_list_cleanup);
```

### 12. Deferred Queue Overflow

**Look for:** High-frequency deferred emission without matching `ss_flush_deferred()`.

**Consequence:** Returns `SS_ERR_WOULD_OVERFLOW` when the queue holds `SS_DEFERRED_QUEUE_SIZE` entries (default 64, 16 with `SS_EMBEDDED_BUILD`), and also for too-long names or (static mode) payloads over `SS_DEFERRED_PAYLOAD_SIZE`. Emissions silently dropped if unchecked. Entries queued by slots during a flush wait for the next flush, so a slot that re-queues itself does not loop but does keep the queue occupied.

**Fix:** Call `ss_flush_deferred()` regularly. Increase `SS_DEFERRED_QUEUE_SIZE` (or `SS_BATCH_MAX_ENTRIES` for batches) if needed. Always check return value.

### 13. Double `ss_init()` Without Cleanup

**Look for:** Calling `ss_init()` twice without `ss_cleanup()` in between.

**Consequence:** `ss_init()` returns `SS_OK` immediately if already initialized — it does NOT reset state. This is safe but may surprise callers expecting a fresh state. If `ss_cleanup()` was called from a slot and the outermost call has not returned yet, `ss_init()` returns `SS_ERR_ALREADY_EXISTS`.

**Fix:** If you need fresh state, call `ss_cleanup()` then `ss_init()`, outside any slot.

### 14. Priority Change Assumptions

**Look for:** Code that assumes slot priority can be changed after connection.

**Consequence:** Priority is set at `ss_connect()` / `ss_connect_ex()` time and the slot is sorted into the linked list by priority at that point. There is no API to change priority after connection.

**Fix:** Disconnect and reconnect with the new priority:

```c
ss_disconnect_handle(handle);
ss_connect_ex("sig", slot, user_data, SS_PRIORITY_HIGH, &handle);
```

### 15. Configuration Mismatch Between Library and Caller

**Look for:** `#define SS_ENABLE_*`, `SS_USE_STATIC_MEMORY`, `SS_MINIMAL_BUILD`/`SS_EMBEDDED_BUILD` or limit macros in a source file before `#include "ss_lib.h"`; build files where the library and the application get different `-D` flags; initialization through plain `ss_init()` / `ss_init_static()` in new code, or an unchecked init result.

**Consequence:** Since 3.0 no flag changes a public type layout or enum value, so a feature-flag mismatch shows up as a compile or link error (a function the library lacks) rather than memory corruption. Limits (`SS_MAX_SIGNAL_NAME_LENGTH`, queue sizes) and the memory mode still differ silently, so the caller's assumptions (e.g. name length limits) are wrong. `ss_init_abi(SS_ABI_TAG)` / `ss_init_static_abi(..., SS_ABI_TAG)` return `SS_ERR_CONFIG_MISMATCH` when the caller's `sizeof(ss_data_t)` or `SS_ABI_REVISION` differs (a header from another version, `-fshort-enums`, packing); plain `ss_init()` does not check. A library built without custom data returns `SS_ERR_INVALID_TYPE` when `SS_TYPE_CUSTOM` data is queued.

**Fix:** Pass every option on the command line, identically for the library and all callers (CMake exports them as PUBLIC compile definitions of `ss_lib::ss_lib`). Initialize with `ss_init_abi(SS_ABI_TAG)` and check the result.

### 16. Default Namespace Surprises

**Look for:** `ss_set_namespace()` set while code uses unqualified names meant for the global scope, or signals registered under one namespace and emitted/connected under another (or none).

**Consequence:** While set, every name without `"::"` is resolved as `"ns::name"` by all name-taking functions (register, unregister, exists, connect, disconnect, emit*, deferred, batch_add, perf stats; ISR events at delivery). A mismatch gives `SS_ERR_NOT_FOUND`. `ss_emit_namespaced()` ignores the default namespace.

**Fix:** Use fully qualified names (`"ui::click"`) across namespace boundaries, or keep the same namespace set for registration and use. Clear it with `ss_set_namespace(NULL)`. Check for `SS_ERR_WOULD_OVERFLOW` when the qualified name is too long.

### 17. Assumptions About Connecting During Emission

**Look for:** A slot that connects a new slot to the signal being emitted and expects it to run (or not run) in the same emission.

**Consequence:** Unspecified: depending on where the new slot's priority places it, the current emission may or may not call it.

**Fix:** Don't rely on either outcome. If the new slot must not see the current event, have it ignore the first call or connect it via a deferred step.

### 18. Expecting Trace Output by Default

**Look for:** Code built with `SS_ENABLE_DEBUG_TRACE` that expects trace on stderr without calling `ss_enable_trace()`.

**Consequence:** Trace is silent until `ss_enable_trace(stream)` names a stream.

**Fix:** Call `ss_enable_trace(stderr)` (or another stream) after `ss_init()`; `ss_disable_trace()` stops it. The `SS_TRACE` macro in `ss_config.h` is deprecated and unused by the library; flag new uses of it.

### No Longer Issues (2.2.0)

Do not report these as bugs in code targeting 2.2.0:
- Calling `ss_disconnect*`, `ss_signal_unregister`, `ss_cleanup`, `ss_emit*`, `ss_emit_deferred`, `ss_flush_deferred` or batch functions from inside a slot, including on the signal being emitted.
- Calling the library from a slot with thread safety enabled (the mutex is recursive).
- Freeing or reusing the emitter's buffer right after `ss_emit_deferred()` / `ss_batch_add()`.
- Calling several `ss_data_set_*` on the same created object (previous payload is released).
- Overriding only `SS_MALLOC`/`SS_FREE`/`SS_CALLOC` (strings are copied with `SS_MALLOC`).

## Anti-Patterns with Corrections

### Anti-Pattern 1: Emitting to unregistered signal

```c
// WRONG
ss_init();
ss_connect("click", on_click, NULL);  // Returns SS_ERR_NOT_FOUND — ignored!
ss_emit_void("click");                // Returns SS_ERR_NOT_FOUND — nothing happens

// CORRECT
ss_init();
ss_signal_register("click");          // Register first
ss_connect("click", on_click, NULL);
ss_emit_void("click");
```

### Anti-Pattern 2: Using SS_Lib from threads without enabling thread safety

```c
// WRONG
ss_init();
pthread_create(&t1, NULL, producer, NULL);  // Race condition!
pthread_create(&t2, NULL, consumer, NULL);

// CORRECT
ss_init();
ss_set_thread_safe(1);                      // Enable BEFORE threads
pthread_create(&t1, NULL, producer, NULL);
pthread_create(&t2, NULL, consumer, NULL);
```

### Anti-Pattern 3: Stack string with ss_emit_string where slot stores the pointer

```c
// WRONG — slot stores data->value.s_val, but it points to stack memory
void logger(const ss_data_t* data, void* ud) {
    log_list_append(ss_data_get_string(data));  // Stores dangling pointer!
}
void emit_status(void) {
    char buf[128];
    snprintf(buf, sizeof(buf), "status: %d", get_status());
    ss_emit_string("status", buf);  // buf is on the stack
}

// CORRECT — the slot copies what it keeps. (Copying in the emitter with
// ss_data_set_string + ss_data_destroy does NOT help: that copy is freed
// right after ss_emit returns.)
void logger(const ss_data_t* data, void* ud) {
    const char* s = ss_data_get_string(data);
    log_list_append(s ? strdup(s) : NULL);   // owned copy
}
```

### Anti-Pattern 4: Forgetting ss_data_destroy

```c
// WRONG — memory leak on every emission
void send_update(int value) {
    ss_data_t* d = ss_data_create(SS_TYPE_INT);
    ss_data_set_int(d, value);
    ss_emit("update", d);
    // Missing ss_data_destroy(d)!
}

// CORRECT
void send_update(int value) {
    ss_emit_int("update", value);  // No allocation needed
}

// Or if you need ss_data_create:
void send_update(int value) {
    ss_data_t* d = ss_data_create(SS_TYPE_INT);
    ss_data_set_int(d, value);
    ss_emit("update", d);
    ss_data_destroy(d);  // Always destroy
}
```

### Anti-Pattern 5: ISR using full emit API

```c
// WRONG — ss_emit may take the mutex and runs every slot inside the ISR
void EXTI0_IRQHandler(void) {
    ss_emit_int("button", 1);  // NOT ISR-safe!
}

// CORRECT — use ISR-safe variant (int only, lock-free, no malloc)
void EXTI0_IRQHandler(void) {
    ss_emit_from_isr("button", 1);  // Queues to the fixed ISR queue
}
// ...and deliver from the main loop
for (;;) { ss_process_isr_queue(); /* ... */ }
```

### Anti-Pattern 6: Freeing the custom data copy in its cleanup

```c
// WRONG — the library frees the copy after calling cleanup: double free.
// The const parameter makes free(p) a diagnostic; a cast only hides it.
void cleanup(const void* p, size_t n) { free(((const Blob*)p)->bytes); free((void*)p); }

// CORRECT
void cleanup(const void* p, size_t n) { (void)n; free(((const Blob*)p)->bytes); }

// WRONG (2.x signature) — no longer converts to ss_cleanup_func_t
void cleanup(void* p) { free(((Blob*)p)->bytes); }
```

## Feature-Flag Awareness

When reviewing code, verify that features used are enabled at compile time:

| API Used | Required Flag | Default |
|----------|--------------|---------|
| `ss_set_thread_safe()`, `ss_is_thread_safe()` | `SS_ENABLE_THREAD_SAFETY` | 1 (on) |
| `ss_get_signal_count()`, `ss_get_signal_list()`, `ss_free_signal_list()` | `SS_ENABLE_INTROSPECTION` | 1 (on) |
| `ss_data_set_custom()`, `ss_data_set_custom_ref()`, `ss_data_get_custom()`, queuing `SS_TYPE_CUSTOM` data | `SS_ENABLE_CUSTOM_DATA` | 1 (on) |
| `ss_get_perf_stats()`, `ss_enable_profiling()`, `ss_reset_perf_stats()` | `SS_ENABLE_PERFORMANCE_STATS` | 0 (off) |
| `ss_get_memory_stats()`, `ss_reset_memory_stats()` | `SS_ENABLE_MEMORY_STATS` | 0 (off) |
| `ss_enable_trace()`, `ss_disable_trace()` | `SS_ENABLE_DEBUG_TRACE` | 0 (off) |
| `ss_emit_from_isr()`, `ss_process_isr_queue()` | `SS_ENABLE_ISR_SAFE` | 0 (off) |

Code using disabled features will fail to compile or link. `SS_TYPE_CUSTOM` and `value.custom` exist in every configuration; without custom data, `ss_emit_deferred()` and `ss_batch_add()` return `SS_ERR_INVALID_TYPE` for it. The `SS_MINIMAL_BUILD` preset disables thread safety, introspection, custom data, performance stats, and memory stats. The `SS_EMBEDDED_BUILD` preset enables static memory, disables thread safety, and sets max slots per signal to 10 and the deferred queue to 16. Flags must be identical for the library and the application (see check 15).

## Memory Model Verification

### Dynamic Mode (default)

- No fixed limits on signal/slot count (bounded by `max_slots_per_signal`, default 100).
- All allocations via `SS_MALLOC`/`SS_CALLOC` (strings via `SS_MALLOC` unless `SS_STRDUP` is defined).
- `ss_cleanup()` must be called to free all resources.
- `ss_data_destroy()` must be called for every `ss_data_create()`, and `ss_data_clear()` for every stack object filled by a copying setter.

### Static Mode (`SS_USE_STATIC_MEMORY`)

- Fixed pools: `SS_MAX_SIGNALS` (32), `SS_MAX_SLOTS` (128); fixed buffers: `SS_MAX_SIGNAL_NAME_LENGTH` (32), `SS_MAX_DESCRIPTION_LENGTH` (32), `SS_DEFERRED_PAYLOAD_SIZE` (32), `SS_DEFERRED_QUEUE_SIZE` entries.
- `ss_init()` uses static storage (`SS_STATIC_CONTEXT=1`, default; with 0 it `SS_CALLOC`s the context); register/connect/emit/deferred/namespace/disconnect/unregister/cleanup never touch the heap.
- Still allocate: `ss_data_create`, `ss_data_set_string`, `ss_data_set_custom`, `ss_get_signal_list`, `ss_batch_create`. `ss_data_set_string_ref` / `ss_data_set_custom_ref` never allocate.
- Returns `SS_ERR_WOULD_OVERFLOW` when pools or buffers are exhausted.
- `ss_cleanup()` clears the pools — safe to re-init.
- `ss_init_static(pool, size)`: pool must be at least `ss_get_required_pool_size()` bytes and outlive `ss_cleanup()` (never freed by the library). A NULL or smaller pool is silently ignored (falls back to `ss_init()`, returns `SS_OK`): flag code that relies on an error, or that sets `SS_STATIC_CONTEXT=0` on a no-heap target with a pool that may be too small (the fallback then uses the heap).

### Custom Allocators

If custom allocators are defined (`SS_MALLOC`, `SS_FREE`, `SS_CALLOC`, `SS_STRDUP`), verify:
- `SS_CALLOC` zeroes memory (SS_Lib depends on this).
- If `SS_STRDUP` is defined, it returns memory freeable by `SS_FREE`.
- All macros are consistent (same allocator family), and the library itself was built with them.

## Correct Slot Callback Patterns

### Type-Safe Data Access

```c
void my_slot(const ss_data_t* data, void* user_data) {
    if (!data) return;  // Handle ss_emit_void() case

    // Getters return default on type mismatch — safe
    int val = ss_data_get_int(data, -1);
    if (val == -1 && data->type != SS_TYPE_INT) {
        // Actually a type mismatch, not the value -1
    }

    // Better: check type explicitly
    if (data->type == SS_TYPE_INT) {
        int val = data->value.i_val;
    }
}
```

### Safe Disconnect During Emit

SS_Lib marks slots removed during emission and frees them after it. From inside a slot it is safe to:
- `ss_disconnect*()` any slot, including itself: a disconnected slot is not called again in that emission.
- `ss_signal_unregister()` the signal being emitted: remaining slots of that emission are skipped, the name can be re-registered at once.
- `ss_cleanup()`: deferred until the outermost library call returns; meanwhile calls fail as if uninitialized and remaining slots are skipped.

Do not rely on a slot connected during the emission being called or skipped (see check 17).

### Const Correctness

The `data` parameter is `const ss_data_t*`. Never cast away const to modify the data in a slot — it may be shared across multiple slot invocations for the same emission. Copy data locally if mutation is needed.
