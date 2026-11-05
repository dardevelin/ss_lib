# Architecture

This document describes the internal design and data structures of SS_Lib 3.0.0. The source of truth is `src/ss_lib.c`; its header comment summarizes the same structure.

## Overview

SS_Lib is a signal-slot event system with two memory models (dynamic and static), optional per-bus thread safety, and a layered feature set controlled by compile-time flags. All state lives in buses (`ss_bus_t`), which share nothing.

```
+------------------+
|  Public API      |  ss_emit, ss_connect, ss_signal_register, ...
+------------------+
|  Entry / Exit    |  bus_enter / bus_leave: lock, nesting depth, deferred destroy, errors
+------------------+
|  Feature Layer   |  Namespaces, Deferred, Batch, ISR queue, Stats, Trace
+------------------+
|  Core Engine     |  Name resolution, signal lookup, slot iteration, sweep
+------------------+
|  Storage         |  Dynamic (heap lists) or Static (fixed arrays), one interface
+------------------+
```

## Buses

All state lives in a bus, `struct ss_bus` (public as the opaque `ss_bus_t`). A bus holds the signal storage, the default namespace (`char ns[SS_MAX_SIGNAL_NAME_LENGTH]`, an inline buffer), the error handler and its `user_data`, the handle counter, the slot limit, the mutex (when it has one), the deferred queue entries, the ISR queue (with `SS_ENABLE_ISR_SAFE`), the statistics and trace settings, and the nesting counters `call_depth` and `closing`. Every public function that uses state takes the bus as its first argument, and every internal function (name resolution, the storage interface, emission, sweep and retire, the entry queue, error reporting, trace) takes it as its `bus` parameter. Buses share nothing, so two buses with the same signal names are independent.

The library has no default bus and almost no global state. What remains is, in static mode, the table of built-in buses `g_static_buses[SS_STATIC_BUSES]` in `.bss`.

### Where a bus lives

The bus's `storage` field records where its own memory came from, so `ss_bus_destroy()` knows how to release it:

| Created by | `storage` | Bus memory | Released by `ss_bus_destroy()` |
|------------|-----------|------------|--------------------------------|
| `ss_bus_create()`, dynamic mode | `BUS_ON_HEAP` | Heap (`SS_CALLOC`) | `SS_FREE` |
| `ss_bus_create()`, static mode | `BUS_BUILT_IN` | An entry of `g_static_buses` | Marked `BUS_FREE` for reuse |
| `ss_bus_create_in(pool, size)`, either mode | `BUS_IN_POOL` | Caller's pool, aligned inside it | Nothing: the pool belongs to the caller |

An unused built-in bus has `storage == BUS_FREE` (zero, as `.bss` starts). `ss_bus_create()` in static mode takes the first free entry and returns `SS_ERR_WOULD_OVERFLOW` when every one is in use; it never touches the heap. Claiming and releasing built-in buses is not locked, so they are created and destroyed from one thread. `SS_STATIC_BUSES=0` drops the table: buses then come only from pools. Dynamic-mode creation only calls `SS_CALLOC` and is safe from any thread.

`ss_bus_pool_size()` returns `sizeof(struct ss_bus)` plus alignment slack, so a pool needs no particular alignment. A NULL pool is `SS_ERR_NULL_PARAM` and a pool that cannot hold the aligned bus is `SS_ERR_BUFFER_TOO_SMALL`; nothing is written to it then. A pool only places the bus; signals and slots still follow the compiled memory model.

`bus_init()` makes any of these memories an empty bus (`memset`, defaults, queue setup, the mutex if requested) and sets `storage` last, so a built-in bus is only claimed once it is usable.

### Configuration check

`ss_bus_create(&bus, flags)` and `ss_bus_create_in(&bus, pool, size, flags)` are macros that pass the caller's `SS_ABI_TAG` (`sizeof(ss_data_t) * 16 + SS_ABI_REVISION` as the caller's compilation sees it) to `ss_bus_create_abi()` / `ss_bus_create_in_abi()`. Both start with `create_check()`: a NULL output pointer is `SS_ERR_NULL_PARAM`; a tag that differs from the library's, or a flag the library does not support (`SS_BUS_THREAD_SAFE` without `SS_ENABLE_THREAD_SAFETY`, or an unknown bit), is `SS_ERR_CONFIG_MISMATCH`. The check therefore runs on every creation without the caller asking for it.

