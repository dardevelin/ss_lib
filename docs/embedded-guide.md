# Embedded Systems Guide

This guide covers using SS_Lib in resource-constrained embedded environments including bare-metal systems, RTOSes, and microcontrollers.

## Static Memory Mode

In static memory mode each bus holds its signals and slots in fixed arrays, and the bus itself is a built-in bus in static storage or lives in a pool you provide. Bus creation, register, connect, emit, deferred emission, ISR queueing and delivery, namespace, disconnect, unregister and bus destruction never touch the heap.

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
| ISR queue (with `SS_ENABLE_ISR_SAFE`) | `SS_ISR_QUEUE_SIZE` entries, each with a name buffer and three ints (704 bytes with the defaults on 32-bit targets), and a counter |

The library keeps no RAM outside the buses.

The deferred queue is often the largest part; reduce `SS_DEFERRED_QUEUE_SIZE` if you do not use deferred emission. For the exact size on your target, `ss_bus_pool_size()` returns the size of a bus plus alignment slack, and `ss_get_memory_stats()` reports the bus's size as `total_bytes_allocated`.

### RAM per bus

Every bus costs the same RAM, so `SS_STATIC_BUSES` (the number of built-in buses, all in `.bss` whether used or not) defaults to 1. A second built-in bus costs the same again; a bus in a pool costs its pool. A program with one bus uses about the same RAM as 2.x.

Measured sizes:

