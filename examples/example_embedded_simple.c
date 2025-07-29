/* Simple Embedded Example with Static Memory */

#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 8
#define SS_MAX_SLOTS 16
#define SS_ENABLE_THREAD_SAFETY 0
#define SS_ENABLE_MEMORY_STATS 1

#include "ss_lib_v2.h"
#include <stdio.h>

static int temperature = 250;  /* 25.0°C */

void on_temp_reading(const ss_data_t* data, void* user_data) {
    int temp = ss_data_get_int(data, 0);
    printf("[SENSOR] Temperature: %d.%d°C\n", temp/10, temp%10);
}

void on_temp_critical(const ss_data_t* data, void* user_data) {
    printf("[ALARM] Temperature critical!\n");
}

void on_button_press(const ss_data_t* data, void* user_data) {
    int button = ss_data_get_int(data, 0);
    printf("[BUTTON] Button %d pressed - alarm cleared\n", button);
}

int main(void) {
    ss_memory_stats_t stats;
    
    printf("=== Static Memory Signal-Slot Example ===\n");
    printf("Configuration:\n");
    printf("- Max signals: %d\n", SS_MAX_SIGNALS);
    printf("- Max slots: %d\n", SS_MAX_SLOTS);
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
    
    /* Check memory usage */
    ss_get_memory_stats(&stats);
    printf("Memory usage after setup:\n");
    printf("- Signals: %zu/%zu\n", stats.signals_used, stats.signals_allocated);
    printf("- Slots: %zu/%zu\n", stats.slots_used, stats.slots_allocated);
    printf("\n");
    
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
    
    printf("\n=== Final Memory Report ===\n");
    ss_get_memory_stats(&stats);
    printf("Signals: %zu/%zu used\n", stats.signals_used, stats.signals_allocated);
    printf("Slots: %zu/%zu used\n", stats.slots_used, stats.slots_allocated);
    
    /* Cleanup */
    ss_cleanup();
    
    return 0;
}