No configuration macro changes a public type: `ss_lib.h` has no `#if` inside a struct, union or enum body, and custom data lives in the value union (`value.custom`). Feature flags only gate function declarations, so a caller and library built with different flags fail to compile or link rather than disagree on a layout. The tag therefore guards against what flags cannot cause: a header from another version (`SS_ABI_REVISION` is bumped when a public layout changes) and layout-changing compiler options. `scripts/check_abi.sh` (`make check-abi`) enforces both rules: it rejects a conditional inside a public type body and compares struct sizes, member offsets and enum values printed by a probe compiled in every tested configuration.

## Entry and Exit (`bus_enter` / `bus_leave`)

Every public function that touches a bus is wrapped in `bus_enter()` / `bus_leave()`. Locking, nesting, deferred destruction and error reporting live only there.

`bus_enter(bus, &locked)`:
1. Returns 0 for a NULL bus (the caller returns `SS_ERR_NULL_PARAM`, 0 or NULL)
2. Locks the bus's mutex if the bus was created with `SS_BUS_THREAD_SAFE` (`thread_safe`, fixed at creation), and records whether it did
3. Returns 0 if `closing` is set (a deferred `ss_bus_destroy()` is pending)
4. Increments `call_depth` and returns 1

`bus_leave()`:
1. Decrements `call_depth`; if it reached 0 and `closing` is set, the bus must be destroyed
2. On success with nothing to destroy, unlocks the mutex if this call locked it and returns (the fast path reads nothing else)
3. Otherwise (`bus_leave_slow()`): reads the error handler and its `user_data` while still locked, unlocks, then destroys the bus (deferred destroy) or, if the call failed, calls `handler(bus, err, msg, user_data)` with the signal name or `ss_error_string(err)` as `msg`

The handler fires for every failing call on the bus, including parameter errors (NULL arguments, handle 0, a batch being destroyed, too-long names). Calls with no bus (a NULL bus or batch) and failed creation have no handler to report to. Calling it after unlocking means a handler may itself call the library, also on its bus; a call nested in a slot reports while the emission that ran the slot still holds the lock. `ss_bus_destroy()` clears the handler and `user_data` when it sets `closing`, so a bus being destroyed reports nothing: neither the calls still running on it nor the outermost call that frees it. A handler that destroys its bus after the outermost call frees it at once; `bus_leave_slow()` touches the bus no more after the handler returns.

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
bus->signals -> [sig3] -> [sig2] -> [sig1] -> NULL
```

### Static Mode

Signals are a fixed array `signals[SS_MAX_SIGNALS]` inside the bus, so each bus has its own. Names and descriptions are inline buffers (`name[SS_MAX_SIGNAL_NAME_LENGTH]`, `description[SS_MAX_DESCRIPTION_LENGTH]`). A free entry has `name[0] == '\0'`; registration takes the first free entry and returns `SS_ERR_WOULD_OVERFLOW` when none is left or the description is too long.

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

In dynamic mode each slot is a heap node; in static mode it comes from `slots[SS_MAX_SLOTS]` in the bus, where a free entry has `func == NULL`.

`sig->slot_count` counts live slots only (removed ones are excluded), so the per-signal limit (`max_slots_per_signal`) applies to live slots.

### Connection Handles

Each slot gets a handle from `bus->next_handle`, starting at 1 and skipping 0 on wrap-around (0 means "no handle"). `ss_disconnect_handle` scans the slots of all live signals of the bus for it. Handles are per bus: two buses hand out the same values.

## Removal and Safe Emission

Slots and signals are never freed while their signal is emitting. They are marked and released when the outermost emission of that signal ends.

- **Disconnect**: `slot->removed = 1`, `sig->slot_count--`, `sig->dirty = 1`. If the signal is not emitting, `sweep_removed_slots()` unlinks and frees the marked slots at once.
- **Unregister**: all slots are marked removed and the signal leaves `signal_count`. If it is emitting, `sig->retired = 1`; retired signals are invisible to lookup (so the name can be registered again immediately) and are destroyed at the end of their outermost emission.
- **Bus destroy**: see below; it marks every slot of every signal of the bus removed.

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
    if (sig->retired) signal_destroy(bus, sig);
    else if (sig->dirty) sweep_removed_slots(bus, sig);
}
```

Consequences:
- A slot disconnected by an earlier slot is skipped; nothing is freed under the iterator.
- Unregistering the signal or calling `ss_bus_destroy()` from a slot marks every slot removed, so the rest of that emission is skipped.
- A slot connected during an emission of the same signal is inserted into the live list. Whether that emission reaches it depends on where its priority places it relative to the iterator, so callers must not rely on either outcome.

## Data Ownership

