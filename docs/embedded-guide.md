# Embedded Systems Guide

This guide covers using SS_Lib in resource-constrained embedded environments including bare-metal systems, RTOSes, and microcontrollers.

## Static Memory Mode

Static memory mode eliminates all dynamic allocation after initialization. Signals and slots are stored in pre-allocated arrays.

### Configuration

```c
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 16       /* adjust to your needs */
#define SS_MAX_SLOTS 32         /* total slots across all signals */
#define SS_MAX_SIGNAL_NAME_LENGTH 32
```

### Memory Budget

Calculate your memory requirements:

| Component | Size |
|-----------|------|
| Signal array | `SS_MAX_SIGNALS * sizeof(ss_signal_t)` (~48 bytes each) |
| Slot array | `SS_MAX_SLOTS * sizeof(ss_slot_t)` (~24 bytes each) |
| Name buffers | `SS_MAX_SIGNALS * SS_MAX_SIGNAL_NAME_LENGTH` |
| Context | ~200 bytes overhead |

Example with 16 signals and 32 slots:
- Signals: 16 * 48 = 768 bytes
- Slots: 32 * 24 = 768 bytes
- Names: 16 * 32 = 512 bytes
- Context: ~200 bytes
- **Total: ~2.1 KB**

### Initialization

```c
ss_init();  /* Uses static arrays, no heap allocation */
```

The `ss_init_static()` function accepts a memory pool parameter for forward compatibility but currently delegates to `ss_init()`.

## ISR-Safe Operations

SS_Lib provides a lock-free emission path for use in interrupt service routines.

### Configuration

```c
#define SS_ENABLE_ISR_SAFE 1
#define SS_ISR_QUEUE_SIZE 16    /* number of queued ISR emissions */
```

### Usage

```c
void ADC_IRQHandler(void) {
    uint16_t value = ADC->DR;
    ss_emit_from_isr("adc_ready", (int)value);
}

/* In main loop, process ISR queue */
void main_loop(void) {
    /* Process pending ISR emissions */
    /* (ISR queue is processed via the standard emit path) */

    ss_emit_int("adc_ready", last_adc_value);
}
```

### How It Works

`ss_emit_from_isr` stores the signal name and integer value in a ring buffer using a compiler write barrier. No mutex is acquired, no memory is allocated. The pending flag uses volatile access for visibility between ISR and main context.

### Limitations

- Only supports integer data (to avoid allocation in ISR)
- Queue has fixed depth (`SS_ISR_QUEUE_SIZE`)
- Returns `SS_ERR_WOULD_OVERFLOW` if queue is full
- Signal name must be shorter than `SS_MAX_SIGNAL_NAME_LENGTH`

## Minimal Build

Strip all optional features for the smallest footprint:

```c
#define SS_MINIMAL_BUILD
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
/* ss_config.h overrides or compiler flags */
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 16
#define SS_MAX_SLOTS 32
#define SS_ENABLE_ISR_SAFE 1
#define SS_ENABLE_THREAD_SAFETY 0
#define SS_MINIMAL_BUILD

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
```

### Custom Allocator for FreeRTOS

```c
#include "FreeRTOS.h"

#define SS_MALLOC(size)        pvPortMalloc(size)
#define SS_FREE(ptr)           vPortFree(ptr)
#define SS_CALLOC(count, size) pvPortCalloc(count, size)
#define SS_STRDUP(str)         port_strdup(str)

static char* port_strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* dup = pvPortMalloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}
```

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

## Tips

1. **Size signal pools conservatively** — Unused signal/slot entries waste RAM
2. **Use short signal names** — Reduce `SS_MAX_SIGNAL_NAME_LENGTH` to save RAM
3. **Disable unused features** — Every disabled feature reduces code size
4. **Use ISR emission sparingly** — The queue has a fixed depth; overflow drops signals
5. **Prefer `ss_emit_int`** — Avoids `ss_data_t` construction overhead in tight loops
6. **Profile with memory stats** — Enable temporarily to verify your pool sizing, then disable
