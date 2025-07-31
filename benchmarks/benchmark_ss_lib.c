#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include "ss_lib.h"

#define BENCHMARK_ITERATIONS 1000000
#define NUM_SLOTS 10
#define NUM_SIGNALS 100

// Timing utilities
static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

typedef struct {
    const char* name;
    uint64_t total_time;
    uint64_t min_time;
    uint64_t max_time;
    int iterations;
} benchmark_result_t;

static void print_result(benchmark_result_t* result) {
    uint64_t avg = result->total_time / result->iterations;
    printf("%-40s: avg=%6llu ns, min=%6llu ns, max=%6llu ns\n",
           result->name, 
           (unsigned long long)avg,
           (unsigned long long)result->min_time,
           (unsigned long long)result->max_time);
}

// Dummy slot functions
static void empty_slot(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
}

static int counter = 0;
static void counting_slot(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    counter++;
}

static void data_slot(const ss_data_t* data, void* user_data) {
    (void)user_data;
    if (data && data->type == SS_TYPE_INT) {
        counter += data->value.i_val;
    }
}

// Benchmarks
static void benchmark_signal_registration(benchmark_result_t* result) {
    result->name = "Signal registration";
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = 1000;
    
    for (int i = 0; i < result->iterations; i++) {
        char signal_name[32];
        snprintf(signal_name, sizeof(signal_name), "bench_signal_%d", i);
        
        uint64_t start = get_time_ns();
        ss_signal_register(signal_name);
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
    }
}

static void benchmark_slot_connection(benchmark_result_t* result) {
    result->name = "Slot connection";
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = 10000;
    
    ss_signal_register("bench_connect");
    
    for (int i = 0; i < result->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_connection_t handle;
        ss_connect_ex("bench_connect", empty_slot, NULL, SS_PRIORITY_NORMAL, &handle);
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
        
        ss_disconnect_handle(handle);
    }
}

static void benchmark_emit_void(benchmark_result_t* result) {
    result->name = "Emit void signal (no slots)";
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = BENCHMARK_ITERATIONS;
    
    ss_signal_register("bench_void");
    
    for (int i = 0; i < result->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_emit_void("bench_void");
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
    }
}

static void benchmark_emit_with_slots(benchmark_result_t* result, int num_slots) {
    char name[64];
    snprintf(name, sizeof(name), "Emit void signal (%d slots)", num_slots);
    result->name = strdup(name);
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = BENCHMARK_ITERATIONS / (num_slots > 0 ? num_slots : 1);
    
    ss_signal_register("bench_slots");
    
    // Connect slots
    for (int i = 0; i < num_slots; i++) {
        ss_connect("bench_slots", empty_slot, NULL);
    }
    
    counter = 0;
    for (int i = 0; i < result->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_emit_void("bench_slots");
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
    }
    
    ss_disconnect_all("bench_slots");
}

static void benchmark_emit_with_data(benchmark_result_t* result) {
    result->name = "Emit int signal (5 slots)";
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = BENCHMARK_ITERATIONS / 5;
    
    ss_signal_register("bench_data");
    
    // Connect slots
    for (int i = 0; i < 5; i++) {
        ss_connect("bench_data", data_slot, NULL);
    }
    
    counter = 0;
    for (int i = 0; i < result->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_emit_int("bench_data", i);
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
    }
    
    ss_disconnect_all("bench_data");
}

static void benchmark_priority_emit(benchmark_result_t* result) {
    result->name = "Emit with priority slots (10 slots)";
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = BENCHMARK_ITERATIONS / 10;
    
    ss_signal_register("bench_priority");
    
    // Connect slots with different priorities
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_LOW, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_CRITICAL, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_NORMAL, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_HIGH, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_NORMAL, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_LOW, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_HIGH, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_CRITICAL, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_NORMAL, NULL);
    ss_connect_ex("bench_priority", empty_slot, NULL, SS_PRIORITY_LOW, NULL);
    
    for (int i = 0; i < result->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_emit_void("bench_priority");
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
    }
    
    ss_disconnect_all("bench_priority");
}

static void benchmark_connection_handle(benchmark_result_t* result) {
    result->name = "Disconnect using handle";
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = 10000;
    
    ss_signal_register("bench_handle");
    
    for (int i = 0; i < result->iterations; i++) {
        ss_connection_t handle;
        ss_connect_ex("bench_handle", empty_slot, NULL, SS_PRIORITY_NORMAL, &handle);
        
        uint64_t start = get_time_ns();
        ss_disconnect_handle(handle);
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
    }
}

