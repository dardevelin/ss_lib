# Performance Tuning

## Profiling API

SS_Lib includes built-in per-signal performance profiling. Enable it at compile time, and at runtime per bus:

### Compile-Time

```bash
-DSS_ENABLE_PERFORMANCE_STATS=1   # for the library and every caller
```

### Runtime

```c
ss_enable_profiling(bus, 1);

/* Run your application... */

ss_perf_stats_t stats;
ss_get_perf_stats(bus, "my_signal", &stats);
printf("Signal: my_signal\n");
printf("  Emissions: %llu\n", stats.total_emissions);
printf("  Total time: %llu ns\n", stats.total_time_ns);
printf("  Avg time: %llu ns\n", stats.avg_time_ns);
printf("  Min time: %llu ns\n", stats.min_time_ns);
printf("  Max time: %llu ns\n", stats.max_time_ns);
```

### Reset Statistics

```c
ss_reset_perf_stats(bus);  /* Clears the statistics of every signal on the bus */
```

### Disable Profiling

When `SS_ENABLE_PERFORMANCE_STATS=0`, the profiling functions are not declared and no timing code is compiled into emission. When compiled in but `ss_enable_profiling(bus, 0)` (the default for a new bus), emission only checks a flag.

## Timing Implementation

- **Linux/macOS**: Uses `clock_gettime(CLOCK_MONOTONIC)` for nanosecond resolution
- **Windows**: Uses `QueryPerformanceCounter` / `QueryPerformanceFrequency`
- Timing covers the signal lookup and the slot invocation loop, not lock acquisition or name qualification
- Emissions of unknown signals are not recorded

## Memory Diagnostics

Track allocation usage with memory statistics (`-DSS_ENABLE_MEMORY_STATS=1`, for the library and every caller):

```c
ss_memory_stats_t mem;
ss_get_memory_stats(bus, &mem);

printf("Signals: %zu used / %zu allocated\n",
       mem.signals_used, mem.signals_allocated);
printf("Slots: %zu used / %zu allocated\n",
       mem.slots_used, mem.slots_allocated);
printf("String bytes: %zu\n", mem.string_bytes);
printf("Peak bytes: %zu\n", mem.peak_bytes_allocated);
```

- The statistics describe one bus.
- Dynamic mode: `total_bytes_allocated` is the heap the bus currently holds (the bus itself unless it lives in a caller pool, its signals, slots and strings) and `peak_bytes_allocated` its maximum.
- Static mode: `signals_allocated` and `slots_allocated` are the pool capacities (`SS_MAX_SIGNALS`, `SS_MAX_SLOTS`), and both byte counts equal `sizeof` the bus.
- `signals_used` / `slots_used` count live signals and slots. `ss_reset_memory_stats(bus)` resets the peak to the current value.

## Performance Characteristics

### Signal Lookup

There is no hash table. Every name-taking call (emit, connect, disconnect, exists, ...) scans the bus's signals linearly with `strcmp`: a linked list in dynamic mode (newest signal first), a fixed array in static mode. Lookup is O(n) in the number of signals registered on the bus and is the main per-emit cost besides the slots themselves. When a default namespace is set, names without `"::"` are additionally copied into a qualified `"ns::name"` buffer before the scan.

### Slot Execution

Slots are stored in a singly-linked list sorted by priority. Execution iterates the list once — O(k) where k is the number of slots connected to the signal.

### Connection

Connection is one signal lookup plus a priority-sorted insertion, O(n + k). Equal priorities keep connection order, so a new slot is inserted after all slots of the same or higher priority.

### Disconnection

