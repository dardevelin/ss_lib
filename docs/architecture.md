# Architecture

This document describes the internal design and data structures of SS_Lib 2.2.1. The source of truth is `src/ss_lib.c`; its header comment summarizes the same structure.

## Overview

SS_Lib is a signal-slot event system with two memory models (dynamic and static), optional thread safety, and a layered feature set controlled by compile-time flags.

```
+------------------+
|  Public API      |  ss_emit, ss_connect, ss_signal_register, ...
+------------------+
|  Entry / Exit    |  ctx_enter / ctx_leave: lock, nesting depth, deferred cleanup, errors
+------------------+
|  Feature Layer   |  Namespaces, Deferred, Batch, ISR queue, Stats, Trace
+------------------+
|  Core Engine     |  Name resolution, signal lookup, slot iteration, sweep
+------------------+
|  Storage         |  Dynamic (heap lists) or Static (fixed arrays), one interface
+------------------+
```

## Library Context

All state lives in one `ss_context_t`, reached through the global pointer `g_context`. The context holds the signal storage, the default namespace (`char ns[SS_MAX_SIGNAL_NAME_LENGTH]`, an inline buffer), the error handler, the handle counter, the mutex, the deferred queue entries, and the nesting counters `call_depth` and `closing`.

Where the context lives depends on how the library was initialized:

| Initialization | Context location | Freed by `ss_cleanup()` |
|----------------|------------------|-------------------------|
| `ss_init()`, dynamic mode | Heap (`SS_CALLOC`) | Yes (`heap_owned`) |
| `ss_init()`, `SS_USE_STATIC_MEMORY`, `SS_STATIC_CONTEXT=1` (default) | Static storage (`g_static_context`, in `.bss`) | No |
| `ss_init()`, `SS_USE_STATIC_MEMORY`, `SS_STATIC_CONTEXT=0` | Heap (`SS_CALLOC`), as in 2.1 | Yes |
| `ss_init_static(pool, size)`, either mode, pool large enough | Caller's pool, aligned inside it | No, the pool is never freed |

`ss_get_required_pool_size()` returns `sizeof(ss_context_t)` plus alignment slack, so the pool needs no particular alignment. `ss_init_static` only places the context; signals and slots still follow the compiled memory model. For 2.1 compatibility, a NULL pool or one smaller than that size is ignored and the call behaves exactly like `ss_init()`; it never returns `SS_ERR_NULL_PARAM` or `SS_ERR_BUFFER_TOO_SMALL`. `SS_STATIC_CONTEXT=0` drops the built-in `.bss` context, which saves a whole context of RAM when the application always supplies its own pool.

