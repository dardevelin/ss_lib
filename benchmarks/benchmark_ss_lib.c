#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L  /* clock_gettime */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include "ss_lib.h"

/* Each benchmark runs on a new bus created by run(), so one benchmark's
 * signals never use up another's capacity in static mode. Every setup call
 * and every timed call is checked, and the emission benchmarks check how
 * many slot calls they delivered: a benchmark that measures failures
 * stops the program instead of printing a number. */
static ss_bus_t* bus = NULL;
static unsigned int bus_flags = 0;   /* 0, or SS_BUS_THREAD_SAFE for [locked bus] */

#define BENCHMARK_ITERATIONS 1000000
#define REGISTRATIONS 1000
#define LOOKUP_SIGNALS 100

/* In static mode a bus holds SS_MAX_SIGNALS signals */
#if SS_USE_STATIC_MEMORY
#define CAPPED(n) ((n) < SS_MAX_SIGNALS ? (n) : SS_MAX_SIGNALS)
#else
#define CAPPED(n) (n)
#endif

static void fail(const char* what, ss_error_t err) {
    fprintf(stderr, "benchmark: %s failed: %s\n", what, ss_error_string(err));
    exit(1);
}

#define CHECK(call) do { ss_error_t check_err_ = (call); \
                         if (check_err_ != SS_OK) fail(#call, check_err_); } while (0)

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

typedef struct {
    char name[64];
    uint64_t total_time;
    uint64_t min_time;
    uint64_t max_time;
    int iterations;
} benchmark_result_t;

static void result_start(benchmark_result_t* r, const char* name, int iterations) {
    snprintf(r->name, sizeof(r->name), "%s%s", name,
             (bus_flags & SS_BUS_THREAD_SAFE) ? " [locked bus]" : "");
    r->total_time = 0;
    r->min_time = UINT64_MAX;
    r->max_time = 0;
    r->iterations = iterations;
}

static void result_add(benchmark_result_t* r, uint64_t start, uint64_t end) {
    uint64_t elapsed = end - start;
    r->total_time += elapsed;
    if (elapsed < r->min_time) r->min_time = elapsed;
    if (elapsed > r->max_time) r->max_time = elapsed;
}

static void print_result(const benchmark_result_t* r) {
    printf("%-40s: avg=%6llu ns, min=%6llu ns, max=%6llu ns\n", r->name,
           (unsigned long long)(r->total_time / (uint64_t)r->iterations),
           (unsigned long long)r->min_time, (unsigned long long)r->max_time);
}

/* Slots. Counters are wide and unsigned: the int benchmark adds up to
 * about 1e11. */
static unsigned long long calls = 0;
static unsigned long long int_sum = 0;

static void empty_slot(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
}

static void counting_slot(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    calls++;
}

static void int_slot(const ss_data_t* data, void* user_data) {
    (void)user_data;
    int_sum += (unsigned long long)ss_data_get_int(data, 0);
    calls++;
}

static void expect_calls(const char* benchmark, unsigned long long expected) {
    if (calls != expected) {
        fprintf(stderr, "benchmark: %s delivered %llu slot calls, expected %llu\n",
                benchmark, calls, expected);
        exit(1);
    }
}

/* Benchmarks: param is the slot count where one applies */
static void bench_registration(benchmark_result_t* r, int param) {
    (void)param;
    result_start(r, "Signal registration", CAPPED(REGISTRATIONS));
    for (int i = 0; i < r->iterations; i++) {
        char signal_name[32];
        snprintf(signal_name, sizeof(signal_name), "bench_signal_%d", i);
        uint64_t start = get_time_ns();
        ss_error_t err = ss_signal_register(bus, signal_name);
        uint64_t end = get_time_ns();
        if (err != SS_OK) fail("ss_signal_register", err);
        result_add(r, start, end);
    }
}

static void bench_connection(benchmark_result_t* r, int param) {
    (void)param;
    result_start(r, "Slot connection", 10000);
    CHECK(ss_signal_register(bus, "bench_connect"));
    for (int i = 0; i < r->iterations; i++) {
        ss_connection_t handle;
        uint64_t start = get_time_ns();
        ss_error_t err = ss_connect_ex(bus, "bench_connect", empty_slot, NULL,
                                       SS_PRIORITY_NORMAL, &handle);
        uint64_t end = get_time_ns();
        if (err != SS_OK) fail("ss_connect_ex", err);
        result_add(r, start, end);
        CHECK(ss_disconnect_handle(bus, handle));
    }
}

static void bench_lookup(benchmark_result_t* r, int param) {
    const int signals = CAPPED(LOOKUP_SIGNALS);
    (void)param;
    result_start(r, "Signal existence check", BENCHMARK_ITERATIONS);
    for (int i = 0; i < signals; i++) {
        char name[32];
        snprintf(name, sizeof(name), "lookup_signal_%d", i);
        CHECK(ss_signal_register(bus, name));
    }
    for (int i = 0; i < r->iterations; i++) {
        char name[32];
        snprintf(name, sizeof(name), "lookup_signal_%d", i % signals);
        uint64_t start = get_time_ns();
        int exists = ss_signal_exists(bus, name);
        uint64_t end = get_time_ns();
        if (!exists) fail("ss_signal_exists", SS_ERR_NOT_FOUND);
        result_add(r, start, end);
    }
}

static void bench_disconnect_handle(benchmark_result_t* r, int param) {
    (void)param;
    result_start(r, "Disconnect using handle", 10000);
    CHECK(ss_signal_register(bus, "bench_handle"));
    for (int i = 0; i < r->iterations; i++) {
        ss_connection_t handle;
        CHECK(ss_connect_ex(bus, "bench_handle", empty_slot, NULL, SS_PRIORITY_NORMAL, &handle));
        uint64_t start = get_time_ns();
        ss_error_t err = ss_disconnect_handle(bus, handle);
        uint64_t end = get_time_ns();
        if (err != SS_OK) fail("ss_disconnect_handle", err);
        result_add(r, start, end);
    }
}

/* Void emission to `slots` counting slots (0: no slots) */
static void bench_emit_void(benchmark_result_t* r, int slots) {
    char name[64];
    if (slots == 0) {
        snprintf(name, sizeof(name), "Emit void signal (no slots)");
    } else {
        snprintf(name, sizeof(name), "Emit void signal (%d slots)", slots);
    }
    result_start(r, name, BENCHMARK_ITERATIONS / (slots > 0 ? slots : 1));
    CHECK(ss_signal_register(bus, "bench_void"));
    for (int i = 0; i < slots; i++) {
        CHECK(ss_connect(bus, "bench_void", counting_slot, NULL));
    }
    calls = 0;
    for (int i = 0; i < r->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_error_t err = ss_emit_void(bus, "bench_void");
        uint64_t end = get_time_ns();
        if (err != SS_OK) fail("ss_emit_void", err);
        result_add(r, start, end);
    }
    expect_calls(r->name, (unsigned long long)r->iterations * (unsigned long long)slots);
}

static void bench_emit_int(benchmark_result_t* r, int slots) {
    unsigned long long n;
    char name[64];
    snprintf(name, sizeof(name), "Emit int signal (%d slots)", slots);
    result_start(r, name, BENCHMARK_ITERATIONS / slots);
    CHECK(ss_signal_register(bus, "bench_data"));
    for (int i = 0; i < slots; i++) {
        CHECK(ss_connect(bus, "bench_data", int_slot, NULL));
    }
    calls = 0;
    int_sum = 0;
    for (int i = 0; i < r->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_error_t err = ss_emit_int(bus, "bench_data", i);
        uint64_t end = get_time_ns();
        if (err != SS_OK) fail("ss_emit_int", err);
        result_add(r, start, end);
    }
    n = (unsigned long long)r->iterations;
    expect_calls(r->name, n * (unsigned long long)slots);
    if (int_sum != (unsigned long long)slots * (n * (n - 1) / 2)) {
        fprintf(stderr, "benchmark: %s: slots received the wrong values\n", r->name);
        exit(1);
    }
}

static void bench_emit_priority(benchmark_result_t* r, int param) {
    static const ss_priority_t priorities[10] = {
        SS_PRIORITY_LOW, SS_PRIORITY_CRITICAL, SS_PRIORITY_NORMAL, SS_PRIORITY_HIGH,
        SS_PRIORITY_NORMAL, SS_PRIORITY_LOW, SS_PRIORITY_HIGH, SS_PRIORITY_CRITICAL,
        SS_PRIORITY_NORMAL, SS_PRIORITY_LOW
    };
    (void)param;
    result_start(r, "Emit with priority slots (10 slots)", BENCHMARK_ITERATIONS / 10);
    CHECK(ss_signal_register(bus, "bench_priority"));
    for (int i = 0; i < 10; i++) {
        CHECK(ss_connect_ex(bus, "bench_priority", counting_slot, NULL, priorities[i], NULL));
    }
    calls = 0;
    for (int i = 0; i < r->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_error_t err = ss_emit_void(bus, "bench_priority");
        uint64_t end = get_time_ns();
        if (err != SS_OK) fail("ss_emit_void", err);
        result_add(r, start, end);
    }
    expect_calls(r->name, (unsigned long long)r->iterations * 10u);
}

#if SS_ENABLE_ISR_SAFE
static void bench_isr_emit(benchmark_result_t* r, int slots) {
    result_start(r, "ISR enqueue (ss_emit_from_isr)", BENCHMARK_ITERATIONS / slots);
    CHECK(ss_signal_register(bus, "bench_isr"));
    for (int i = 0; i < slots; i++) {
        CHECK(ss_connect(bus, "bench_isr", counting_slot, NULL));
    }
    calls = 0;
    for (int i = 0; i < r->iterations; i++) {
        uint64_t start = get_time_ns();
        ss_error_t err = ss_emit_from_isr(bus, "bench_isr", i);
        uint64_t end = get_time_ns();
        if (err != SS_OK) fail("ss_emit_from_isr", err);
        result_add(r, start, end);
        CHECK(ss_process_isr_queue(bus));   /* deliver outside the timed region */
    }
    expect_calls(r->name, (unsigned long long)r->iterations * (unsigned long long)slots);
}
#endif

typedef void (*benchmark_fn)(benchmark_result_t*, int);

static benchmark_result_t results[24];
static int num_results = 0;

static void run(benchmark_fn bench, int param) {
    if (num_results == (int)(sizeof(results) / sizeof(results[0]))) {
        fprintf(stderr, "benchmark: too many results\n");
        exit(1);
    }
    CHECK(ss_bus_create(&bus, bus_flags));
    bench(&results[num_results++], param);
#if SS_ENABLE_MEMORY_STATS
    if (num_results == 1) {   /* the registration benchmark's bus */
        ss_memory_stats_t stats;
        CHECK(ss_get_memory_stats(bus, &stats));
        printf("Memory after %d registrations: %zu signals used, %zu bytes allocated\n\n",
               results[0].iterations, stats.signals_used, stats.total_bytes_allocated);
    }
#endif
    ss_bus_destroy(bus);
    bus = NULL;
}

/* The emission benchmarks, also run on a locked bus */
static void run_emission(void) {
    run(bench_emit_void, 0);
    run(bench_emit_void, 1);
    run(bench_emit_void, 5);
    run(bench_emit_void, 10);
    run(bench_emit_int, 5);
    run(bench_emit_priority, 0);
}

int main(void) {
    printf("SS_Lib Benchmark Suite\n");
    printf("======================\n\n");

    printf("Configuration:\n");
#if SS_USE_STATIC_MEMORY
    printf("  Memory: Static (MAX_SIGNALS=%d, MAX_SLOTS=%d)\n", SS_MAX_SIGNALS, SS_MAX_SLOTS);
#else
    printf("  Memory: Dynamic\n");
#endif
#if SS_ENABLE_THREAD_SAFETY
    printf("  Thread Safety: compiled in; results marked [locked bus] use a bus created\n"
           "                 with SS_BUS_THREAD_SAFE, the others an unlocked bus\n");
#else
    printf("  Thread Safety: not compiled in (every bus is unlocked)\n");
#endif
#if SS_ENABLE_ISR_SAFE
    printf("  ISR Safe: Enabled\n");
#else
    printf("  ISR Safe: Disabled\n");
#endif
    printf("\nRunning benchmarks...\n\n");

    run(bench_registration, 0);
    run(bench_connection, 0);
    run(bench_lookup, 0);
    run(bench_disconnect_handle, 0);
    run_emission();
#if SS_ENABLE_ISR_SAFE
    run(bench_isr_emit, 5);
#endif
#if SS_ENABLE_THREAD_SAFETY
    bus_flags = SS_BUS_THREAD_SAFE;
    run_emission();
    bus_flags = 0;
#endif

    printf("Results:\n");
    printf("--------\n");
    for (int i = 0; i < num_results; i++) {
        print_result(&results[i]);
    }
    return 0;
}
