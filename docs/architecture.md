# Architecture

This document describes the internal design and data structures of SS_Lib.

## Overview

SS_Lib is a signal-slot event system with two memory models (dynamic and static), optional thread safety, and a layered feature set controlled by compile-time flags.

```
+------------------+
|  Public API      |  ss_emit, ss_connect, ss_signal_register, ...
+------------------+
|  Feature Layer   |  Namespaces, Deferred, Batch, Stats, Trace
+------------------+
|  Core Engine     |  Signal lookup, slot iteration, sweep
+------------------+
|  Memory Layer    |  Dynamic (malloc/free) or Static (pool arrays)
+------------------+
```

## Signal Registry

### Dynamic Mode

Signals are stored in a singly-linked list. Each `ss_signal_t` node is heap-allocated and contains a dynamically allocated name string.

```
g_context->signals -> [sig3] -> [sig2] -> [sig1] -> NULL
```

Signal lookup (`find_signal`) traverses the list with `strcmp`. This is O(n) but suitable for typical signal counts (< 100).

New signals are prepended to the list head for O(1) insertion.

### Static Mode

Signals are stored in a fixed-size array (`ss_signal_t signals[SS_MAX_SIGNALS]`). A parallel `signal_used[]` bitmap tracks which entries are occupied. Signal names use pre-allocated buffers (`signal_names[][]`).

```
signals[0]: used=1, name="button_click"
signals[1]: used=0
signals[2]: used=1, name="data_ready"
...
```

Lookup scans the array linearly, checking the `signal_used` flag and comparing names.

## Slot Lists

Each signal maintains a singly-linked list of slots, sorted by priority at connection time (higher priority first).

```
sig->slots -> [CRITICAL] -> [HIGH] -> [NORMAL] -> [LOW] -> NULL
```

### Priority-Sorted Insertion

`ss_connect_ex` inserts the new slot at the correct position:

1. If the list is empty or the new slot has higher priority than the head, prepend
2. Otherwise, walk the list to find the insertion point where `next->priority < new_priority`
3. Insert after the found node

This ensures emission always invokes slots in descending priority order without sorting at emit time.

### Connection Handles

Each slot is assigned a monotonically increasing handle from `g_context->next_handle`. Handles are `uintptr_t` values starting at 1 (0 is reserved for "no handle"). The handle allows disconnection without knowing the function pointer or signal name.

## Safe Emission (Deferred Removal)

The core challenge in signal-slot systems is handling disconnection during emission. If a slot callback disconnects another slot (or itself), the iteration must not crash.

### Approach

1. Before iterating, set `sig->emitting++` (supports nested/reentrant emission)
2. Capture `next = slot->next` **before** invoking the callback
3. Skip slots where `slot->removed == 1`
4. After the loop, decrement `sig->emitting`
5. When `emitting` reaches 0, call `sweep_removed_slots()` to physically unlink and free removed slots

```c
sig->emitting++;
slot = sig->slots;
while (slot) {
    next_slot = slot->next;
    if (!slot->removed) {
        slot->func(data, slot->user_data);
    }
    slot = next_slot;
}
sig->emitting--;
if (sig->emitting == 0) {
    sweep_removed_slots(sig);
}
```

### Why This Works

- Capturing `next` before the callback prevents use-after-free if the current slot is removed
- The `removed` flag prevents invoking a slot that was disconnected by an earlier callback
- Sweep is deferred until all nested emissions complete

## Static Memory Pool

In static mode, slots are allocated from a fixed array:

```
ss_slot_t slots[SS_MAX_SLOTS];
uint8_t slot_used[SS_MAX_SLOTS];
```

`allocate_slot()` scans for the first unused entry. `free_slot()` calculates the index from pointer arithmetic and clears the entry.

This design:
- Guarantees zero heap fragmentation
- Has predictable memory usage
- Allows compile-time memory budgeting

## Deferred Emission Queue

A fixed-size array of `ss_deferred_entry_t` structures:

```c
typedef struct {
    char signal_name[SS_MAX_SIGNAL_NAME_LENGTH];
    ss_data_t data;
    int has_string;
} ss_deferred_entry_t;
```

`ss_emit_deferred` copies the signal name and data into the next queue slot. String data is duplicated via `SS_STRDUP` to prevent dangling pointers.

`ss_flush_deferred` snapshots the count, resets it to 0, then emits all entries. The snapshot prevents infinite loops if slot callbacks enqueue more deferred emissions.

## ISR Queue (Ring Buffer)

When `SS_ENABLE_ISR_SAFE` is enabled, a separate volatile ring buffer holds ISR-queued emissions:

```c
static volatile struct {
    char signal_name[SS_MAX_SIGNAL_NAME_LENGTH];
    int value;
    volatile int pending;
} g_isr_queue[SS_ISR_QUEUE_SIZE];
```

`ss_emit_from_isr`:
1. Scans for a non-pending entry
2. Copies the signal name via `ss_strscpy`
3. Sets the value
4. Issues a compiler write barrier (`__asm__ volatile("" ::: "memory")`)
5. Sets `pending = 1`

No mutex, no malloc, no function calls that might not be reentrant.

## Batch Operations

`ss_batch_t` is a heap-allocated structure containing a fixed array of `ss_deferred_entry_t`:

```c
struct ss_batch {
    ss_deferred_entry_t entries[SS_BATCH_MAX_ENTRIES];
    size_t count;
};
```

`ss_batch_emit` iterates the entries, emits each signal, frees any duplicated strings, and resets the count. The batch can be reused after emission.

## Namespace Implementation

Namespace support is purely syntactic. `ss_emit_namespaced("ui", "button", data)` constructs the string `"ui::button"` in a stack buffer and calls `ss_emit`. No separate namespace registry exists.

The default namespace (`ss_set_namespace`) is stored in `g_context->ns` as a heap-allocated string.

## Thread Safety

When `SS_ENABLE_THREAD_SAFETY=1` and `ss_set_thread_safe(1)` is called:

- A single global mutex protects all signal/slot operations
- The mutex is acquired at the start of each public function and released before return
- ISR emission bypasses the mutex entirely (lock-free path)

The single-mutex design was chosen over fine-grained locking for:
- Simplicity (fewer deadlock scenarios)
- Small code size (important for embedded)
- Predictable behavior (no lock ordering issues)

The trade-off is that concurrent emission of different signals still contends on the same lock. This is acceptable for the target use cases (embedded, game engines) where emission is typically single-threaded or occurs on a dedicated event loop.

## Custom Allocators

All allocation goes through four macros defined in `ss_config.h`:

```c
SS_MALLOC(size)
SS_FREE(ptr)
SS_CALLOC(count, size)
SS_STRDUP(str)
```

Both `ss_lib.c` and `ss_lib_c89.c` use these exclusively. Users can redirect allocation to pool allocators, RTOS heaps, or debug allocators by redefining these macros.

## Error Reporting

Internal errors call `report_error(error_code, message)` which invokes the user-set error handler (if any). This allows centralized error logging without polluting the API return path.

## Build Variants

| Source File | Standard | Use Case |
|------------|----------|----------|
| `src/ss_lib.c` | C11 | Primary implementation |
| `src/ss_lib_c89.c` | C89 | Legacy toolchains |

Both files provide identical functionality and pass the same test suite. The C89 version avoids:
- `//` comments
- Mixed declarations and code
- `= {0}` aggregate initializers (uses `memset` instead)
- Variadic macros in user-facing code
- C99/C11-only headers (`<stdbool.h>` is not used)

The `ss_config.h` header is shared and works with both versions.
