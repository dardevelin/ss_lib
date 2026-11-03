# Performance Tuning

## Profiling API

SS_Lib includes built-in per-signal performance profiling. Enable it at compile time and runtime:

### Compile-Time

```bash
-DSS_ENABLE_PERFORMANCE_STATS=1   # for the library and every caller
```

### Runtime

```c
ss_enable_profiling(1);

/* Run your application... */

ss_perf_stats_t stats;
ss_get_perf_stats("my_signal", &stats);
printf("Signal: my_signal\n");
printf("  Emissions: %llu\n", stats.total_emissions);
printf("  Total time: %llu ns\n", stats.total_time_ns);
printf("  Avg time: %llu ns\n", stats.avg_time_ns);
printf("  Min time: %llu ns\n", stats.min_time_ns);
printf("  Max time: %llu ns\n", stats.max_time_ns);
```

### Reset Statistics

```c
ss_reset_perf_stats();  /* Clears all signal statistics */
```

### Disable Profiling

When `SS_ENABLE_PERFORMANCE_STATS=0`, the profiling functions are not declared and no timing code is compiled into emission. When compiled in but `ss_enable_profiling(0)` (the default), emission only checks a flag.

## Timing Implementation

- **Linux/macOS**: Uses `clock_gettime(CLOCK_MONOTONIC)` for nanosecond resolution
- **Windows**: Uses `QueryPerformanceCounter` / `QueryPerformanceFrequency`
- Timing covers the signal lookup and the slot invocation loop, not lock acquisition or name qualification
- Emissions of unknown signals are not recorded

## Memory Diagnostics

Track allocation usage with memory statistics (`-DSS_ENABLE_MEMORY_STATS=1`, for the library and every caller):

```c
ss_memory_stats_t mem;
ss_get_memory_stats(&mem);

printf("Signals: %zu used / %zu allocated\n",
       mem.signals_used, mem.signals_allocated);
printf("Slots: %zu used / %zu allocated\n",
       mem.slots_used, mem.slots_allocated);
printf("String bytes: %zu\n", mem.string_bytes);
printf("Peak bytes: %zu\n", mem.peak_bytes_allocated);
```

- Dynamic mode: `total_bytes_allocated` is the heap the library currently holds (context, signals, slots, strings) and `peak_bytes_allocated` its maximum.
- Static mode: `signals_allocated` and `slots_allocated` are the pool capacities (`SS_MAX_SIGNALS`, `SS_MAX_SLOTS`), and both byte counts equal `sizeof` the context.
- `signals_used` / `slots_used` count live signals and slots. `ss_reset_memory_stats()` resets the peak to the current value.

## Performance Characteristics

### Signal Lookup

There is no hash table. Every name-taking call (emit, connect, disconnect, exists, ...) scans the signals linearly with `strcmp`: a linked list in dynamic mode (newest signal first), a fixed array in static mode. Lookup is O(n) in the number of registered signals and is the main per-emit cost besides the slots themselves. When a default namespace is set, names without `"::"` are additionally copied into a qualified `"ns::name"` buffer before the scan.

### Slot Execution

Slots are stored in a singly-linked list sorted by priority. Execution iterates the list once — O(k) where k is the number of slots connected to the signal.

### Connection

Connection is one signal lookup plus a priority-sorted insertion, O(n + k). Equal priorities keep connection order, so a new slot is inserted after all slots of the same or higher priority.

### Disconnection

- `ss_disconnect_handle` — O(total slots) in the worst case (searches the slots of all signals for the handle)
- `ss_disconnect` — O(n + k) (signal lookup, then one signal's slot list)
- During an emission of that signal, the slot is only flagged; it is unlinked and freed after the emission

## Optimization Tips

### Reduce Signal Count

Fewer signals means faster lookup, since lookup is linear. Combine related events where possible, and keep frequently emitted signals few.

### Use Connection Handles

Handles identify a connection without the signal name or function pointer, and distinguish several connections of the same function. `ss_disconnect_handle` compares integers but scans the slots of every signal.

### Disable Unused Features

Each disabled feature removes conditional checks from the code path. Pass the flags on the command line so the library and every caller see the same configuration (initialize with `ss_init_abi(SS_ABI_TAG)` to have a layout mismatch reported as `SS_ERR_CONFIG_MISMATCH`):

```bash
-DSS_ENABLE_THREAD_SAFETY=0      # no mutex checks
-DSS_ENABLE_PERFORMANCE_STATS=0  # no timing overhead
-DSS_ENABLE_MEMORY_STATS=0       # no allocation tracking
```

### Minimize Lock Contention

If thread safety is enabled, emission holds the library's single mutex for the duration of all slot invocations (`ss_flush_deferred` and `ss_batch_emit` hold it for the whole flush). Keep slot callbacks short and move heavy work out of slots, e.g. hand it to a worker queue of your own.

### Use Typed Emit Functions

`ss_emit_int`, `ss_emit_float`, etc. construct the `ss_data_t` on the stack — no heap allocation. Prefer these over manually creating `ss_data_t` objects.

### Batch Emissions

`ss_batch_emit()` takes the lock once for all entries, but `ss_batch_add()` copies string and custom payloads and each entry is still looked up by name. Use batches for grouping and ordering, not as a speed optimization for small payloads.

### Size the Static Buffers

In static mode the context is a single object in `.bss` (or the caller's pool). With `SS_STATIC_CONTEXT=0` there is no built-in `.bss` context: an application that always calls `ss_init_static()` with its own pool saves that RAM (`ss_init()` then allocates the context with `SS_CALLOC`). Besides `SS_MAX_SIGNALS`, `SS_MAX_SLOTS` and `SS_MAX_SIGNAL_NAME_LENGTH`, its size is driven by:

- `SS_DEFERRED_QUEUE_SIZE` entries, each holding a name buffer plus `SS_DEFERRED_PAYLOAD_SIZE` payload bytes
- `SS_MAX_DESCRIPTION_LENGTH` bytes per signal (only with `SS_ENABLE_INTROSPECTION`)

The ISR queue (`SS_ISR_QUEUE_SIZE` entries with a name buffer each) is a separate static array. `ss_get_required_pool_size()` reports the context size for `ss_init_static`.

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

Slot connection is much faster than in 2.1.1, which rescanned every signal on each connect to update statistics. In static mode the context is now in `.bss` (default `SS_STATIC_CONTEXT=1`) instead of being allocated at init.

Run benchmarks:

```bash
make benchmark        # Quick run
make benchmark-all    # Comprehensive suite (benchmarks/run_benchmarks.sh, output in build/benchmarks/)
```

Compare two runs with `python3 benchmarks/compare.py BASE_RESULTS CURRENT_RESULTS` (standard library only); `benchmarks/visualize.py` plots results and needs the packages in `benchmarks/requirements.txt`.

Benchmark results from CI are available in the [GitHub Actions workflow](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml).

## Cache Considerations

The `SS_CACHE_LINE_SIZE` macro (default 64) is available for alignment-aware data structures. The current implementation does not enforce cache-line alignment but the macro is available for future use and custom allocators.