static void benchmark_signal_lookup(benchmark_result_t* result) {
    result->name = "Signal existence check";
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = BENCHMARK_ITERATIONS;
    
    // Register many signals to stress the lookup
    for (int i = 0; i < NUM_SIGNALS; i++) {
        char name[32];
        snprintf(name, sizeof(name), "lookup_signal_%d", i);
        ss_signal_register(name);
    }
    
    for (int i = 0; i < result->iterations; i++) {
        int idx = i % NUM_SIGNALS;
        char name[32];
        snprintf(name, sizeof(name), "lookup_signal_%d", idx);
        
        uint64_t start = get_time_ns();
        ss_signal_exists(name);
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
    }
}

#if SS_ENABLE_ISR_SAFE
static void benchmark_isr_emit(benchmark_result_t* result) {
    result->name = "ISR-safe emit (5 slots)";
    result->min_time = UINT64_MAX;
    result->max_time = 0;
    result->total_time = 0;
    result->iterations = BENCHMARK_ITERATIONS / 5;
    
    ss_signal_register("bench_isr");
    
    // Connect slots
    for (int i = 0; i < 5; i++) {
        ss_connect("bench_isr", counting_slot, NULL);
    }
    
    counter = 0;
    for (int i = 0; i < result->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_emit_from_isr("bench_isr", i);
        uint64_t end = get_time_ns();
        
        uint64_t elapsed = end - start;
        result->total_time += elapsed;
        if (elapsed < result->min_time) result->min_time = elapsed;
        if (elapsed > result->max_time) result->max_time = elapsed;
    }
    
    ss_disconnect_all("bench_isr");
}
#endif

int main(void) {
    printf("SS_Lib Benchmark Suite\n");
    printf("======================\n\n");
    
    // Initialize library
    if (ss_init() != SS_OK) {
        fprintf(stderr, "Failed to initialize SS_Lib\n");
        return 1;
    }
    
    printf("Configuration:\n");
#if SS_USE_STATIC_MEMORY
    printf("  Memory: Static (MAX_SIGNALS=%d, MAX_SLOTS=%d)\n", 
           SS_MAX_SIGNALS, SS_MAX_SLOTS);
#else
    printf("  Memory: Dynamic\n");
#endif
#if SS_ENABLE_THREAD_SAFETY
    printf("  Thread Safety: Enabled\n");
#else
    printf("  Thread Safety: Disabled\n");
#endif
#if SS_ENABLE_ISR_SAFE
    printf("  ISR Safe: Enabled\n");
#else
    printf("  ISR Safe: Disabled\n");
#endif
    printf("\n");
    
    // Run benchmarks
    benchmark_result_t results[20];
    int num_results = 0;
    
    printf("Running benchmarks...\n\n");
    
    // Basic operations
    benchmark_signal_registration(&results[num_results++]);
    benchmark_slot_connection(&results[num_results++]);
    benchmark_signal_lookup(&results[num_results++]);
    benchmark_connection_handle(&results[num_results++]);
    
    // Emission benchmarks
    benchmark_emit_void(&results[num_results++]);
    benchmark_emit_with_slots(&results[num_results++], 1);
    benchmark_emit_with_slots(&results[num_results++], 5);
    benchmark_emit_with_slots(&results[num_results++], 10);
    benchmark_emit_with_data(&results[num_results++]);
    benchmark_priority_emit(&results[num_results++]);
    
#if SS_ENABLE_ISR_SAFE
    benchmark_isr_emit(&results[num_results++]);
#endif
    
    // Print results
    printf("Results:\n");
    printf("--------\n");
    for (int i = 0; i < num_results; i++) {
        print_result(&results[i]);
    }
    
    // Memory statistics
#if SS_ENABLE_MEMORY_STATS
    printf("\nMemory Statistics:\n");
    ss_memory_stats_t stats;
    if (ss_get_memory_stats(&stats) == SS_OK) {
        printf("  Signals: %zu used, %zu allocated\n", 
               stats.signals_used, stats.signals_allocated);
        printf("  Slots: %zu used, %zu allocated\n",
               stats.slots_used, stats.slots_allocated);
        printf("  Total memory: %zu bytes\n", stats.total_bytes);
    }
#endif
    
    ss_cleanup();
    
    // Free allocated names (only those created with strdup)
    for (int i = 0; i < num_results; i++) {
        if (strstr(results[i].name, "Emit void signal (") == results[i].name &&
            strcmp(results[i].name, "Emit void signal (no slots)") != 0) {
            free((void*)results[i].name);
        }
    }
    
    return 0;
}