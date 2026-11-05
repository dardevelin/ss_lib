# Embedded Systems Guide

This guide covers using SS_Lib in resource-constrained embedded environments including bare-metal systems, RTOSes, and microcontrollers.

## Static Memory Mode

In static memory mode each bus holds its signals and slots in fixed arrays, and the bus itself is a built-in bus in static storage or lives in a pool you provide. Bus creation, register, connect, emit, deferred emission, ISR delivery, namespace, disconnect, unregister and bus destruction never touch the heap.

Functions whose job is to hand you an allocated object still use `SS_MALLOC`/`SS_CALLOC`: `ss_data_create`, `ss_data_set_string`, `ss_data_set_custom`, `ss_get_signal_list` and `ss_batch_create`. Avoid them on targets without a heap; the `ss_emit_*` helpers, and `ss_emit` or `ss_emit_deferred` with a stack `ss_data_t` (`SS_DATA_INIT`, filled with `ss_data_set_string_ref` / `ss_data_set_custom_ref`), need no allocation.

### Configuration

Pass the configuration on the command line, identically for the library and every file that includes `ss_lib.h`:

```bash
-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -DSS_MAX_SLOTS=32 -DSS_MAX_SIGNAL_NAME_LENGTH=32
```

`-DSS_EMBEDDED_BUILD` is a preset for static memory, no threading, 10 slots per signal and a deferred queue of 16 entries.

Fixed limits in static mode:
- Names: `SS_MAX_SIGNAL_NAME_LENGTH` bytes including the terminator (default 32)
- Descriptions: `SS_MAX_DESCRIPTION_LENGTH` bytes including the terminator (default 32); longer ones are rejected with `SS_ERR_WOULD_OVERFLOW`
- String/custom payloads of deferred and batch entries: `SS_DEFERRED_PAYLOAD_SIZE` bytes (default 32); larger ones are rejected with `SS_ERR_WOULD_OVERFLOW`

### Memory Budget

In static mode all of a bus's RAM is inside the bus: a built-in bus is in `.bss`, a pool bus in your pool. Per bus:

| Component | Size |
|-----------|------|
| Signal array | `SS_MAX_SIGNALS` entries, each with a name buffer (`SS_MAX_SIGNAL_NAME_LENGTH`), a description buffer (`SS_MAX_DESCRIPTION_LENGTH`, only with introspection) and a few words of bookkeeping |
| Slot array | `SS_MAX_SLOTS` entries of a few pointers each |
| Deferred queue | `SS_DEFERRED_QUEUE_SIZE` entries, each with a name buffer, an `ss_data_t` and `SS_DEFERRED_PAYLOAD_SIZE` payload bytes |
| Namespace | `SS_MAX_SIGNAL_NAME_LENGTH` bytes |

Besides the buses, the library has one global in RAM: with `SS_ENABLE_ISR_SAFE`, the ISR queue of `SS_ISR_QUEUE_SIZE` entries, each with a name buffer and three ints, shared by every bus.

The deferred queue is often the largest part; reduce `SS_DEFERRED_QUEUE_SIZE` if you do not use deferred emission. For the exact size on your target, `ss_bus_pool_size()` returns the size of a bus plus alignment slack, and `ss_get_memory_stats()` reports the bus's size as `total_bytes_allocated`.

### RAM per bus

Every bus costs the same RAM, so `SS_STATIC_BUSES` (the number of built-in buses, all in `.bss` whether used or not) defaults to 1. A second built-in bus costs the same again; a bus in a pool costs its pool. A program with one bus uses about the same RAM as 2.x.

Measured sizes:

- 64-bit host (macOS arm64), the configuration of `make test_embedded` (`SS_EMBEDDED_BUILD`, 16 signals, 32 slots, memory and performance stats, ISR queue): `ss_bus_pool_size()` is 5663 bytes, including alignment slack.
- Cortex-M (`arm-none-eabi-gcc -Os`, the same on M0 and M4), `sizeof` the bus:

  | Configuration | RAM per bus |
  |---|---|
  | `SS_EMBEDDED_BUILD SS_MINIMAL_BUILD`, 16 signals, 32 slots (the CI Embedded job's M4 build) | 3224 bytes |
  | `SS_EMBEDDED_BUILD SS_MINIMAL_BUILD`, default 32 signals, 128 slots (the CI M0 build) | 6360 bytes |
  | `SS_EMBEDDED_BUILD`, 16 signals, 32 slots, memory stats (as `make test_embedded`, without performance stats) | 3744 bytes |

  The ISR queue adds 704 bytes once, whatever the number of buses. The CI
  job's `.bss` is one built-in bus plus the ISR queue: 3936 bytes for the M4
  build and 7072 for the M0 build.

`ss_data_t` has the same layout in every configuration: 24 bytes on Cortex-M (and other 32-bit Arm targets) and 32 bytes on 64-bit hosts. Each deferred and batch entry holds one. In 2.x it was 16 bytes on Cortex-M without custom data, so a build with `SS_ENABLE_CUSTOM_DATA=0` (such as `SS_MINIMAL_BUILD`) now uses 8 bytes more RAM per deferred entry: 128 bytes per bus with the embedded preset's 16 entries. With custom data on it uses 8 bytes less per entry than 2.x (32 bytes).

### Creating the bus

```c
static ss_bus_t* bus;   /* the application's bus */

if (ss_bus_create(&bus, 0) != SS_OK) {
    /* SS_ERR_WOULD_OVERFLOW: every built-in bus is in use */
}
```

In static mode `ss_bus_create()` takes one of the `SS_STATIC_BUSES` built-in buses and never uses the heap; when all are in use it returns `SS_ERR_WOULD_OVERFLOW`. Claiming and releasing built-in buses is not locked, so create and destroy them from one thread, normally at startup, and create the bus before enabling the interrupts that emit. `ss_bus_destroy(bus)` releases the bus's place for a later `ss_bus_create()`.

`ss_bus_create_in(&bus, pool, size, 0)` places a bus in a buffer you provide instead, in either memory mode: to put it in a specific memory region, to have more buses than `SS_STATIC_BUSES`, or to avoid the heap for the bus in dynamic mode. The buffer needs no particular alignment and at least `ss_bus_pool_size()` bytes; it must outlive the bus, and `ss_bus_destroy()` never frees it. A NULL buffer returns `SS_ERR_NULL_PARAM` and a buffer that is too small `SS_ERR_BUFFER_TOO_SMALL`. Build with `-DSS_STATIC_BUSES=0` to drop the built-in buses when you always use pools.

```c
static unsigned char pool[BUS_POOL_SIZE];   /* >= ss_bus_pool_size() */

if (ss_bus_create_in(&bus, pool, sizeof(pool), 0) == SS_ERR_BUFFER_TOO_SMALL) {
    /* enlarge the pool to ss_bus_pool_size() */
}
```

`ss_bus_pool_size()` is a runtime value, so size the pool from a measurement on your target (for example the size printed by `examples/example_embedded.c`) and keep the check.

Both create macros pass the application's `SS_ABI_TAG`, so creation returns `SS_ERR_CONFIG_MISMATCH` if the application and the library see different `ss_data_t` layouts (a header from another version, or options such as `-fshort-enums` used for only one of them). Feature flags no longer change layouts, but must still match, since they decide which functions are declared.

## ISR-Safe Operations

SS_Lib provides a lock-free path for interrupt service routines: the ISR queues an event, and the main loop delivers it to the slots.

### Configuration

```bash
-DSS_ENABLE_ISR_SAFE=1 -DSS_ISR_QUEUE_SIZE=16   # number of queued ISR events
```

### Usage

```c
void ADC_IRQHandler(void) {
    uint16_t value = ADC->DR;
    ss_emit_from_isr("adc_ready", (int)value);   /* queued; no slot runs here */
}

int main(void) {
    ss_bus_t* bus;
    ss_bus_create(&bus, 0);       /* before enabling the interrupts that emit */
    ss_signal_register(bus, "adc_ready");
    ss_connect(bus, "adc_ready", on_adc_ready, NULL);

    for (;;) {
        ss_process_isr_queue(bus);   /* delivers queued events, oldest first */
        /* ... other work ... */
    }
}
```

Never call `ss_emit()` or other library functions from an ISR; only `ss_emit_from_isr()` is safe there.

### How It Works

`ss_emit_from_isr` claims a free queue entry with an atomic compare-and-swap, copies the name and value into it and marks it ready. No mutex is acquired, no memory is allocated and no slot is called, so it is safe with nested interrupts and from several threads. The queue is global and shared by every bus, so it is usable before a bus exists. `ss_process_isr_queue(bus)` takes a snapshot of the ready entries, frees each one and emits it in queue order, also when an interrupt queues events while it takes the snapshot. Names are resolved against the bus's default namespace at delivery, and the slots of that bus run. Events queued during delivery wait for the next call. Destroying a bus leaves queued events in the queue for the next bus that processes it, so disable the interrupts that emit before `ss_bus_destroy()`.

The atomics come from C11 `<stdatomic.h>`, GCC/Clang `__atomic` builtins or MSVC Interlocked functions. On cores without compare-and-swap (Cortex-M0/M0+, AVR), define `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` to disable and restore interrupts, e.g. in `ss_config.h`. Both must also be compiler memory barriers, so the compiler cannot move the queue's reads and writes out of the critical section; the CMSIS functions below are:

```c
#define SS_ISR_CRITICAL_ENTER() uint32_t ss_irq_ = __get_PRIMASK(); __disable_irq()
#define SS_ISR_CRITICAL_EXIT()  __set_PRIMASK(ss_irq_)
```

### Limitations

- Only supports integer data (to avoid allocation in ISR)
- Queue has fixed depth (`SS_ISR_QUEUE_SIZE`); events stay queued until `ss_process_isr_queue()` runs
- Returns `SS_ERR_WOULD_OVERFLOW` if the queue is full or the name is too long, `SS_ERR_NULL_PARAM` for a NULL or empty name
- Signal name must be shorter than `SS_MAX_SIGNAL_NAME_LENGTH`
- An event for an unregistered signal is dropped; `ss_process_isr_queue()` returns `SS_ERR_NOT_FOUND`

## Minimal Build

Strip all optional features for the smallest footprint:

```bash
-DSS_MINIMAL_BUILD
```

This disables:
- Thread safety (no mutex code)
- Introspection (no signal listing)
- Custom data types
- Performance statistics
- Memory statistics

Minimal build retains: signal register/unregister, connect/disconnect, all emit functions, data handling (int, float, double, string, pointer).

## ARM Cortex-M Example

### STM32 with HAL

```c
/* Library and application compiled with:
 *   -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -DSS_MAX_SLOTS=32
 *   -DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_THREAD_SAFETY=0 -DSS_MINIMAL_BUILD
 */
#include "ss_lib.h"
#include "stm32f4xx_hal.h"

static void on_adc_complete(const ss_data_t* data, void* user_data) {
    int value = ss_data_get_int(data, 0);
    /* Process ADC reading */
    DAC->DHR12R1 = (uint16_t)value;
}

static void on_button_press(const ss_data_t* data, void* user_data) {
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
}

static ss_bus_t* bus;

void system_init(void) {
    ss_bus_create(&bus, 0);   /* a built-in bus: no heap */

    ss_signal_register(bus, "adc_complete");
    ss_signal_register(bus, "button_press");

    ss_connect(bus, "adc_complete", on_adc_complete, NULL);
    ss_connect(bus, "button_press", on_button_press, NULL);
}

/* ISR handlers */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    uint32_t value = HAL_ADC_GetValue(hadc);
    ss_emit_from_isr("adc_complete", (int)value);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == BUTTON_Pin) {
        ss_emit_from_isr("button_press", 0);
    }
}

int main(void) {
    HAL_Init();
    system_init();
    for (;;) {
        ss_process_isr_queue(bus);   /* slots run here, not in the ISRs */
    }
}
```

### Custom Allocator for FreeRTOS

Add to `ss_config.h` (the library source must see these, so defining them in your application file has no effect):

```c
#include "FreeRTOS.h"

#define SS_MALLOC(size)        pvPortMalloc(size)
#define SS_FREE(ptr)           vPortFree(ptr)
#define SS_CALLOC(count, size) pvPortCalloc(count, size)
```

Strings are copied with `SS_MALLOC`, so no `SS_STRDUP` is needed. In static mode these are only used by the functions that return allocated objects (see [Static Memory Mode](#static-memory-mode)).

## Compile-Time Verification

Verify your embedded build compiles cleanly:

```bash
# ARM Cortex-M cross-compilation
arm-none-eabi-gcc -std=c11 -mcpu=cortex-m4 -mthumb \
    -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=16 -DSS_MAX_SLOTS=32 \
    -DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_THREAD_SAFETY=0 \
    -DSS_MINIMAL_BUILD -Iinclude -c src/ss_lib.c -o ss_lib.o

# C89 compatible build
arm-none-eabi-gcc -std=c89 -pedantic \
    -DSS_USE_STATIC_MEMORY=1 -DSS_MINIMAL_BUILD \
    -Iinclude -c src/ss_lib_c89.c -o ss_lib_c89.o
```

On the host, `make test_embedded` builds the embedded examples against a library compiled with `SS_EMBEDDED_BUILD` and runs them.

## Tips

1. **Size signal pools conservatively** — Unused signal/slot entries waste RAM, in every bus; keep `SS_STATIC_BUSES` at the number of buses you create
2. **Use short signal names** — Reduce `SS_MAX_SIGNAL_NAME_LENGTH` to save RAM
3. **Disable unused features** — Every disabled feature reduces code size
4. **Drain the ISR queue often** — The queue has a fixed depth; when it is full, `ss_emit_from_isr()` rejects new events with `SS_ERR_WOULD_OVERFLOW`
5. **Prefer `ss_emit_int`** — Avoids `ss_data_t` construction overhead in tight loops
6. **Check pool sizing with memory stats** — `signals_used` / `slots_used` against `SS_MAX_SIGNALS` / `SS_MAX_SLOTS`; enable temporarily, then disable