An `ss_data_t` frees its string or custom payload only when its `owned` field is set, and only `ss_data_set_string` and `ss_data_set_custom` (which copy) set it. `ss_data_clear` frees an owned payload (calling the custom cleanup on the copy first) and resets the object to `SS_DATA_INIT`; every setter starts with it, and `ss_data_destroy` is `ss_data_clear` plus `SS_FREE` of the object. Everything else borrows: direct field writes, the `_ref` setters, the `ss_emit_*` helpers (`data_of`), deferred and batch entries (above), and ISR events.

Slots always receive borrowed data. `ss_emit` and `ss_emit_namespaced` pass an object that owns its payload through `borrowed_view`, a stack copy with `owned` cleared that points to the same payload; borrowed data is passed as is. A slot that copies its data and reuses the copy with a setter therefore never frees the emitter's payload.

## Deferred Destroy

`ss_bus_destroy()` enters the bus, sets `closing = 1` and marks all its slots removed. If no other call on the bus is in progress, `bus_leave()` destroys the bus immediately. Called from inside a slot, `call_depth` is still above zero, so destruction waits until the bus's outermost call returns. Meanwhile `bus_enter()` refuses every call on the bus (`SS_ERR_NULL_PARAM`, 0 or NULL; a second `ss_bus_destroy()` is a no-op), and queue flushes and ISR drains on it stop early. The bus's memory stays valid until then, so the slots still running may keep passing the pointer. Other buses are not affected, also when the slot is running inside a call on another bus.

`bus_leave()` unlocks, then `bus_free()` releases pending deferred entries, destroys all signals, destroys the mutex (if the bus has one), and frees the bus (heap), marks it `BUS_FREE` (built-in) or leaves it (pool). After that the pointer is dangling: a later call is undefined behavior, like memory after `free()`.

## Entry Queue (Deferred Emission and Batches)

The deferred queue and batches share one implementation: `ss_queue_t`, a ring buffer (`entries`, `capacity`, `head`, `count`) of `ss_entry_t`.

```c
typedef struct {
    char signal_name[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_data_t data;              /* type and scalar value; pointers and owned cleared */
    int has_payload;
    /* static: inline bytes[SS_DEFERRED_PAYLOAD_SIZE]; dynamic: heap copy */
} ss_entry_t;
```

- `queue_push` copies the resolved name and the data. String and custom payloads are copied (static mode: into the inline buffer, aligned like `long double`, from C11 `max_align_t` (not with MSVC, whose C headers lack it), and with GCC/Clang `__int128`, `SS_ERR_WOULD_OVERFLOW` if larger than `SS_DEFERRED_PAYLOAD_SIZE`; dynamic mode: `SS_MALLOC`). The entry, not its `ss_data_t`, owns the copy (`has_payload`, freed by `entry_release`): `owned` and `value.custom.cleanup` are cleared in the entry, so the cleanup belongs to the caller's object and is never called for the copy. A library built without `SS_ENABLE_CUSTOM_DATA` has no code to copy custom data, so `entry_fill` refuses an `SS_TYPE_CUSTOM` entry with `SS_ERR_INVALID_TYPE` before touching the ring slot.
- Every entry gets a push sequence number from the queue's `pushed` counter. `queue_flush` records `pushed` when it starts and stops at the first entry pushed after that (wrap-safe comparison), so it emits only the entries queued before it started, oldest first, even when a slot runs a nested flush of the same queue. Each entry is **popped before it is emitted**, so slots may push to, or flush, the same queue; entries pushed meanwhile wait for the next flush. The slot sees an `ss_data_t` rebuilt by `entry_view` that points into the entry's payload copy, released right after the emission.
- Flushing stops early if the bus is closing (or, for batches, if the batch was destroyed by a slot).

The deferred queue is `deferred_entries[SS_DEFERRED_QUEUE_SIZE]` inside the bus. A batch (`struct ss_batch`) is a heap object with `entries[SS_BATCH_MAX_ENTRIES]`, its queue, the `bus` it was created for (`ss_batch_create(bus)`), and `emitting` / `destroy_pending` counters. `ss_batch_add` and `ss_batch_emit` enter that bus, which resolves names, emits and reports errors. `ss_batch_destroy` from a slot only sets `destroy_pending`, and `ss_batch_emit` frees the batch when it returns (it reads `batch->bus` first, so it can still leave the bus). `ss_batch_destroy` never touches the bus, so it also works on a batch whose bus a slot destroyed. Batches are not locked against concurrent use of the same batch.

## ISR Queue

