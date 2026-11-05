# Embedded Systems Guide

This guide covers using SS_Lib in resource-constrained embedded environments including bare-metal systems, RTOSes, and microcontrollers.

## Static Memory Mode

In static memory mode the library context, signals and slots live in static storage. `ss_init` (unless `SS_STATIC_CONTEXT` is 0), register, connect, emit, deferred emission, namespace, disconnect, unregister and cleanup never touch the heap.

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

In static mode the library's own RAM is in `.bss` (the context is in your pool instead if you use `ss_init_static()` with `SS_STATIC_CONTEXT=0`):

| Component | Size |
|-----------|------|
| Signal array | `SS_MAX_SIGNALS` entries, each with a name buffer (`SS_MAX_SIGNAL_NAME_LENGTH`), a description buffer (`SS_MAX_DESCRIPTION_LENGTH`, only with introspection) and a few words of bookkeeping |
| Slot array | `SS_MAX_SLOTS` entries of a few pointers each |
| Deferred queue | `SS_DEFERRED_QUEUE_SIZE` entries, each with a name buffer, an `ss_data_t` and `SS_DEFERRED_PAYLOAD_SIZE` payload bytes |
| Namespace | `SS_MAX_SIGNAL_NAME_LENGTH` bytes |
| ISR queue (`SS_ENABLE_ISR_SAFE`) | `SS_ISR_QUEUE_SIZE` entries, each with a name buffer and three ints |

The deferred queue is often the largest part; reduce `SS_DEFERRED_QUEUE_SIZE` if you do not use deferred emission. For the exact size on your target, `ss_get_required_pool_size()` returns the size of the context (everything above except the ISR queue) plus alignment slack, and `ss_get_memory_stats()` reports it as `total_bytes_allocated`. For reference, on a 64-bit host the embedded preset with 16 signals and 32 slots needs about 5 KB (about 4 KB with `SS_MINIMAL_BUILD`); 32-bit targets need less.

### Initialization

```c
ss_init();  /* Uses static storage, no heap allocation */
```

`ss_init_static(pool, size)` places the context in a buffer you provide instead (any alignment, at least `ss_get_required_pool_size()` bytes; `ss_cleanup()` never frees it). Use it to put the context in a specific memory region, or to avoid the heap for the context in dynamic mode. A NULL or too small buffer is ignored and the call behaves like `ss_init()` (for 2.1 compatibility), so check `ss_get_required_pool_size()` yourself.

In static mode the built-in context is still compiled in, so a pool alone does not save RAM. Build with `-DSS_STATIC_CONTEXT=0` to drop it when you always pass a pool; `ss_init()` then allocates the context with `SS_CALLOC`.

```c
static unsigned char pool[CONTEXT_POOL_SIZE];   /* >= ss_get_required_pool_size() */

if (ss_get_required_pool_size() > sizeof(pool)) {
    /* enlarge the pool */
}
ss_init_static_abi(pool, sizeof(pool), SS_ABI_TAG);
```

`ss_init_abi(SS_ABI_TAG)` / `ss_init_static_abi(..., SS_ABI_TAG)` return `SS_ERR_CONFIG_MISMATCH` if the application and the library were built with different `ss_data_t` layouts; prefer them in new code.

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
    ss_init();
    ss_signal_register("adc_ready");
    ss_connect("adc_ready", on_adc_ready, NULL);

    for (;;) {
        ss_process_isr_queue();   /* delivers queued events, oldest first */
        /* ... other work ... */
    }
}
```

Never call `ss_emit()` or other library functions from an ISR; only `ss_emit_from_isr()` is safe there.

### How It Works

`ss_emit_from_isr` claims a free queue entry with an atomic compare-and-swap, copies the name and value into it and marks it ready. No mutex is acquired, no memory is allocated and no slot is called, so it is safe with nested interrupts and from several threads, and usable before `ss_init()`. `ss_process_isr_queue()` takes a snapshot of the ready entries, frees each one and emits it in queue order, also when an interrupt queues events while it takes the snapshot. Names are resolved against the default namespace at delivery. Events queued during delivery wait for the next call; `ss_cleanup()` discards queued events.

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

void system_init(void) {
    ss_init();

    ss_signal_register("adc_complete");
    ss_signal_register("button_press");

    ss_connect("adc_complete", on_adc_complete, NULL);
    ss_connect("button_press", on_button_press, NULL);
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
        ss_process_isr_queue();   /* slots run here, not in the ISRs */
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

1. **Size signal pools conservatively** — Unused signal/slot entries waste RAM
2. **Use short signal names** — Reduce `SS_MAX_SIGNAL_NAME_LENGTH` to save RAM
3. **Disable unused features** — Every disabled feature reduces code size
4. **Drain the ISR queue often** — The queue has a fixed depth; when it is full, `ss_emit_from_isr()` rejects new events with `SS_ERR_WOULD_OVERFLOW`
5. **Prefer `ss_emit_int`** — Avoids `ss_data_t` construction overhead in tight loops
6. **Check pool sizing with memory stats** — `signals_used` / `slots_used` against `SS_MAX_SIGNALS` / `SS_MAX_SLOTS`; enable temporarily, then disable
