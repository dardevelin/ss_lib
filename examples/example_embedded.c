/* Example: Embedded System with Static Memory Allocation */

/* Configure for embedded use */
#define SS_USE_STATIC_MEMORY 1
#define SS_MAX_SIGNALS 16
#define SS_MAX_SLOTS 32
#define SS_ENABLE_THREAD_SAFETY 0
#define SS_ENABLE_MEMORY_STATS 1
#define SS_ENABLE_PERFORMANCE_STATS 1

#include "ss_lib.h"
#include <stdio.h>
#include <stdint.h>

/* Simulated hardware registers */
static volatile uint16_t ADC_VALUE = 0;
static volatile uint8_t BUTTON_STATE = 0;
static volatile uint32_t TIMER_TICKS = 0;

/* Application state */
typedef struct {
    uint16_t temperature;
    uint16_t threshold;
    uint8_t alarm_active;
    uint32_t alarm_count;
} app_state_t;

static app_state_t g_app_state = {
    .temperature = 0,
    .threshold = 750,  /* 75.0°C */
    .alarm_active = 0,
    .alarm_count = 0
};

/* Signal handlers */
void on_adc_reading(const ss_data_t* data, void* user_data) {
    uint16_t raw_value = ss_data_get_int(data, 0);
    g_app_state.temperature = raw_value / 10;  /* Convert to tenths of degree */
    
    printf("[ADC] Temperature: %u.%u°C\n", 
           g_app_state.temperature / 10, 
           g_app_state.temperature % 10);
}

void on_temp_critical(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    
    g_app_state.alarm_active = 1;
    g_app_state.alarm_count++;
    
    printf("[ALARM] Temperature critical! Count: %lu\n", 
           (unsigned long)g_app_state.alarm_count);
    
    /* In real embedded system, would trigger hardware actions */
    /* GPIO_SetPin(ALARM_LED); */
    /* PWM_SetDutyCycle(COOLING_FAN, 100); */
}

void on_button_pressed(const ss_data_t* data, void* user_data) {
    int button_id = ss_data_get_int(data, 0);
    
    printf("[BUTTON] Button %d pressed\n", button_id);
    
    if (button_id == 1) {
        /* Reset alarm */
        g_app_state.alarm_active = 0;
        printf("[BUTTON] Alarm reset\n");
    }
}

void on_timer_tick(const ss_data_t* data, void* user_data) {
    static uint32_t last_report = 0;
    uint32_t ticks = ss_data_get_int(data, 0);
    
    /* Report every 10 ticks */
    if (ticks - last_report >= 10) {
        ss_memory_stats_t stats;
        ss_get_memory_stats(&stats);
        
        printf("[TIMER] Tick %lu - Signals: %zu/%zu, Slots: %zu/%zu\n",
               (unsigned long)ticks,
               stats.signals_used, stats.signals_allocated,
               stats.slots_used, stats.slots_allocated);
               
        last_report = ticks;
    }
}

/* Simulated ISR handlers */
void ADC_ISR(void) {
    /* In real ISR, would read from hardware register */
    ADC_VALUE = 650 + (TIMER_TICKS % 200);  /* Simulate temperature rise */
    
    /* Emit from ISR - no malloc, no locks */
    ss_emit_from_isr("adc_ready", ADC_VALUE);
}

void TIMER_ISR(void) {
    TIMER_TICKS++;
    
    /* Check temperature threshold */
    if (g_app_state.temperature > g_app_state.threshold) {
        ss_emit_from_isr("temp_critical", 1);
    }
    
    /* Periodic tick */
    if (TIMER_TICKS % 5 == 0) {
        ss_emit_from_isr("timer_tick", TIMER_TICKS);
    }
}

void GPIO_ISR(void) {
    static uint8_t prev_state = 0;
    uint8_t current = BUTTON_STATE;
    
    /* Detect rising edge */
    if (current && !prev_state) {
        ss_emit_from_isr("button_press", current);
    }
    
    prev_state = current;
}

