# Skill: Reviewing and Debugging SS_Lib Code

## Trigger

Activate when user asks to review, debug, audit, or fix code that uses SS_Lib (`ss_lib.h`).

## Review Checklist

Work through each item. Report findings with: what's wrong, the consequence, and the fix.

SS_Lib 3.0 keeps all state in a bus (`ss_bus_t*`). Every stateful call takes the bus first; buses share nothing; the library has no default bus.

### 1. Missing or Unchecked Bus Creation

**Look for:** SS_Lib calls on a bus pointer that was never set by `ss_bus_create()` / `ss_bus_create_in()`, or whose creation result is not checked (`ss_bus_t* bus; ss_bus_create(&bus, 0); ss_signal_register(bus, ...)`).

**Consequence:** On failure `*bus` is NULL, and every call on a NULL bus fails: `ss_error_t` functions return `SS_ERR_NULL_PARAM`, others return 0/NULL or do nothing, and no error handler is called (the handler belongs to a bus). An uninitialized pointer variable is undefined behavior. Creation fails with `SS_ERR_CONFIG_MISMATCH` (caller's `SS_ABI_TAG` differs, or `SS_BUS_THREAD_SAFE` in a library without thread support), `SS_ERR_WOULD_OVERFLOW` (static mode: every built-in bus in use), `SS_ERR_BUFFER_TOO_SMALL` / `SS_ERR_NULL_PARAM` (pool), or `SS_ERR_MEMORY`.

**Fix:** Create the bus at startup and check the result before using it:

```c
ss_bus_t* bus = NULL;
ss_error_t err = ss_bus_create(&bus, 0);
if (err != SS_OK) { fprintf(stderr, "bus: %s\n", ss_error_string(err)); return 1; }
```

Flag direct calls to `ss_bus_create_abi()` / `ss_bus_create_in_abi()` with a hand-computed tag in C code: use the macros, which pass `SS_ABI_TAG` (only FFI callers need the `_abi` functions).

### 1b. Bus Used After Destroy

**Look for:** Any use of a bus pointer after `ss_bus_destroy(bus)` returned, including a slot that destroys its bus and the emitter continuing to use it afterwards, a stale global that is not reset, or a batch used after its bus was destroyed.

**Consequence:** Undefined behavior (use after free in dynamic mode; in static mode the built-in bus may already belong to a new owner). While a destroy requested from a slot is pending, the bus refuses every call (`SS_ERR_NULL_PARAM`) and skips the remaining slots; once the outermost call on that bus returns, the bus is gone.

**Fix:** Set the pointer to NULL after destroying (`ss_bus_destroy(NULL)` is a no-op). After an emit that may destroy its bus from a slot, have the slot record it (a flag) and do not touch the bus again.

### 1c. Wrong Bus Passed / Relying on a Default Bus

**Look for:** A signal registered on one bus and connected or emitted on another; connection handles passed to a bus other than the one that issued them; code that assumes signals, namespaces, error handlers, max-slot limits or deferred queues are shared between buses; a helper library that creates its own bus and expects to see the application's signals; slots that call the library without a way to know their bus.

**Consequence:** `SS_ERR_NOT_FOUND`, or the call silently acts on the wrong bus. Handles are per bus and start at 1 on every bus, so a handle passed to the wrong bus may disconnect an unrelated slot there. There is no global or default bus, and a NULL bus never means "the default".

**Fix:** Keep one bus per independent part of the program and pass it explicitly. A slot that needs its bus gets it through `user_data` (`ss_connect(bus, "sig", slot, bus)`, or a struct holding the bus and other state). A program that wants a single bus keeps it in its own `static ss_bus_t* bus;` set at startup.

### 2. Unregistered Signals

**Look for:** `ss_connect()` or `ss_emit*()` on a signal name that was never passed to `ss_signal_register()` on that same bus.

**Consequence:** Returns `SS_ERR_NOT_FOUND`. Slots never fire. Easy to miss if return value is unchecked.

**Fix:** Always register signals on the bus before connecting or emitting there. Check return values.

### 3. `ss_emit_string()` with Temporary Strings

**Look for:** `ss_emit_string()` called with stack-local buffers, `sprintf` results, or strings that are freed/modified after the call.

**Consequence:** `ss_emit_string()` stores the pointer directly — it does NOT copy. If the string is on the stack and the slot stores or defers the pointer, it becomes a dangling pointer.

**Fix:** The string must outlive the emission, and a slot that keeps the string must copy it: the pointer a slot receives is only valid during the slot call, whatever the emitter used (including deferred/batch, whose copies are freed right after each emission). `ss_emit_deferred()` and `ss_batch_add()` copy string payloads internally, so emitters may pass temporary buffers to them.

```c
// WRONG — stack buffer may be gone by the time deferred slot reads it
char buf[64];
snprintf(buf, sizeof(buf), "value: %d", x);
ss_emit_string(bus, "log", buf);  // OK only if all slots read synchronously and don't store the pointer

// SAFE for later delivery — the deferred queue copies the string
ss_emit_deferred(bus, "log", &(ss_data_t){ .type = SS_TYPE_STRING, .value.s_val = buf });  /* C99+ */
// SAFE in the slot — copy what you keep
void logger(const ss_data_t* d, void* ud) { log_append(strdup(ss_data_get_string(d))); }
```

### 4. Unchecked Error Codes

**Look for:** Calls to `ss_bus_create()`, `ss_connect()`, `ss_emit()`, `ss_signal_register()`, or any `ss_error_t`-returning function where the return value is discarded.

**Consequence:** Silent failures. A failed `ss_bus_create()` means everything else fails. `ss_connect()` failure on static memory means slot limit hit.

**Fix:** Always check at minimum `ss_bus_create()` and `ss_connect()`. Use `ss_error_string()` for human-readable messages, or set a per-bus handler with `ss_set_error_handler(bus, handler)`.

### 5. Thread Safety Not Requested at Creation

**Look for:** A bus used from several threads that was created without `SS_BUS_THREAD_SAFE`.

**Consequence:** Data races on that bus. Without the flag the bus has no mutex at all; there is no way to turn locking on later.

**Fix:** Create shared buses with the flag, before spawning the threads that use them:

```c
ss_bus_t* bus = NULL;
if (ss_bus_create(&bus, SS_BUS_THREAD_SAFE) != SS_OK) return 1;
// NOW safe to use from multiple threads
```

Also check:
- `SS_EMBEDDED_BUILD` / `SS_MINIMAL_BUILD` compile thread safety out: the flag then returns `SS_ERR_CONFIG_MISMATCH` (an unchecked creation leaves `bus` NULL).
- `ss_bus_destroy()` racing with other threads using the same bus (not thread-safe): join them first.
- Static mode: built-in buses (`ss_bus_create()`) claimed or released from several threads at once. That table is not locked; create and destroy built-in buses from one thread (normally at startup), or give each thread a pool via `ss_bus_create_in()`.
- Threads that each use only their own bus need no flag (buses share no state); flag it only where a bus is actually shared.

### 5b. Slot Waits for Another Thread (Deadlock)

**Look for:** A slot that joins a thread, waits on a condition variable/semaphore/blocking queue, or otherwise blocks until another thread makes progress, where that other thread calls the same bus.

**Consequence:** Slots run with their bus's mutex held. The other thread blocks on the mutex, the slot blocks on the other thread: deadlock. (Calling back into the same bus from the slot on the same thread is fine; the mutex is recursive.)

**Fix:** Don't block in slots. Hand work to the other thread without waiting, or post with `ss_emit_deferred()` and flush outside the slot.

### 5c. Cross-Bus Calls in Both Directions

**Look for:** Slots of bus A that call bus B while slots of bus B call bus A (directly, or through a chain A → B → C → A), on thread-safe buses used from several threads.

**Consequence:** A slot of A holds A's lock while it takes B's lock. If another thread is in a slot of B calling A at the same moment, each waits for the other's lock: deadlock. It only shows up under load.

**Fix:** Cross-bus calls from slots go in one direction only. Choose an order (e.g. driver bus → application bus). A reverse path must not call the other bus from a slot at all (not even `ss_emit_deferred()`, which also takes that bus's lock): hand the event to an application queue and emit it from normal context, outside any slot (see Anti-Pattern 8).

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

**Look for:** In static memory builds (`SS_USE_STATIC_MEMORY`), more than `SS_MAX_SIGNALS` (default 32) signals or more than `SS_MAX_SLOTS` (default 128) total slots on one bus, descriptions of `SS_MAX_DESCRIPTION_LENGTH` (32 incl. NUL) or more, names of `SS_MAX_SIGNAL_NAME_LENGTH` (32 incl. NUL, including any `"ns::"` prefix) or more, deferred/batch string or custom payloads larger than `SS_DEFERRED_PAYLOAD_SIZE` (32) bytes, more simultaneous `ss_bus_create()` buses than `SS_STATIC_BUSES` (default 1).

**Consequence:** Returns `SS_ERR_WOULD_OVERFLOW` (nothing is truncated; the extra bus is not created and `*bus` is NULL). `SS_ERR_MAX_SLOTS` means the per-signal limit (`max_slots_per_signal`, 10 with `SS_EMBEDDED_BUILD`), counted over live slots. Silently drops registrations/connections/buses if return values are unchecked. Every bus carries its own arrays and queue, so each extra bus costs `ss_bus_pool_size()` bytes of RAM.

**Fix:** Size the limits for your application (on the command line, for library and app alike); raise `SS_STATIC_BUSES` or place extra buses in pools with `ss_bus_create_in()`. Check all return values in embedded code. Also flag uses of `ss_data_create`, `ss_data_set_string/custom`, `ss_get_signal_list`, `ss_batch_create` on targets that must not use the heap: these allocate even in static mode.

### 8. ISR with Non-Integer Data

**Look for:** Attempting to pass string, pointer, or custom data from an ISR context.

**Consequence:** `ss_emit_from_isr()` only accepts `int`. There is no ISR-safe variant for other types. Using `ss_emit()` from ISR causes undefined behavior (mutex usage, malloc).

**Fix:** Pass only `int` from ISR. Use the int as an event code, and look up full data in the main loop.

### 8b. ISR Events Never Delivered (or Delivered to the Wrong Bus)

**Look for:** `ss_emit_from_isr()` without a regular `ss_process_isr_queue(bus)` call in normal context (main loop, task); programs with several buses where more than one processes the ISR queue.

**Consequence:** Events stay queued; once `SS_ISR_QUEUE_SIZE` (16) entries are pending, `ss_emit_from_isr()` returns `SS_ERR_WOULD_OVERFLOW` and events are lost. The ISR queue is a single queue shared by every bus: `ss_emit_from_isr()` takes no bus, and each event goes to whichever bus next calls `ss_process_isr_queue()`. Destroying a bus does not drop queued events; the next bus that processes the queue receives them.

**Fix:** Call `ss_process_isr_queue(bus)` from the main loop, on the one bus that owns the ISR signals. Also check: it is never called from an ISR (it may take the bus mutex and runs slots); on cores without compare-and-swap (Cortex-M0, AVR) `SS_ISR_CRITICAL_ENTER()`/`SS_ISR_CRITICAL_EXIT()` are defined, otherwise the build fails or uses the wrong atomics; names are resolved against that bus's default namespace at delivery, not at enqueue.

### 9. Missing `ss_bus_destroy()`

**Look for:** Program exit paths (normal return, error returns) and long-lived components that create a bus and never destroy it; repeated create/use cycles without destroy.

**Consequence:** Memory leaks in dynamic mode (the bus itself plus its signals, slots and queued payloads); in static mode the built-in bus stays claimed, so later `ss_bus_create()` calls return `SS_ERR_WOULD_OVERFLOW`.

**Fix:** Call `ss_bus_destroy(bus)` on all exit paths, after destroying the batches created for it. For a single global bus, a wrapper `static void quit(void) { ss_bus_destroy(bus); bus = NULL; }` can serve as an `atexit` handler.

### 10. Invalid Connection Handles

**Look for:** Using a `ss_connection_t` after the slot was already disconnected, using handle value 0, or passing a handle to a bus other than the one that issued it.

**Consequence:** `ss_disconnect_handle()` returns `SS_ERR_NOT_FOUND`. Handle 0 is never assigned (handles start at 1). Handles are numbered per bus, so on another bus the same value may name an unrelated connection and disconnect it.

**Fix:** Keep the handle with its bus, set it to 0 after disconnecting, and check the return value of `ss_disconnect_handle()`.

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

**Look for:** High-frequency deferred emission without matching `ss_flush_deferred(bus)` on the same bus.

**Consequence:** Returns `SS_ERR_WOULD_OVERFLOW` when the bus's queue holds `SS_DEFERRED_QUEUE_SIZE` entries (default 64, 16 with `SS_EMBEDDED_BUILD`), and also for too-long names or (static mode) payloads over `SS_DEFERRED_PAYLOAD_SIZE`. Emissions silently dropped if unchecked. Each bus has its own queue: flushing one bus does not deliver another bus's entries. Entries queued by slots during a flush wait for the next flush, so a slot that re-queues itself does not loop but does keep the queue occupied.

**Fix:** Call `ss_flush_deferred(bus)` regularly on every bus that defers. Increase `SS_DEFERRED_QUEUE_SIZE` (or `SS_BATCH_MAX_ENTRIES` for batches) if needed. Always check return value.

### 13. Batch Outliving Its Bus

**Look for:** A batch created with `ss_batch_create(bus)` that is still added to or emitted after `ss_bus_destroy(bus)`; batches stored in long-lived objects whose bus is torn down first; code expecting a batch to emit on a different bus or with another bus's namespace.

**Consequence:** A batch belongs to the bus it was created for: that bus supplies the namespace when entries are added, runs the emission and reports errors. `ss_batch_add()` / `ss_batch_emit()` after the bus is gone are use after free. (`ss_batch_destroy()` itself does not touch the bus, so destroying a batch whose bus was destroyed from one of its slots is fine.)

**Fix:** Destroy batches before their bus. Create one batch per bus when emitting on several.

### 14. Priority Change Assumptions

**Look for:** Code that assumes slot priority can be changed after connection.

**Consequence:** Priority is set at `ss_connect()` / `ss_connect_ex()` time and the slot is sorted into the linked list by priority at that point. There is no API to change priority after connection.

**Fix:** Disconnect and reconnect with the new priority:

```c
ss_disconnect_handle(bus, handle);
ss_connect_ex(bus, "sig", slot, user_data, SS_PRIORITY_HIGH, &handle);
```

### 15. Configuration Mismatch Between Library and Caller

**Look for:** `#define SS_ENABLE_*`, `SS_USE_STATIC_MEMORY`, `SS_MINIMAL_BUILD`/`SS_EMBEDDED_BUILD` or limit macros (including `SS_STATIC_BUSES`) in a source file before `#include "ss_lib.h"`; build files where the library and the application get different `-D` flags; an unchecked `ss_bus_create()` result.

**Consequence:** Since 3.0 no flag changes a public type layout or enum value, so a feature-flag mismatch shows up as a compile or link error (a function the library lacks) rather than memory corruption. Limits (`SS_MAX_SIGNAL_NAME_LENGTH`, queue sizes) and the memory mode still differ silently, so the caller's assumptions (e.g. name length limits) are wrong. The `ss_bus_create` / `ss_bus_create_in` macros pass the caller's `SS_ABI_TAG` and return `SS_ERR_CONFIG_MISMATCH` when its `sizeof(ss_data_t)` or `SS_ABI_REVISION` differs (a header from another version, `-fshort-enums`, packing), or when the caller asks for `SS_BUS_THREAD_SAFE` from a library built without thread support. A library built without custom data returns `SS_ERR_INVALID_TYPE` when `SS_TYPE_CUSTOM` data is queued.

**Fix:** Pass every option on the command line, identically for the library and all callers (CMake exports them as PUBLIC compile definitions of `ss_lib::ss_lib`). Create buses with the macros and check the result.

### 16. Default Namespace Surprises

**Look for:** `ss_set_namespace(bus, ...)` set while code uses unqualified names meant for the global scope, signals registered under one namespace and emitted/connected under another (or none), or code expecting a namespace set on one bus to apply to another.

**Consequence:** The default namespace is per bus. While set, every name without `"::"` is resolved as `"ns::name"` by all name-taking functions on that bus (register, unregister, exists, connect, disconnect, emit*, deferred, batch_add for its batches, perf stats; ISR events at delivery). A mismatch gives `SS_ERR_NOT_FOUND`. `ss_emit_namespaced()` ignores the default namespace.

**Fix:** Use fully qualified names (`"ui::click"`) across namespace boundaries, or keep the same namespace set for registration and use. Clear it with `ss_set_namespace(bus, NULL)`. Check for `SS_ERR_WOULD_OVERFLOW` when the qualified name is too long. Consider separate buses instead of namespaces for parts that never talk to each other.

### 17. Assumptions About Connecting During Emission

**Look for:** A slot that connects a new slot to the signal being emitted and expects it to run (or not run) in the same emission.

**Consequence:** Unspecified: depending on where the new slot's priority places it, the current emission may or may not call it.

**Fix:** Don't rely on either outcome. If the new slot must not see the current event, have it ignore the first call or connect it via a deferred step.

### 18. Expecting Trace Output by Default

**Look for:** Code built with `SS_ENABLE_DEBUG_TRACE` that expects trace on stderr without calling `ss_enable_trace()`.

**Consequence:** Each bus's trace is silent until `ss_enable_trace(bus, stream)` names a stream.

**Fix:** Call `ss_enable_trace(bus, stderr)` (or another stream) after creating the bus; `ss_disable_trace(bus)` stops it. These functions are the only trace interface; 3.0 has no trace macro.

### 19. Migrating 2.x Code

**Look for:** 2.x calls that 3.0 no longer has (the global init and cleanup functions, the pool-size query, the runtime thread-safety toggle, the static-context macro, the trace macro and the two error codes 2.x never returned; `docs/migration-3.0.md` lists them by name), stateful calls without a bus argument, and `ss_get_namespace` used as returning a pointer (it now copies into a caller buffer and returns `ss_error_t`).

**Consequence:** They fail to compile or link; none silently changes meaning.

**Fix:** Follow `docs/migration-3.0.md`: create a bus (`ss_bus_create(&bus, flags)`, or `ss_bus_create_in(&bus, pool, size, flags)` for a pool, sized with `ss_bus_pool_size()`), pass it first to every stateful call, choose thread safety with `SS_BUS_THREAD_SAFE`, destroy it with `ss_bus_destroy(bus)`, and use `SS_STATIC_BUSES` in static mode. Note that a too-small pool is now an error (`SS_ERR_BUFFER_TOO_SMALL`), not a silent fallback. Replace `const char* ns = ss_get_namespace()` with `char ns[SS_MAX_SIGNAL_NAME_LENGTH]; ss_get_namespace(bus, ns, sizeof(ns))`, and a NULL check on it with `ns[0] == '\0'`.

### No Longer Issues

Do not report these as bugs:
- Calling `ss_disconnect*`, `ss_signal_unregister`, `ss_bus_destroy` (on the slot's own bus), `ss_emit*`, `ss_emit_deferred`, `ss_flush_deferred` or batch functions from inside a slot, including on the signal being emitted.
- Calling the same bus from a slot of a thread-safe bus (the mutex is recursive).
- Calling another bus from a slot, as long as cross-bus calls go in one direction (check 5c).
- Freeing or reusing the emitter's buffer right after `ss_emit_deferred()` / `ss_batch_add()`.
- Calling several `ss_data_set_*` on the same created object (previous payload is released).
- Overriding only `SS_MALLOC`/`SS_FREE`/`SS_CALLOC` (strings and dynamic-mode buses are allocated with them).
- Using the same signal names on several buses (they are independent signals).

## Anti-Patterns with Corrections

### Anti-Pattern 1: Emitting to unregistered signal

```c
// WRONG
ss_connect(bus, "click", on_click, NULL);  // Returns SS_ERR_NOT_FOUND — ignored!
ss_emit_void(bus, "click");                // Returns SS_ERR_NOT_FOUND — nothing happens

// CORRECT
ss_signal_register(bus, "click");          // Register first
ss_connect(bus, "click", on_click, NULL);
ss_emit_void(bus, "click");
```

### Anti-Pattern 2: Sharing a bus between threads without the flag

```c
// WRONG
ss_bus_create(&bus, 0);                         // no mutex: race condition
pthread_create(&t1, NULL, producer, bus);
pthread_create(&t2, NULL, consumer, bus);

// CORRECT
if (ss_bus_create(&bus, SS_BUS_THREAD_SAFE) != SS_OK) return 1;   // before threads
pthread_create(&t1, NULL, producer, bus);
pthread_create(&t2, NULL, consumer, bus);
```

### Anti-Pattern 3: Stack string with ss_emit_string where slot stores the pointer

```c
// WRONG — slot stores data->value.s_val, but it points to stack memory
void logger(const ss_data_t* data, void* ud) {
    log_list_append(ss_data_get_string(data));  // Stores dangling pointer!
}
void emit_status(ss_bus_t* bus) {
    char buf[128];
    snprintf(buf, sizeof(buf), "status: %d", get_status());
    ss_emit_string(bus, "status", buf);  // buf is on the stack
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
void send_update(ss_bus_t* bus, int value) {
    ss_data_t* d = ss_data_create(SS_TYPE_INT);
    ss_data_set_int(d, value);
    ss_emit(bus, "update", d);
    // Missing ss_data_destroy(d)!
}

// CORRECT
void send_update(ss_bus_t* bus, int value) {
    ss_emit_int(bus, "update", value);  // No allocation needed
}

// Or if you need ss_data_create:
void send_update(ss_bus_t* bus, int value) {
    ss_data_t* d = ss_data_create(SS_TYPE_INT);
    ss_data_set_int(d, value);
    ss_emit(bus, "update", d);
    ss_data_destroy(d);  // Always destroy
}
```

### Anti-Pattern 5: ISR using full emit API

```c
// WRONG — ss_emit may take the mutex and runs every slot inside the ISR
void EXTI0_IRQHandler(void) {
    ss_emit_int(bus, "button", 1);  // NOT ISR-safe!
}

// CORRECT — use ISR-safe variant (int only, lock-free, no malloc, no bus)
void EXTI0_IRQHandler(void) {
    ss_emit_from_isr("button", 1);  // Queues to the fixed ISR queue
}
// ...and deliver from the main loop
for (;;) { ss_process_isr_queue(bus); /* ... */ }
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

### Anti-Pattern 7: Slot with no way to reach its bus

```c
// WRONG — relies on a bus the slot cannot see (or on a "default" bus)
void on_saved(const ss_data_t* d, void* ud) {
    ss_emit_void(NULL, "status");     // NULL is never a default bus: SS_ERR_NULL_PARAM
}

// CORRECT — pass the bus through user_data
void on_saved(const ss_data_t* d, void* ud) {
    ss_bus_t* bus = (ss_bus_t*)ud;
    (void)d;
    ss_emit_void(bus, "status");
}
ss_connect(bus, "saved", on_saved, bus);
```

### Anti-Pattern 8: Cross-bus calls in both directions

```c
// WRONG — thread 1 in a slot of A calls B while thread 2 in a slot of B calls A: deadlock
void a_to_b(const ss_data_t* d, void* ud) { ss_emit(bus_b, "from_a", d); (void)ud; }
void b_to_a(const ss_data_t* d, void* ud) { ss_emit(bus_a, "from_b", d); (void)ud; }

// CORRECT — slots call in one direction only (A -> B). The reverse path does not
// touch bus A from a slot of B: it queues in an application structure that the
// main loop drains outside any slot.
void b_to_a(const ss_data_t* d, void* ud) { app_queue_push(&to_a, ss_data_get_int(d, 0)); (void)ud; }
/* main loop, not in a slot: */
while (app_queue_pop(&to_a, &v)) ss_emit_int(bus_a, "from_b", v);
```

Note: `ss_emit_deferred(bus_a, ...)` from a slot of B is not a fix: it still takes A's lock while B's is held, which is exactly the lock order that deadlocks.

## Feature-Flag Awareness

When reviewing code, verify that features used are enabled at compile time:

| API Used | Required Flag | Default |
|----------|--------------|---------|
| `SS_BUS_THREAD_SAFE` (creation flag) | `SS_ENABLE_THREAD_SAFETY` (else `SS_ERR_CONFIG_MISMATCH` at runtime) | 1 (on) |
| `ss_get_signal_count()`, `ss_get_signal_list()`, `ss_free_signal_list()` | `SS_ENABLE_INTROSPECTION` | 1 (on) |
| `ss_data_set_custom()`, `ss_data_set_custom_ref()`, `ss_data_get_custom()`, queuing `SS_TYPE_CUSTOM` data | `SS_ENABLE_CUSTOM_DATA` | 1 (on) |
| `ss_get_perf_stats()`, `ss_enable_profiling()`, `ss_reset_perf_stats()` | `SS_ENABLE_PERFORMANCE_STATS` | 0 (off) |
| `ss_get_memory_stats()`, `ss_reset_memory_stats()` | `SS_ENABLE_MEMORY_STATS` | 0 (off) |
| `ss_enable_trace()`, `ss_disable_trace()` | `SS_ENABLE_DEBUG_TRACE` | 0 (off) |
| `ss_emit_from_isr()`, `ss_process_isr_queue()` | `SS_ENABLE_ISR_SAFE` | 0 (off) |

Code using disabled features will fail to compile or link, except `SS_BUS_THREAD_SAFE`, which is always defined and refused at bus creation. `SS_TYPE_CUSTOM` and `value.custom` exist in every configuration; without custom data, `ss_emit_deferred()` and `ss_batch_add()` return `SS_ERR_INVALID_TYPE` for it. The `SS_MINIMAL_BUILD` preset disables thread safety, introspection, custom data, performance stats, and memory stats. The `SS_EMBEDDED_BUILD` preset enables static memory, disables thread safety, and sets max slots per signal to 10 and the deferred queue to 16. Flags must be identical for the library and the application (see check 15).

## Memory Model Verification

### Dynamic Mode (default)

- No fixed limits on signal/slot count (bounded by each bus's `max_slots_per_signal`, default 100) or on the number of buses.
- All allocations via `SS_MALLOC`/`SS_CALLOC` (strings via `SS_MALLOC` unless `SS_STRDUP` is defined); `ss_bus_create()` allocates the bus with `SS_CALLOC`.
- `ss_bus_destroy()` must be called for every bus to free all resources.
- `ss_data_destroy()` must be called for every `ss_data_create()`, and `ss_data_clear()` for every stack object filled by a copying setter.

### Static Mode (`SS_USE_STATIC_MEMORY`)

- Per bus, fixed pools: `SS_MAX_SIGNALS` (32), `SS_MAX_SLOTS` (128); fixed buffers: `SS_MAX_SIGNAL_NAME_LENGTH` (32), `SS_MAX_DESCRIPTION_LENGTH` (32), `SS_DEFERRED_PAYLOAD_SIZE` (32), `SS_DEFERRED_QUEUE_SIZE` entries. Buses do not share pools: one bus cannot starve another.
- `ss_bus_create()` takes one of `SS_STATIC_BUSES` (default 1) built-in buses in `.bss` and returns `SS_ERR_WOULD_OVERFLOW` when all are in use; it never uses the heap. Register/connect/emit/deferred/ISR delivery/namespace/disconnect/unregister/destroy never touch the heap on any bus.
- Still allocate: `ss_data_create`, `ss_data_set_string`, `ss_data_set_custom`, `ss_get_signal_list`, `ss_batch_create`. `ss_data_set_string_ref` / `ss_data_set_custom_ref` never allocate.
- Returns `SS_ERR_WOULD_OVERFLOW` when pools or buffers are exhausted.
- `ss_bus_destroy()` releases a built-in bus for reuse.
- `ss_bus_create_in(&bus, pool, size, flags)` (either mode): the pool needs `ss_bus_pool_size()` bytes (any alignment) and must outlive the bus; the library never frees it. A NULL pool returns `SS_ERR_NULL_PARAM`, a smaller one `SS_ERR_BUFFER_TOO_SMALL`; there is no fallback. Flag code that ignores that result, sizes a pool with a compile-time guess without checking, or frees/reuses the pool while the bus is alive.
- Built-in buses are claimed without a lock: flag `ss_bus_create()` / `ss_bus_destroy()` of built-in buses from several threads at once.

### Custom Allocators

If custom allocators are defined (`SS_MALLOC`, `SS_FREE`, `SS_CALLOC`, `SS_STRDUP`), verify:
- `SS_CALLOC` zeroes memory (SS_Lib depends on this).
- If `SS_STRDUP` is defined, it returns memory freeable by `SS_FREE`.
- All macros are consistent (same allocator family), and the library itself was built with them.

## Correct Slot Callback Patterns

### Type-Safe Data Access

```c
void my_slot(const ss_data_t* data, void* user_data) {
    if (!data) return;  // ss_emit(bus, name, NULL) passes NULL

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
- `ss_disconnect*()` any slot of the bus, including itself: a disconnected slot is not called again in that emission.
- `ss_signal_unregister()` the signal being emitted: remaining slots of that emission are skipped, the name can be re-registered at once.
- `ss_bus_destroy()` its own bus: deferred until the bus's outermost call returns; meanwhile calls on that bus fail with `SS_ERR_NULL_PARAM` and remaining slots are skipped. Other buses are unaffected. After that outermost call returns, nobody may use the bus.

Do not rely on a slot connected during the emission being called or skipped (see check 17).

### Const Correctness

The `data` parameter is `const ss_data_t*`. Never cast away const to modify the data in a slot — it may be shared across multiple slot invocations for the same emission. Copy data locally if mutation is needed.