With `SS_ENABLE_ISR_SAFE`, each bus has its own ISR queue: a fixed array `isr_queue[SS_ISR_QUEUE_SIZE]` and a sequence counter `isr_seq`, the first members of `struct ss_bus` (small offsets for Thumb-1 code). `ss_emit_from_isr(bus, ...)` writes into that bus's queue and `ss_process_isr_queue(bus)` drains only that queue, so an event is delivered only by the bus it was queued on. Each entry has an atomic `state`, an atomic sequence number `seq`, the int value and the name. A zeroed bus (`bus_init`'s `memset`, `ISR_FREE` is 0) has an empty queue.

Entry states: `FREE -> WRITING -> READY -> READING -> FREE`.

`ss_emit_from_isr`:
1. Rejects a NULL bus and NULL/empty names (`SS_ERR_NULL_PARAM`) and names of `SS_MAX_SIGNAL_NAME_LENGTH` or more (`SS_ERR_WOULD_OVERFLOW`), touching nothing
2. Claims the first entry of the bus's queue with compare-and-swap `FREE -> WRITING`; none free is `SS_ERR_WOULD_OVERFLOW`
3. Copies name and value, stores the bus's next sequence number
4. Publishes with a release store `-> READY`

No locks, no allocation, no slot calls, and no other field of the bus is read: no `bus_enter`, so neither the mutex of a `SS_BUS_THREAD_SAFE` bus nor `closing`. Because claiming is a CAS, nested interrupts and several threads may enqueue concurrently, also while another call on the bus is in progress.

`ss_process_isr_queue(bus)` (normal context, through `bus_enter`):
1. Snapshots the READY entries and sorts them by sequence number (wrap-around safe). An interrupt during the scan can queue one event in an entry already scanned and a newer one in an entry not yet scanned, so it scans again until a scan finds no new entry (at most `SS_ISR_QUEUE_SIZE` extra scans); READY entries stay READY until taken, so the snapshot then holds every event older than its newest
2. For each, claims it `READY -> READING`, copies name and value, frees it (`-> FREE`), resolves the name against the bus's default namespace and emits it on the bus

Events queued after the snapshot wait for the next call. The `READY -> READING` claim gives each event to exactly one drain, also when a slot drains the same bus again from inside a drain. The ordering argument holds per bus: each queue has its own counter, and an interrupt that queues on another bus during the scan does not touch this queue.

**Destruction.** The queue goes with the bus: `bus_free` releases or recycles the bus's memory without reading or waiting for any entry, so the events still queued are dropped (a drain interrupted by the bus's destruction stops, and its remaining events are dropped too). An entry an interrupt is still writing (`WRITING`) cannot stall destruction, but the interrupt would then write into freed or reused memory, so callers disable the interrupts that emit on a bus before `ss_bus_destroy()` (and create the bus before enabling them). During a deferred destruction (from a slot) the bus memory is still valid: `ss_emit_from_isr` still queues, and the event is dropped with the rest. A built-in or pool bus created again in the same memory starts with an empty queue.

Atomics come from C11 `<stdatomic.h>`, GCC/Clang `__atomic` builtins, or MSVC `Interlocked*`. If `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` are defined, they take precedence and wrap plain accesses, for cores without compare-and-swap (Cortex-M0, AVR); they must also be compiler memory barriers. `tests/test_isr.c` compiles the implementation with hooked macros to interrupt `ss_process_isr_queue` at every point of its scan, with two buses: the interrupt queues on the bus being drained or on the other one.

## Namespace Implementation

Namespaces are syntactic: there is no namespace registry. `ss_set_namespace` copies the namespace into the bus's inline `ns` buffer (`NULL` or `""` clears it; a namespace too long for even `"ns::x"` is `SS_ERR_WOULD_OVERFLOW`). `ss_get_namespace(bus, buf, size)` copies it out under the bus's lock (`""` when unset; `SS_ERR_BUFFER_TOO_SMALL`, with `buf` set to `""`, when it does not fit), so the buffer is never handed out: a reader cannot see it change, and the argument of `ss_set_namespace` can never point into it, so the copy needs no overlap check. A namespace saved with `ss_get_namespace` restores with `ss_set_namespace`. Name resolution (above) applies the namespace to every name-taking function. `ss_emit_namespaced(bus, ns, name, data)` builds `"ns::name"` itself and ignores the default namespace.

## Thread Safety