- 64-bit host (macOS arm64), the configuration of `make test_embedded` (`SS_EMBEDDED_BUILD`, 16 signals, 32 slots, memory and performance stats, ISR queue): `ss_bus_pool_size()` is 6383 bytes, including alignment slack.
- Cortex-M (`arm-none-eabi-gcc -Os`, the same on M0 and M4), `sizeof` the bus, measured as the `.bss` growth from one built-in bus to two:

  | Configuration | RAM per bus | Without the ISR queue |
  |---|---|---|
  | `SS_EMBEDDED_BUILD SS_MINIMAL_BUILD`, 16 signals, 32 slots, ISR queue (the CI Embedded job's M4 build) | 3928 bytes | 3224 bytes |
  | `SS_EMBEDDED_BUILD SS_MINIMAL_BUILD`, default 32 signals, 128 slots, ISR queue (the CI M0 build) | 7064 bytes | 6360 bytes |
  | `SS_EMBEDDED_BUILD`, 16 signals, 32 slots, memory stats, ISR queue (as `make test_embedded`, without performance stats) | 4448 bytes | 3744 bytes |

  The ISR queue is part of each bus: 704 bytes with the defaults (16 entries
  of a 32-byte name and three ints; its counter fills padding). A program
  with one bus uses no more RAM than with the shared queue of 2.x and
  earlier 3.0 builds; each further bus costs the queue again. The CI job's
  `.bss` is the one built-in bus: 3928 bytes for the M4 build and 7064 for
  the M0 build.

`ss_data_t` has the same layout in every configuration: 24 bytes on Cortex-M (and other 32-bit Arm targets) and 32 bytes on 64-bit hosts. Each deferred and batch entry holds one. In 2.x it was 16 bytes on Cortex-M without custom data, so a build with `SS_ENABLE_CUSTOM_DATA=0` (such as `SS_MINIMAL_BUILD`) now uses 8 bytes more RAM per deferred entry: 128 bytes per bus with the embedded preset's 16 entries. With custom data on it uses 8 bytes less per entry than 2.x (32 bytes).

### Creating the bus

```c
static ss_bus_t* bus;   /* the application's bus */

if (ss_bus_create(&bus, 0) != SS_OK) {
    /* SS_ERR_WOULD_OVERFLOW: every built-in bus is in use */
}
```

In static mode `ss_bus_create()` takes one of the `SS_STATIC_BUSES` built-in buses and never uses the heap; when all are in use it returns `SS_ERR_WOULD_OVERFLOW`. Claiming and releasing built-in buses is not locked, so create and destroy them from one thread, normally at startup. Create the bus before enabling the interrupts that emit on it, and disable them before destroying it (see [ISR-Safe Operations](#isr-safe-operations)). `ss_bus_destroy(bus)` releases the bus's place for a later `ss_bus_create()`.

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

SS_Lib provides a lock-free path for interrupt service routines: the ISR queues an event on a bus, and the main loop delivers it to that bus's slots.

### Configuration

```bash
-DSS_ENABLE_ISR_SAFE=1 -DSS_ISR_QUEUE_SIZE=16   # queued ISR events, per bus
```

### Usage

An ISR needs the bus, so keep it in a global that is set before the interrupts that emit are enabled:

```c
static ss_bus_t* bus;   /* set before the ADC interrupt is enabled */

void ADC_IRQHandler(void) {
    uint16_t value = ADC->DR;
    ss_emit_from_isr(bus, "adc_ready", (int)value);   /* queued; no slot runs here */
}

int main(void) {
    ss_bus_create(&bus, 0);
    ss_signal_register(bus, "adc_ready");
    ss_connect(bus, "adc_ready", on_adc_ready, NULL);
    NVIC_EnableIRQ(ADC_IRQn);     /* only once the bus exists */

    for (;;) {
        ss_process_isr_queue(bus);   /* delivers queued events, oldest first */
        /* ... other work ... */
    }
}
```

Never call `ss_emit()` or other library functions from an ISR; only `ss_emit_from_isr()` is safe there.

### The bus and the interrupts

- **Create the bus before enabling the interrupts that emit on it.** `ss_emit_from_isr()` needs a bus; with a NULL bus it returns `SS_ERR_NULL_PARAM` and queues nothing.
- **Disable those interrupts before `ss_bus_destroy()`.** Destruction drops the events still queued on the bus and, for a bus from the heap, frees its memory; an interrupt that emits on the bus during or after the destruction writes into memory that is no longer the bus's. The same holds for threads that call `ss_emit_from_isr()`: stop them first.
- When a slot destroys its bus, the destruction completes when the bus's outermost call returns. Until then the bus memory is valid, so an interrupt that emits on it in that window is harmless: `ss_emit_from_isr()` returns `SS_OK` and the destruction drops the event.

### How It Works

Each bus has its own ISR queue of `SS_ISR_QUEUE_SIZE` entries and its own sequence counter. `ss_emit_from_isr(bus, ...)` claims a free entry of that bus's queue with an atomic compare-and-swap, copies the name and value into it and marks it ready. No mutex is acquired (also on a bus created with `SS_BUS_THREAD_SAFE`), no memory is allocated and no slot is called, so it is safe with nested interrupts, from several threads and during any other call on the bus except `ss_bus_destroy()`. `ss_process_isr_queue(bus)` takes a snapshot of that bus's ready entries, frees each one and emits it in queue order, also when an interrupt queues events while it takes the snapshot. It never delivers another bus's events, and a full queue on one bus does not affect another. Names are resolved against the bus's default namespace at delivery. Events queued during delivery wait for the next call.

The atomics come from C11 `<stdatomic.h>`, GCC/Clang `__atomic` builtins or MSVC Interlocked functions. On cores without compare-and-swap (Cortex-M0/M0+, AVR), define `SS_ISR_CRITICAL_ENTER()` / `SS_ISR_CRITICAL_EXIT()` to disable and restore interrupts, e.g. in `ss_config.h`. Both must also be compiler memory barriers, so the compiler cannot move the queue's reads and writes out of the critical section; the CMSIS functions below are:

```c
#define SS_ISR_CRITICAL_ENTER() uint32_t ss_irq_ = __get_PRIMASK(); __disable_irq()
#define SS_ISR_CRITICAL_EXIT()  __set_PRIMASK(ss_irq_)
```

### Limitations

- Only supports integer data (to avoid allocation in ISR)
- Each bus's queue has a fixed depth (`SS_ISR_QUEUE_SIZE`); events stay queued until `ss_process_isr_queue(bus)` runs on that bus
- Returns `SS_ERR_WOULD_OVERFLOW` if the bus's queue is full or the name is too long, `SS_ERR_NULL_PARAM` for a NULL bus or a NULL or empty name
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

/* ISR handlers: they run only after system_init() has set bus */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    uint32_t value = HAL_ADC_GetValue(hadc);
    ss_emit_from_isr(bus, "adc_complete", (int)value);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == BUTTON_Pin) {
        ss_emit_from_isr(bus, "button_press", 0);
    }
}

int main(void) {
    HAL_Init();
    system_init();   /* creates the bus before the interrupts are enabled */
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
4. **Drain the ISR queue often** — Each bus's queue has a fixed depth; when it is full, `ss_emit_from_isr()` rejects new events with `SS_ERR_WOULD_OVERFLOW`. Every bus has a queue, so with `SS_ENABLE_ISR_SAFE` lower `SS_ISR_QUEUE_SIZE` if buses that receive no ISR events cost too much
5. **Prefer `ss_emit_int`** — Avoids `ss_data_t` construction overhead in tight loops
6. **Check pool sizing with memory stats** — `signals_used` / `slots_used` against `SS_MAX_SIGNALS` / `SS_MAX_SLOTS`; enable temporarily, then disable
