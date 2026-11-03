/* Simple Embedded Example with Static Memory
 *
 * Configuration macros must be identical for the library and every file
 * that includes ss_lib.h, so they are passed on the command line rather
 * than defined here. `make test_embedded` builds this example against a
 * library compiled with:
 *   -DSS_EMBEDDED_BUILD -DSS_MAX_SIGNALS=16 -DSS_MAX_SLOTS=32
 *   -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1 -DSS_ENABLE_ISR_SAFE=1
 */

#include "ss_lib.h"
#include <stdio.h>

static int temperature = 250;  /* 25.0°C */

void on_temp_reading(const ss_data_t* data, void* user_data) {
    int temp = ss_data_get_int(data, 0);
    (void)user_data;
    printf("[SENSOR] Temperature: %d.%d°C\n", temp/10, temp%10);
}

void on_temp_critical(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    printf("[ALARM] Temperature critical!\n");
}

void on_button_press(const ss_data_t* data, void* user_data) {
    int button = ss_data_get_int(data, 0);
    (void)user_data;
    printf("[BUTTON] Button %d pressed - alarm cleared\n", button);
}

static void print_memory(const char* title) {
#if SS_ENABLE_MEMORY_STATS
    ss_memory_stats_t stats;
    if (ss_get_memory_stats(&stats) == SS_OK) {
        printf("%s\n", title);
        printf("- Signals: %zu/%zu used\n", stats.signals_used, stats.signals_allocated);
        printf("- Slots: %zu/%zu used\n\n", stats.slots_used, stats.slots_allocated);
    }
#else
    (void)title;
#endif
}

int main(void) {
    printf("=== Static Memory Signal-Slot Example ===\n");
#if SS_USE_STATIC_MEMORY
    printf("Configuration:\n");
    printf("- Max signals: %d\n", SS_MAX_SIGNALS);
    printf("- Max slots: %d\n", SS_MAX_SLOTS);
#else
    printf("(built with dynamic memory; see the comment at the top)\n");
#endif
    printf("\n");
    
    /* Initialize */
    if (ss_init() != SS_OK) {
        printf("Init failed!\n");
        return 1;
    }
    
    /* Register signals */
    ss_signal_register("temp_reading");
    ss_signal_register("temp_critical");
    ss_signal_register("button_press");
    
    /* Connect handlers */
    ss_connect("temp_reading", on_temp_reading, NULL);
    ss_connect("temp_critical", on_temp_critical, NULL);
    ss_connect("button_press", on_button_press, NULL);
    
    print_memory("Memory usage after setup:");
    
    /* Simulate operation */
    printf("Starting simulation...\n\n");
    
    for (int i = 0; i < 10; i++) {
        /* Temperature rises */
        temperature += 50;
        ss_emit_int("temp_reading", temperature);
        
        /* Check threshold */
        if (temperature > 750) {  /* 75°C */
            ss_emit_void("temp_critical");
        }
        
        /* Button press at i=5 */
        if (i == 5) {
            ss_emit_int("button_press", 1);
            temperature = 250;  /* Reset temp */
        }
    }
    
    printf("\n");
    print_memory("=== Final Memory Report ===");
    
    /* Cleanup */
    ss_cleanup();
    
    return 0;
}