Thread safety is chosen per bus at creation. With `SS_ENABLE_THREAD_SAFETY=1`, a bus created with `SS_BUS_THREAD_SAFE` gets its own **recursive** mutex (pthread mutex with `PTHREAD_MUTEX_RECURSIVE`, or a Windows `CRITICAL_SECTION`) and `thread_safe = 1`; `bus_enter` locks it on every call. A bus created without the flag has no mutex and is never locked. The flag cannot change later. Without `SS_ENABLE_THREAD_SAFETY` the flag is refused with `SS_ERR_CONFIG_MISMATCH`.

- Every public function that reads or writes the bus's state locks, including the deferred queue, namespace, error handler, profiling flag and max-slots setting.
- Slots run with their bus's mutex held. Recursion lets a slot call back into the same bus on the same thread; a slot that waits for another thread that is itself calling that bus deadlocks.
- Different buses share no mutable state and never contend. A slot of bus A that calls bus B holds A's lock while it takes B's, so two threads making cross-bus calls in opposite directions (A to B and B to A) can deadlock: cross-bus calls from slots must go in one direction only.
- `ss_bus_destroy` is not thread-safe with respect to other threads using the same bus. Creating and destroying built-in buses (static mode) is not locked.
- The ISR enqueue path never locks, on any bus.

One mutex per bus, rather than fine-grained locking inside a bus, keeps the code small and free of lock-ordering issues within a bus. The trade-off is that concurrent emission of different signals on the same bus contends on its lock; an application that needs parallelism splits its signals over several buses.

## Memory Modes and Allocation

| Object | Dynamic mode | Static mode |
|--------|--------------|-------------|
| Bus | Heap, or caller pool | Built-in bus in `.bss` (`SS_STATIC_BUSES`), or caller pool |
| Signals, names, descriptions | Heap | Bus arrays |
| Slots | Heap | Bus array |
| Namespace | Bus buffer | Bus buffer |
| Deferred entries and payloads | Bus array, heap payload copies | Bus array, inline payloads |
| `ss_data_create` / `ss_data_set_string` / `ss_data_set_custom` | Heap | Heap |
| `ss_data_set_string_ref` / `ss_data_set_custom_ref` | None | None |
| `ss_get_signal_list` result, `ss_batch_create` | Heap | Heap |

In static mode, bus creation, register, connect, emit, deferred, ISR, namespace, disconnect, unregister and bus destruction never touch the heap, on every bus. Only functions whose purpose is to hand the caller an allocated object allocate. `tests/test_memory.c` checks this with two built-in buses and one pool bus alive at once.

## Custom Allocators

All allocation goes through `SS_MALLOC`, `SS_FREE` and `SS_CALLOC` from `ss_config.h`. Strings are copied with an internal `ss_strdup` that uses `SS_MALLOC`, so overriding `SS_MALLOC`/`SS_FREE` routes every allocation; `SS_STRDUP` can be defined to override string duplication specifically. With `SS_ENABLE_MEMORY_STATS` in dynamic mode, each bus accounts the heap bytes it holds (the bus itself when it is on the heap, signals, slots, strings).

## Error Reporting

Public functions return `ss_error_t`. Failures of calls on a bus are also reported to that bus's error handler (`ss_error_handler_t`, called with the bus and the `user_data` given to `ss_set_error_handler()`) from `bus_leave()`, after the mutex is released, until `ss_bus_destroy()` is called on the bus. Debug trace (`SS_ENABLE_DEBUG_TRACE`) writes to the stream set by `ss_enable_trace(bus, stream)` (stored in the bus and changed under its lock) and is silent until then; the library uses an internal trace function, and no public trace macro exists.

## Build Variants

| Source File | Role |
|------------|------|
| `src/ss_lib.c` | Primary implementation, written in the common subset of C89 and C11 |
| `src/ss_lib_c89.c` | C89 copy for legacy toolchains: identical body, own header comment |
| `include/ss_lib_single.h` | Generated single-header build |

`src/ss_lib.c` uses no `//` comments, no variadic macros, no designated initializers and declares variables at the top of blocks; C11-only facilities (`<stdatomic.h>`) are selected with `__STDC_VERSION__`. `scripts/check_c89_sync.sh` checks that everything from the `SS_FEATURE_MACROS_BEGIN` marker on is identical in both files (`--fix` copies the body over); `make test-c89` builds with `-std=c89 -pedantic`.

`include/ss_lib_single.h` is generated by `make single-header` (`create_single_header.sh`) from `ss_config.h`, `ss_lib.h` and `src/ss_lib.c`; define `SS_IMPLEMENTATION` in one translation unit. `make check-single-header` fails when the committed copy is stale or does not compile. The version lives only in `include/ss_lib.h` (`SS_VERSION_*`); `scripts/version.sh` prints it for the build scripts.