- `ss_disconnect_handle` — O(total slots on the bus) in the worst case (searches the slots of all its signals for the handle)
- `ss_disconnect` — O(n + k) (signal lookup, then one signal's slot list)
- During an emission of that signal, the slot is only flagged; it is unlinked and freed after the emission

## Optimization Tips

### Reduce Signal Count

Fewer signals means faster lookup, since lookup is linear. Combine related events where possible, and keep frequently emitted signals few. Signals on different buses do not slow each other down: a subsystem with its own bus scans only its own signals.

### Use Connection Handles

Handles identify a connection without the signal name or function pointer, and distinguish several connections of the same function. `ss_disconnect_handle` compares integers but scans the slots of every signal on the bus.

### Disable Unused Features

Each disabled feature removes conditional checks from the code path. Pass the flags on the command line so the library and every caller see the same configuration (flags gate function declarations, not layouts, so a mismatch is a compile or link error):

```bash
-DSS_ENABLE_THREAD_SAFETY=0      # no mutex code at all
-DSS_ENABLE_PERFORMANCE_STATS=0  # no timing overhead
-DSS_ENABLE_MEMORY_STATS=0       # no allocation tracking
```

### Minimize Lock Contention

A bus created with `SS_BUS_THREAD_SAFE` has one recursive mutex, held for the duration of all slot invocations of an emission (`ss_flush_deferred` and `ss_batch_emit` hold it for the whole flush). Keep slot callbacks short and move heavy work out of slots, e.g. hand it to a worker queue of your own. A bus created without the flag has no mutex and costs nothing to lock; buses never contend with each other, so giving independent subsystems their own buses removes contention between them.

### Use Typed Emit Functions

`ss_emit_int`, `ss_emit_float`, etc. construct the `ss_data_t` on the stack — no heap allocation. Prefer these over manually creating `ss_data_t` objects.

### Batch Emissions

`ss_batch_emit()` takes the lock once for all entries, but `ss_batch_add()` copies string and custom payloads and each entry is still looked up by name. Use batches for grouping and ordering, not as a speed optimization for small payloads.

### Size the Static Buffers

In static mode each bus is a single object: one of the `SS_STATIC_BUSES` built-in buses in `.bss` (default 1), or a caller pool given to `ss_bus_create_in()`. Every built-in bus costs its full size whether it is used or not, so keep `SS_STATIC_BUSES` at the number of buses the program creates with `ss_bus_create()`; with `SS_STATIC_BUSES=0` there is no built-in bus and every bus comes from a pool. Besides `SS_MAX_SIGNALS`, `SS_MAX_SLOTS` and `SS_MAX_SIGNAL_NAME_LENGTH`, the size of a bus is driven by:

- `SS_DEFERRED_QUEUE_SIZE` entries, each holding a name buffer plus `SS_DEFERRED_PAYLOAD_SIZE` payload bytes
- `SS_MAX_DESCRIPTION_LENGTH` bytes per signal (only with `SS_ENABLE_INTROSPECTION`)
- with `SS_ENABLE_ISR_SAFE`, `SS_ISR_QUEUE_SIZE` ISR queue entries, each holding a name buffer and three ints

`ss_bus_pool_size()` reports the size of one bus, including alignment slack, for `ss_bus_create_in()`.

## Benchmark Methodology

The project includes a benchmark suite in `benchmarks/benchmark_ss_lib.c`. It measures:

- Signal registration time
- Slot connection time
- Signal lookup time
- Disconnect by handle time
- Emission time with no slots and with 5 slots
- Priority slot emission time (10 slots)
- Signal existence check
- ISR enqueue time (`ss_emit_from_isr`, with `SS_ENABLE_ISR_SAFE`): only the enqueue is timed; the queue is drained with `ss_process_isr_queue()` outside the timed region

### Reference Numbers

Measured for 2.2.0; treat them as indicative, not guarantees:

| Measurement | 2.2.0 | 2.1.1 |
|-------------|-------|-------|
| Emit with 5 slots (Apple Silicon, clang -O2) | 11.8 ns | 11.6 ns |
| Code size, `.text` (Cortex-M4, arm-none-eabi-gcc -Os, embedded + minimal preset) | ~3.8 KB | ~3.1 KB |

Slot connection is much faster than in 2.1.1, which rescanned every signal on each connect to update statistics. Since 2.2.0, static mode keeps the library state in `.bss` instead of allocating it at init; in 3.0 that storage is the built-in bus.

Run benchmarks:

```bash
make benchmark        # Quick run
make benchmark-all    # Comprehensive suite (benchmarks/run_benchmarks.sh, output in build/benchmarks/)
```

Compare two runs with `python3 benchmarks/compare.py BASE_RESULTS CURRENT_RESULTS`. `benchmarks/visualize.py` writes a JSON report. Both use only the Python standard library; `visualize.py` also draws plots if you happen to have matplotlib installed, but nothing requires it.

Benchmark results from CI are available in the [GitHub Actions workflow](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml).

## Cache Considerations

The `SS_CACHE_LINE_SIZE` macro (default 64) is available for alignment-aware data structures. The current implementation does not enforce cache-line alignment but the macro is available for future use and custom allocators.
