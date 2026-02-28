# Skill: Reviewing and Debugging SS_Lib Code

## Trigger

Activate when user asks to review, debug, audit, or fix code that uses SS_Lib (`ss_lib.h`).

## Review Checklist

Work through each item. Report findings with: what's wrong, the consequence, and the fix.

### 1. Missing `ss_init()`

**Look for:** Any SS_Lib API call without a preceding `ss_init()` or `ss_init_static()`.

**Consequence:** All API functions check for `g_context` and return error codes or silently fail. Signal registration, connection, and emission will all fail.

**Fix:** Call `ss_init()` (or `ss_init_static()` for embedded) at program startup before any other SS_Lib call.

### 2. Unregistered Signals

**Look for:** `ss_connect()` or `ss_emit*()` on a signal name that was never passed to `ss_signal_register()`.

**Consequence:** Returns `SS_ERR_NOT_FOUND`. Slots never fire. Easy to miss if return value is unchecked.

**Fix:** Always register signals before connecting or emitting. Check return values.

### 3. `ss_emit_string()` with Temporary Strings

**Look for:** `ss_emit_string()` called with stack-local buffers, `sprintf` results, or strings that are freed/modified after the call.

**Consequence:** `ss_emit_string()` stores the pointer directly — it does NOT copy. If the string is on the stack and the slot stores or defers the pointer, it becomes a dangling pointer.

**Fix:** For immediate synchronous use, the string must outlive the emission. For deferred use, use `ss_data_create()` + `ss_data_set_string()` (which copies via `SS_STRDUP`) + `ss_emit()`. Note: `ss_emit_deferred()` and `ss_batch_add()` do copy strings internally.

```c
// WRONG — stack buffer may be gone by the time deferred slot reads it
char buf[64];
snprintf(buf, sizeof(buf), "value: %d", x);
ss_emit_string("log", buf);  // OK only if all slots read synchronously and don't store the pointer

// SAFE — use ss_data_set_string for owned copy
ss_data_t* d = ss_data_create(SS_TYPE_STRING);
ss_data_set_string(d, buf);  // Copies via SS_STRDUP
ss_emit("log", d);
ss_data_destroy(d);
```

### 4. Unchecked Error Codes

**Look for:** Calls to `ss_init()`, `ss_connect()`, `ss_emit()`, `ss_signal_register()`, or any `ss_error_t`-returning function where the return value is discarded.

**Consequence:** Silent failures. `ss_init()` failure means everything else will fail. `ss_connect()` failure on static memory means slot limit hit.

**Fix:** Always check at minimum `ss_init()` and `ss_connect()`. Use `ss_error_string()` for human-readable messages.

### 5. Thread Safety Not Enabled at Runtime

**Look for:** Multi-threaded code using SS_Lib without calling `ss_set_thread_safe(1)`.

**Consequence:** Data races on the global context. Thread safety is compiled in by default (`SS_ENABLE_THREAD_SAFETY 1`) but **disabled at runtime** (`g_context->thread_safe = 0`). The mutex is not even initialized until `ss_set_thread_safe(1)` is called.

**Fix:** Call `ss_set_thread_safe(1)` immediately after `ss_init()`, before spawning any threads that use SS_Lib.

```c
ss_init();
ss_set_thread_safe(1);  // Initializes mutex
// NOW safe to use from multiple threads
```

### 6. Leaked `ss_data_t`

**Look for:** `ss_data_create()` without matching `ss_data_destroy()`.

**Consequence:** Memory leak. If the data holds a string (via `ss_data_set_string()`), the duplicated string also leaks. If custom data with a cleanup function, the cleanup never runs.

**Fix:** Always pair `ss_data_create()` with `ss_data_destroy()`. Consider using stack-local `ss_data_t` with the type-specific emit helpers instead.

### 7. Static Memory Overflow

**Look for:** In static memory builds (`SS_USE_STATIC_MEMORY`), registering more than `SS_MAX_SIGNALS` (default 32) signals or connecting more than `SS_MAX_SLOTS` (default 128) total slots.

**Consequence:** Returns `SS_ERR_MAX_SLOTS` or `SS_ERR_MEMORY`. Silently drops connections if return value unchecked.

**Fix:** Size `SS_MAX_SIGNALS` and `SS_MAX_SLOTS` for your application. Check all return values in embedded code.

### 8. ISR with Non-Integer Data

**Look for:** Attempting to pass string, pointer, or custom data from an ISR context.

**Consequence:** `ss_emit_from_isr()` only accepts `int`. There is no ISR-safe variant for other types. Using `ss_emit()` from ISR causes undefined behavior (mutex usage, malloc).

**Fix:** Pass only `int` from ISR. Use the int as an event code, and look up full data in the main loop.

### 9. Missing `ss_cleanup()`

**Look for:** Program exit paths (normal return, error returns, signal handlers) that skip `ss_cleanup()`.

**Consequence:** Memory leaks in dynamic mode. In long-running applications, repeated init/use cycles without cleanup cause unbounded memory growth.

**Fix:** Call `ss_cleanup()` on all exit paths. Consider `atexit(ss_cleanup)` as a safety net.

### 10. Invalid Connection Handles

**Look for:** Using a `ss_connection_t` after the slot was already disconnected, or using handle value 0.

**Consequence:** `ss_disconnect_handle()` returns `SS_ERR_NOT_FOUND`. Handle 0 is never assigned (handles start at 1).

**Fix:** Set handle to 0 after disconnecting. Check return value of `ss_disconnect_handle()`.

### 11. Custom Data Without Cleanup

**Look for:** `ss_data_set_custom()` with heap-allocated data but NULL cleanup function, where the caller also doesn't free the original.

