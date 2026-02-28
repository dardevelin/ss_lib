# Performance Tuning

## Profiling API

SS_Lib includes built-in per-signal performance profiling. Enable it at compile time and runtime:

### Compile-Time

```c
#define SS_ENABLE_PERFORMANCE_STATS 1
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

When `SS_ENABLE_PERFORMANCE_STATS=0`, `ss_enable_profiling()` compiles to a no-op stub. No timing overhead is added to emission.

## Timing Implementation

- **Linux/macOS**: Uses `clock_gettime(CLOCK_MONOTONIC)` for nanosecond resolution
- **Windows**: Uses `QueryPerformanceCounter` / `QueryPerformanceFrequency`
- Timing wraps only the slot invocation loop, not lock acquisition

## Memory Diagnostics

Track allocation usage with memory statistics:

```c
#define SS_ENABLE_MEMORY_STATS 1
```

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

In static mode, `signals_allocated` and `slots_allocated` reflect pool capacity (`SS_MAX_SIGNALS` and `SS_MAX_SLOTS`).

## Performance Characteristics

### Signal Lookup

Signals are stored in a linked list (dynamic mode) or scanned linearly (static mode). Lookup is O(n) where n is the number of registered signals.

For high signal counts, the hash table in the C11 implementation provides O(1) average-case lookup.

### Slot Execution

Slots are stored in a singly-linked list sorted by priority. Execution iterates the list once — O(k) where k is the number of slots connected to the signal.

### Connection

`ss_connect_ex` performs priority-sorted insertion: O(k) in the worst case. `ss_connect` (default priority) is effectively O(1) when most slots share the same priority.

### Disconnection

- `ss_disconnect_handle` — O(n*k) in the worst case (searches all signals for the handle)
- `ss_disconnect` — O(k) (scans one signal's slot list)
- During emission, disconnection is O(1) — the slot is flagged for deferred removal

## Optimization Tips

### Reduce Signal Count

Fewer signals means faster lookup. Combine related events where possible.

### Use Connection Handles

Store handles at connection time instead of searching by function pointer later. Handle-based disconnection avoids the function pointer scan.

### Disable Unused Features

Each disabled feature removes conditional checks from the code path:

```c
#define SS_ENABLE_THREAD_SAFETY 0    /* no mutex checks */
#define SS_ENABLE_PERFORMANCE_STATS 0 /* no timing overhead */
#define SS_ENABLE_MEMORY_STATS 0      /* no allocation tracking */
```

### Minimize Lock Contention

If thread safety is enabled, emission holds the global mutex for the duration of all slot invocations. Keep slot callbacks short or use deferred emission to batch work outside the lock.

### Use Typed Emit Functions

`ss_emit_int`, `ss_emit_float`, etc. construct the `ss_data_t` on the stack — no heap allocation. Prefer these over manually creating `ss_data_t` objects.

### Batch Emissions

When emitting multiple signals in sequence, consider `ss_batch_emit()` to reduce per-emission overhead.

## Benchmark Methodology

The project includes a benchmark suite in `benchmarks/benchmark_ss_lib.c`. It measures:

- Signal registration time
- Slot connection time
- Signal lookup time
- Disconnect by handle time
- Emission time with varying slot counts
- Priority slot emission time

Run benchmarks:

```bash
make benchmark        # Quick run
make benchmark-all    # Comprehensive suite with multiple configurations
```

Benchmark results from CI are available in the [GitHub Actions workflow](https://github.com/dardevelin/ss_lib/actions/workflows/benchmarks.yml).

## Cache Considerations

The `SS_CACHE_LINE_SIZE` macro (default 64) is available for alignment-aware data structures. The current implementation does not enforce cache-line alignment but the macro is available for future use and custom allocators.