/* Main application */
int main(void) {
    ss_error_t err;
    ss_memory_stats_t mem_stats;
    ss_perf_stats_t perf_stats;
    int i;
    
    printf("=== Embedded Signal-Slot Example ===\n");
    printf("Static memory configuration:\n");
    printf("- Max signals: %d\n", SS_MAX_SIGNALS);
    printf("- Max slots: %d\n", SS_MAX_SLOTS);
    printf("- Thread safety: %s\n", SS_ENABLE_THREAD_SAFETY ? "ON" : "OFF");
    printf("\n");
    
    /* Initialize signal-slot system */
    err = ss_init();
    if (err != SS_OK) {
        printf("Failed to initialize: %s\n", ss_error_string(err));
        return 1;
    }
    
    /* Enable performance profiling */
    ss_enable_profiling(1);
    
    /* Register signals */
    ss_signal_register_ex("adc_ready", "ADC conversion complete", SS_PRIORITY_HIGH);
    ss_signal_register_ex("temp_critical", "Temperature above threshold", SS_PRIORITY_CRITICAL);
    ss_signal_register_ex("button_press", "Button press detected", SS_PRIORITY_NORMAL);
    ss_signal_register_ex("timer_tick", "Periodic timer tick", SS_PRIORITY_LOW);
    
    /* Connect signal handlers */
    ss_connect("adc_ready", on_adc_reading, NULL);
    ss_connect("temp_critical", on_temp_critical, NULL);
    ss_connect("button_press", on_button_pressed, NULL);
    ss_connect("timer_tick", on_timer_tick, NULL);
    
    /* Check initial memory usage */
    ss_get_memory_stats(&mem_stats);
    printf("Initial memory usage:\n");
    printf("- Signals: %zu used, %zu allocated\n", 
           mem_stats.signals_used, mem_stats.signals_allocated);
    printf("- Slots: %zu used, %zu allocated\n",
           mem_stats.slots_used, mem_stats.slots_allocated);
    printf("\n");
    
    /* Simulate embedded system operation */
    printf("Starting simulation...\n\n");
    
    for (i = 0; i < 30; i++) {
        /* Simulate ISRs */
        if (i % 2 == 0) ADC_ISR();
        if (i % 1 == 0) TIMER_ISR();
        if (i == 25) {
            BUTTON_STATE = 1;
            GPIO_ISR();
            BUTTON_STATE = 0;
        }
        
        /* In a real embedded system, we would process ISR queue here */
        /* For this simulation, emit directly instead */
        if (i % 2 == 0) {
            ss_emit_int("adc_ready", ADC_VALUE);
        }
        if (g_app_state.temperature > g_app_state.threshold) {
            ss_emit_int("temp_critical", 1);
        }
        if (i % 5 == 0) {
            ss_emit_int("timer_tick", TIMER_TICKS);
        }
        if (i == 25) {
            ss_emit_int("button_press", 1);
        }
        
        /* Small delay */
        for (volatile int j = 0; j < 10000; j++);
    }
    
    printf("\n=== Performance Statistics ===\n");
    
    /* Show performance stats for each signal */
    const char* signals[] = {"adc_ready", "temp_critical", "timer_tick", "button_press"};
    for (i = 0; i < 4; i++) {
        if (ss_get_perf_stats(signals[i], &perf_stats) == SS_OK) {
            if (perf_stats.total_emissions > 0) {
                printf("Signal '%s':\n", signals[i]);
                printf("  - Emissions: %llu\n", 
                       (unsigned long long)perf_stats.total_emissions);
                printf("  - Avg time: %llu ns\n", 
                       (unsigned long long)perf_stats.avg_time_ns);
                printf("  - Max time: %llu ns\n", 
                       (unsigned long long)perf_stats.max_time_ns);
            }
        }
    }
    
    /* Final memory report */
    ss_get_memory_stats(&mem_stats);
    printf("\n=== Final Memory Report ===\n");
    printf("Signals: %zu/%zu used (%.1f%%)\n", 
           mem_stats.signals_used, mem_stats.signals_allocated,
           100.0 * mem_stats.signals_used / mem_stats.signals_allocated);
    printf("Slots: %zu/%zu used (%.1f%%)\n",
           mem_stats.slots_used, mem_stats.slots_allocated,
           100.0 * mem_stats.slots_used / mem_stats.slots_allocated);
    printf("Total static allocation: ~%zu KB\n", 
           (SS_MAX_SIGNALS * sizeof(void*) * 10 + SS_MAX_SLOTS * sizeof(void*) * 5) / 1024);
    
    /* Cleanup */
    ss_cleanup();
    
    return 0;
}