**Consequence:** `ss_data_set_custom()` copies data via `memcpy` into a new allocation. The copy is freed by `ss_data_destroy()`. But if the custom data contains nested pointers to heap memory, those nested allocations are leaked since only the outer copy is freed.

**Fix:** Provide a cleanup function for custom data with nested heap allocations:

```c
void point_list_cleanup(void* data) {
    PointList* pl = (PointList*)data;
    free(pl->points);  // Free nested allocation
}
ss_data_set_custom(d, &pl, sizeof(PointList), point_list_cleanup);
```

### 12. Deferred Queue Overflow

**Look for:** High-frequency deferred emission without matching `ss_flush_deferred()`.

**Consequence:** Returns `SS_ERR_WOULD_OVERFLOW` when queue reaches `SS_DEFERRED_QUEUE_SIZE` (default 64). Emissions silently dropped if unchecked.

**Fix:** Call `ss_flush_deferred()` regularly. Increase `SS_DEFERRED_QUEUE_SIZE` if needed. Always check return value.

### 13. Double `ss_init()` Without Cleanup

**Look for:** Calling `ss_init()` twice without `ss_cleanup()` in between.

**Consequence:** `ss_init()` returns `SS_OK` immediately if already initialized — it does NOT reset state. This is safe but may surprise callers expecting a fresh state.

**Fix:** If you need fresh state, call `ss_cleanup()` then `ss_init()`.

### 14. Priority Change Assumptions

**Look for:** Code that assumes slot priority can be changed after connection.

**Consequence:** Priority is set at `ss_connect()` / `ss_connect_ex()` time and the slot is sorted into the linked list by priority at that point. There is no API to change priority after connection.

**Fix:** Disconnect and reconnect with the new priority:

```c
ss_disconnect_handle(handle);
ss_connect_ex("sig", slot, user_data, SS_PRIORITY_HIGH, &handle);
```

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

// CORRECT — use ss_data_set_string to duplicate
void emit_status(void) {
    char buf[128];
    snprintf(buf, sizeof(buf), "status: %d", get_status());
    ss_data_t* d = ss_data_create(SS_TYPE_STRING);
    ss_data_set_string(d, buf);    // SS_STRDUP copies buf
    ss_emit("status", d);
    ss_data_destroy(d);
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
// WRONG — ss_emit calls mutex lock and may malloc
void EXTI0_IRQHandler(void) {
    ss_emit_int("button", 1);  // NOT ISR-safe!
}

// CORRECT — use ISR-safe variant (int only, lock-free, no malloc)
void EXTI0_IRQHandler(void) {
    ss_emit_from_isr("button", 1);  // Queues to lock-free ring buffer
}
```

## Feature-Flag Awareness

When reviewing code, verify that features used are enabled at compile time:

| API Used | Required Flag | Default |
|----------|--------------|---------|
| `ss_set_thread_safe()`, `ss_is_thread_safe()` | `SS_ENABLE_THREAD_SAFETY` | 1 (on) |
| `ss_get_signal_count()`, `ss_get_signal_list()`, `ss_free_signal_list()` | `SS_ENABLE_INTROSPECTION` | 1 (on) |
| `SS_TYPE_CUSTOM`, `ss_data_set_custom()`, `ss_data_get_custom()` | `SS_ENABLE_CUSTOM_DATA` | 1 (on) |
| `ss_get_perf_stats()`, `ss_enable_profiling()`, `ss_reset_perf_stats()` | `SS_ENABLE_PERFORMANCE_STATS` | 0 (off) |
| `ss_get_memory_stats()`, `ss_reset_memory_stats()` | `SS_ENABLE_MEMORY_STATS` | 0 (off) |
| `ss_enable_trace()`, `ss_disable_trace()` | `SS_ENABLE_DEBUG_TRACE` | 0 (off) |
| `ss_emit_from_isr()` | `SS_ENABLE_ISR_SAFE` | 0 (off) |

Code using disabled features will fail to compile (linker errors for functions, or compile errors for `SS_TYPE_CUSTOM`). The `SS_MINIMAL_BUILD` preset disables thread safety, introspection, custom data, performance stats, and memory stats. The `SS_EMBEDDED_BUILD` preset enables static memory and disables thread safety.

## Memory Model Verification

### Dynamic Mode (default)

- No fixed limits on signal/slot count (bounded by `max_slots_per_signal`, default 100).
- All allocations via `SS_MALLOC`/`SS_CALLOC`/`SS_STRDUP`.
- `ss_cleanup()` must be called to free all resources.
- `ss_data_destroy()` must be called for every `ss_data_create()`.

### Static Mode (`SS_USE_STATIC_MEMORY`)

- Fixed pools: `SS_MAX_SIGNALS` (32), `SS_MAX_SLOTS` (128).
- Signal names limited to `SS_MAX_SIGNAL_NAME_LENGTH` (32 in static mode).
- No heap fragmentation. Predictable memory usage.
- Returns `SS_ERR_MAX_SLOTS` when pools exhausted.
- `ss_cleanup()` zeroes the pools — safe to re-init.

### Custom Allocators

If custom allocators are defined (`SS_MALLOC`, `SS_FREE`, `SS_CALLOC`, `SS_STRDUP`), verify:
- `SS_CALLOC` zeroes memory (SS_Lib depends on this).
- `SS_STRDUP` returns heap memory freeable by `SS_FREE`.
- All four macros are consistent (same allocator family).

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

SS_Lib uses a deferred removal flag (`slot->removed`) to handle disconnection during emission. It is safe to call `ss_disconnect()` from within a slot callback — the slot is marked for removal and cleaned up after the current emission completes. Do not rely on the slot being immediately removed.

### Const Correctness

The `data` parameter is `const ss_data_t*`. Never cast away const to modify the data in a slot — it may be shared across multiple slot invocations for the same emission. Copy data locally if mutation is needed.