`ss_init()` and `ss_init_static()` are plain functions. The configuration check is opt-in: `ss_init_abi(SS_ABI_TAG)` / `ss_init_static_abi(pool, size, SS_ABI_TAG)` compare `SS_ABI_TAG` (`sizeof(ss_data_t)` as the caller's compilation sees it) with the library's value and return `SS_ERR_CONFIG_MISMATCH` on a difference, before initializing. New code should initialize through them; `ss_init()` and `ss_init_static()` simply pass the library's own tag.

## Entry and Exit (`ctx_enter` / `ctx_leave`)

Every public function that touches shared state is wrapped in `ctx_enter()` / `ctx_leave()`. Locking, nesting, deferred cleanup and error reporting live only there.

`ctx_enter()`:
1. Returns NULL if there is no context (the caller returns `SS_ERR_NULL_PARAM`, 0 or NULL)
2. Locks the mutex if `thread_safe` is set, and records whether it did
3. Returns NULL if `closing` is set (a deferred `ss_cleanup()` is pending)
4. Increments `call_depth`

`ctx_leave()`:
1. Decrements `call_depth`; if it reached 0 and `closing` is set, the context must be destroyed
2. Unlocks the mutex if this call locked it
3. After unlocking: destroys the context (deferred cleanup) or, if the call failed, invokes the error handler with the signal name or `ss_error_string(err)`

The handler fires for every failing call while the library is initialized, including parameter errors (NULL arguments, handle 0, a NULL or destroyed batch, too-long names). Calling it after unlocking means a handler may itself call the library.

## Name Resolution

Every name-taking function resolves the name before using it:

- A NULL or empty name is `SS_ERR_NULL_PARAM`.
- If a default namespace is set and the name contains no `"::"`, it is rewritten to `"ns::name"` in a stack buffer of `SS_MAX_SIGNAL_NAME_LENGTH` bytes.
- Names (after qualification) that do not fit `SS_MAX_SIGNAL_NAME_LENGTH` are rejected with `SS_ERR_WOULD_OVERFLOW` by register/deferred/batch, and reported as `SS_ERR_NOT_FOUND` by lookups (such a signal cannot exist).

When no namespace is set, the emit path skips the qualification step entirely.

## Signal Registry

There is no hash table. Lookup (`signal_find`) is a linear scan comparing names with `strcmp`, skipping retired signals.

### Dynamic Mode

Signals are a singly-linked list of heap nodes, each with a heap copy of its name (and description when `SS_ENABLE_INTROSPECTION` is on). New signals are prepended.

```
g_context->signals -> [sig3] -> [sig2] -> [sig1] -> NULL
```

### Static Mode

Signals are a fixed array `signals[SS_MAX_SIGNALS]` inside the context. Names and descriptions are inline buffers (`name[SS_MAX_SIGNAL_NAME_LENGTH]`, `description[SS_MAX_DESCRIPTION_LENGTH]`). A free entry has `name[0] == '\0'`; registration takes the first free entry and returns `SS_ERR_WOULD_OVERFLOW` when none is left or the description is too long.

```
signals[0]: name="button_click"
signals[1]: name=""            (free)
signals[2]: name="data_ready"
...
```

### Storage Interface

Both modes implement the same small interface, so the core engine has no memory-mode branches: `signal_create`, `signal_next` (iteration), `signal_release_storage`, `slot_alloc`, `slot_release`. `signal_destroy` releases a signal's slots and then its storage.

## Slot Lists

Each signal has a singly-linked list of slots sorted by priority at connection time: higher priority first, equal priorities in connection order.

```
sig->slots -> [CRITICAL] -> [HIGH] -> [NORMAL] -> [NORMAL] -> [LOW] -> NULL
```

`ss_connect_ex` prepends when the list is empty or the new priority is higher than the head; otherwise it walks while `next->priority >= priority` and inserts there. Emission therefore never sorts.

In dynamic mode each slot is a heap node; in static mode it comes from `slots[SS_MAX_SLOTS]` in the context, where a free entry has `func == NULL`.

`sig->slot_count` counts live slots only (removed ones are excluded), so the per-signal limit (`max_slots_per_signal`) applies to live slots.

### Connection Handles

Each slot gets a handle from `g_context->next_handle`, starting at 1 and skipping 0 on wrap-around (0 means "no handle"). `ss_disconnect_handle` scans the slots of all live signals for it.

## Removal and Safe Emission

Slots and signals are never freed while their signal is emitting. They are marked and released when the outermost emission of that signal ends.

- **Disconnect**: `slot->removed = 1`, `sig->slot_count--`, `sig->dirty = 1`. If the signal is not emitting, `sweep_removed_slots()` unlinks and frees the marked slots at once.
- **Unregister**: all slots are marked removed and the signal leaves `signal_count`. If it is emitting, `sig->retired = 1`; retired signals are invisible to lookup (so the name can be registered again immediately) and are destroyed at the end of their outermost emission.
- **Cleanup**: see below; it marks every slot of every signal removed.

The emit loop (`emit_locked`):

```c
sig->emitting++;
for (slot = sig->slots; slot; ) {
    ss_slot_t* next = slot->next;      /* nodes are only marked while emitting */
    if (!slot->removed) slot->func(data, slot->user_data);
    slot = next;
}
sig->emitting--;
if (sig->emitting == 0) {
    if (sig->retired) signal_destroy(ctx, sig);
    else if (sig->dirty) sweep_removed_slots(ctx, sig);
}
```

Consequences:
- A slot disconnected by an earlier slot is skipped; nothing is freed under the iterator.
- Unregistering the signal or calling `ss_cleanup()` from a slot marks every slot removed, so the rest of that emission is skipped.
- A slot connected during an emission of the same signal is inserted into the live list. Whether that emission reaches it depends on where its priority places it relative to the iterator, so callers must not rely on either outcome.

## Deferred Cleanup

`ss_cleanup()` sets `closing = 1` and marks all slots removed. If no call is in progress, `ctx_leave()` destroys the context immediately. Called from inside a slot, `call_depth` is still above zero, so destruction waits until the outermost library call returns. Meanwhile `ctx_enter()` refuses every call (the library behaves as uninitialized), queue flushes and ISR drains stop early, and `ss_init()` returns `SS_ERR_ALREADY_EXISTS`.

`context_destroy()` clears `g_context`, releases pending deferred entries, destroys all signals, discards ready ISR events, destroys the mutex and frees the context only if it is heap-owned.

## Entry Queue (Deferred Emission and Batches)

The deferred queue and batches share one implementation: `ss_queue_t`, a ring buffer (`entries`, `capacity`, `head`, `count`) of `ss_entry_t`.

```c
typedef struct {
    char signal_name[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_data_t data;              /* type and scalar value; pointers cleared */
    int has_payload;
    /* static: inline bytes[SS_DEFERRED_PAYLOAD_SIZE]; dynamic: heap copy */
} ss_entry_t;
```

- `queue_push` copies the resolved name and the data. String and custom payloads are copied (static mode: into the inline buffer, `SS_ERR_WOULD_OVERFLOW` if larger than `SS_DEFERRED_PAYLOAD_SIZE`; dynamic mode: `SS_MALLOC`). `custom_cleanup` is cleared in the entry: it belongs to the caller's object and is never called for the copy.
- Every entry gets a push sequence number from the queue's `pushed` counter. `queue_flush` records `pushed` when it starts and stops at the first entry pushed after that (wrap-safe comparison), so it emits only the entries queued before it started, oldest first, even when a slot runs a nested flush of the same queue. Each entry is **popped before it is emitted**, so slots may push to, or flush, the same queue; entries pushed meanwhile wait for the next flush. The slot sees an `ss_data_t` rebuilt by `entry_view` that points into the entry's payload copy, released right after the emission.
- Flushing stops early if the context is closing (or, for batches, if the batch was destroyed by a slot).

The deferred queue is `deferred_entries[SS_DEFERRED_QUEUE_SIZE]` inside the context. A batch (`struct ss_batch`) is a heap object with `entries[SS_BATCH_MAX_ENTRIES]`, its queue, and `emitting` / `destroy_pending` counters: `ss_batch_destroy` from a slot only sets `destroy_pending`, and `ss_batch_emit` frees the batch when it returns. Batches are not locked against concurrent use of the same batch.

## ISR Queue

With `SS_ENABLE_ISR_SAFE`, `ss_emit_from_isr` writes into a fixed global array `g_isr_queue[SS_ISR_QUEUE_SIZE]`, independent of the context (so it works before `ss_init`). Each entry has an atomic `state`, an atomic sequence number `seq`, the int value and the name.

Entry states: `FREE -> WRITING -> READY -> READING -> FREE`.

`ss_emit_from_isr`:
1. Rejects NULL/empty names and names of `SS_MAX_SIGNAL_NAME_LENGTH` or more
2. Claims the first entry with compare-and-swap `FREE -> WRITING`; none free is `SS_ERR_WOULD_OVERFLOW`
3. Copies name and value, stores the next global sequence number
4. Publishes with a release store `-> READY`

No locks, no allocation, no slot calls. Because claiming is a CAS, nested interrupts and several threads may enqueue concurrently.

`ss_process_isr_queue` (normal context, through `ctx_enter`):
1. Snapshots the READY entries and sorts them by sequence number (wrap-around safe). An interrupt during the scan can queue one event in an entry already scanned and a newer one in an entry not yet scanned, so it scans again until a scan finds no new entry (at most `SS_ISR_QUEUE_SIZE` extra scans); READY entries stay READY until taken, so the snapshot then holds every event older than its newest
2. For each, claims it `READY -> READING`, copies name and value, frees it (`-> FREE`), resolves the name against the default namespace and emits it

Events queued after the snapshot wait for the next call. `ss_cleanup` discards READY entries.

Atomics come from C11 `<stdatomic.h>`, GCC/Clang `__atomic` builtins, or MSVC `Interlocked*`. If `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` are defined, they take precedence and wrap plain accesses, for cores without compare-and-swap (Cortex-M0, AVR); they must also be compiler memory barriers. `tests/test_isr.c` compiles the implementation with hooked macros to interrupt `ss_process_isr_queue` at every point of its scan.

## Namespace Implementation

Namespaces are syntactic: there is no namespace registry. `ss_set_namespace` copies the namespace into the context's inline `ns` buffer (`NULL` or `""` clears it; a namespace too long for even `"ns::x"` is `SS_ERR_WOULD_OVERFLOW`). Passing the pointer returned by `ss_get_namespace()` (the buffer itself) is detected and is a no-op. Name resolution (above) applies it to every name-taking function. `ss_emit_namespaced(ns, name, data)` builds `"ns::name"` itself and ignores the default namespace.

## Thread Safety

With `SS_ENABLE_THREAD_SAFETY=1`, `ss_init` creates one **recursive** mutex in the context (pthread mutex with `PTHREAD_MUTEX_RECURSIVE`, or a Windows `CRITICAL_SECTION`). Locking is off until `ss_set_thread_safe(1)`, which only flips a flag; `ctx_enter` records per call whether it locked, so the setting can change without unbalancing an in-flight call (it still must only change while no other thread uses the library).

- Every public function that reads or writes shared state locks, including the deferred queue, namespace, error handler, profiling flag and max-slots setting.
- Slots run with the mutex held. Recursion lets a slot call back into the library on the same thread; a slot that waits for another thread that is itself calling the library deadlocks.
- `ss_init` / `ss_cleanup` are not thread-safe with respect to other threads.
- The ISR enqueue path never locks.

The single-mutex design was chosen over fine-grained locking for simplicity, small code size and the absence of lock-ordering issues. The trade-off is that concurrent emission of different signals contends on the same lock, which suits the target use cases (embedded, game loops) where emission is mostly single-threaded.

## Memory Modes and Allocation

| Object | Dynamic mode | Static mode |
|--------|--------------|-------------|
| Context | Heap (or caller pool) | `.bss`, or heap with `SS_STATIC_CONTEXT=0` (or caller pool) |
| Signals, names, descriptions | Heap | Context arrays |
| Slots | Heap | Context array |
| Namespace | Context buffer | Context buffer |
| Deferred entries and payloads | Context array, heap payload copies | Context array, inline payloads |
| `ss_data_create` / `ss_data_set_string` / `ss_data_set_custom` | Heap | Heap |
| `ss_get_signal_list` result, `ss_batch_create` | Heap | Heap |

In static mode (with the default `SS_STATIC_CONTEXT=1` or a caller pool), register, connect, emit, deferred, namespace, disconnect, unregister and cleanup never touch the heap. Only functions whose purpose is to hand the caller an allocated object allocate.

## Custom Allocators

All allocation goes through `SS_MALLOC`, `SS_FREE` and `SS_CALLOC` from `ss_config.h`. Strings are copied with an internal `ss_strdup` that uses `SS_MALLOC`, so overriding `SS_MALLOC`/`SS_FREE` routes every allocation; `SS_STRDUP` can be defined to override string duplication specifically. With `SS_ENABLE_MEMORY_STATS` in dynamic mode, the library accounts the heap bytes it holds (context, signals, slots, strings).

## Error Reporting

Public functions return `ss_error_t`. Failures of calls made while the library is initialized are also reported to the user error handler from `ctx_leave()`, after the mutex is released. Debug trace (`SS_ENABLE_DEBUG_TRACE`) writes to the stream set by `ss_enable_trace()` and is silent until then; the library uses an internal trace function. The public `SS_TRACE` macro in `ss_config.h` is kept only for source compatibility, deprecated and unused by the library.

## Build Variants

| Source File | Role |
|------------|------|
| `src/ss_lib.c` | Primary implementation, written in the common subset of C89 and C11 |
| `src/ss_lib_c89.c` | C89 copy for legacy toolchains: identical body, own header comment |
| `include/ss_lib_single.h` | Generated single-header build |

`src/ss_lib.c` uses no `//` comments, no variadic macros, no designated initializers and declares variables at the top of blocks; C11-only facilities (`<stdatomic.h>`) are selected with `__STDC_VERSION__`. `scripts/check_c89_sync.sh` checks that everything from the `SS_FEATURE_MACROS_BEGIN` marker on is identical in both files (`--fix` copies the body over); `make test-c89` builds with `-std=c89 -pedantic`.

`include/ss_lib_single.h` is generated by `make single-header` (`create_single_header.sh`) from `ss_config.h`, `ss_lib.h` and `src/ss_lib.c`; define `SS_IMPLEMENTATION` in one translation unit. `make check-single-header` fails when the committed copy is stale or does not compile. The version lives only in `include/ss_lib.h` (`SS_VERSION_*`); `scripts/version.sh` prints it for the build scripts.
