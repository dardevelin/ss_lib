/*
 * Concurrency stress test (POSIX threads). Run under ThreadSanitizer:
 *   make test-tsan
 *
 * Three phases:
 *   1. one bus created with SS_BUS_THREAD_SAFE, shared by every thread:
 *      every public operation that touches its state is hammered, including
 *      slots that call back into the library and producers on the ISR queue;
 *   2. one bus per thread, created without the flag: buses share no state,
 *      so they need no lock;
 *   3. two thread-safe buses, where slots of the first emit on the second
 *      (cross-bus calls in one direction only) while other threads use the
 *      second directly.
 * Any data race, deadlock, lost or duplicated event fails the run.
 */
#include "ss_lib.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The asserts are the test: keep them in every build type (CMake's Release
 * configuration defines NDEBUG) */
#undef NDEBUG
#include <assert.h>
#include <unistd.h>

#if SS_ENABLE_THREAD_SAFETY && SS_ENABLE_ISR_SAFE

#define THREADS 8
#define ITERATIONS 3000

static ss_bus_t* bus;                /* phase 1: shared by every thread */
static atomic_long g_calls;
static atomic_long g_isr_sent;
static atomic_long g_isr_seen;
#if SS_ENABLE_DEBUG_TRACE
static FILE* g_trace_sink;
#endif

static void slot_count(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    atomic_fetch_add(&g_calls, 1);
}

static void slot_isr(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    atomic_fetch_add(&g_isr_seen, 1);
}

/* Re-enters the library from inside a slot */
static void slot_reenter(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    assert(ss_signal_exists(bus, "shared") == 1);
    (void)ss_emit_deferred(bus, "shared", NULL);
    (void)ss_emit_int(bus, "shared", 1);
}

static void* worker(void* arg) {
    long id = (long)(size_t)arg;
    ss_connection_t handles[8] = {0};
    char churn[32];
    int i;

    snprintf(churn, sizeof(churn), "churn_%ld", id);
    for (i = 0; i < ITERATIONS; i++) {
        ss_connection_t* h = &handles[i % 8];
        switch (i % 10) {
        case 0:
            if (*h) {
                (void)ss_disconnect_handle(bus, *h);
                *h = 0;
            }
            (void)ss_connect_ex(bus, "shared", slot_count, NULL,
                                (ss_priority_t)(i % 16), h);
            break;
        case 1:
            (void)ss_emit_int(bus, "shared", i);
            break;
        case 2:
            (void)ss_emit_deferred(bus, "shared", NULL);
            break;
        case 3:
            (void)ss_flush_deferred(bus);
            break;
        case 4:
            if (ss_signal_register(bus, churn) == SS_OK) {
                (void)ss_connect(bus, churn, slot_count, NULL);
                (void)ss_emit_void(bus, churn);
                (void)ss_signal_unregister(bus, churn);
            }
            break;
        case 5:
            if (ss_emit_from_isr("isr", i) == SS_OK) {
                atomic_fetch_add(&g_isr_sent, 1);
            }
            break;
        case 6:
            (void)ss_process_isr_queue(bus);
            break;
        case 7:
            (void)ss_emit_void(bus, "reenter");
            break;
        case 8: {
#if SS_ENABLE_INTROSPECTION
            ss_signal_info_t* list = NULL;
            size_t count = 0;
            if (ss_get_signal_list(bus, &list, &count) == SS_OK) {
                ss_free_signal_list(list, count);
            }
#endif
#if SS_ENABLE_MEMORY_STATS
            ss_memory_stats_t stats;
            (void)ss_get_memory_stats(bus, &stats);
#endif
#if SS_ENABLE_DEBUG_TRACE
            if (i % 20 == 8) {
                ss_enable_trace(bus, g_trace_sink);
            } else {
                ss_disable_trace(bus);
            }
#endif
            break;
        }
        default:
            (void)ss_disconnect(bus, "shared", slot_count);
            break;
        }
    }
    for (i = 0; i < 8; i++) {
        if (handles[i]) (void)ss_disconnect_handle(bus, handles[i]);
    }
    return NULL;
}

/* Phase 2: a bus of the thread's own. In static mode the built-in buses
 * must not be claimed from several threads at once, so it lives in a pool
 * (ss_bus_create in dynamic mode only calls SS_CALLOC). */
static void slot_add(const ss_data_t* data, void* user_data) {
    *(long*)user_data += ss_data_get_int(data, 0);
}

static void* own_bus_worker(void* arg) {
    ss_bus_t* own = NULL;
    ss_batch_t* batch;
    void* pool = NULL;
    long sum = 0;
    int i;
    (void)arg;
#if SS_USE_STATIC_MEMORY
    pool = malloc(ss_bus_pool_size());
    assert(pool != NULL);
    assert(ss_bus_create_in(&own, pool, ss_bus_pool_size(), 0) == SS_OK);
#else
    assert(ss_bus_create(&own, 0) == SS_OK);
#endif
    /* Every bus uses the same names */
    assert(ss_signal_register(own, "mine") == SS_OK);
    assert(ss_connect(own, "mine", slot_add, &sum) == SS_OK);
    for (i = 0; i < ITERATIONS; i++) {
        ss_data_t one = SS_DATA_INIT;
        assert(ss_data_set_int(&one, 1) == SS_OK);
        assert(ss_emit_int(own, "mine", 1) == SS_OK);
        assert(ss_emit_deferred(own, "mine", &one) == SS_OK);
        if (i % 10 == 0) {
            assert(ss_signal_register(own, "churn") == SS_OK);
            assert(ss_signal_unregister(own, "churn") == SS_OK);
            assert(ss_flush_deferred(own) == SS_OK);
        }
    }
    assert(ss_flush_deferred(own) == SS_OK);
    batch = ss_batch_create(own);
    assert(batch != NULL);
    assert(ss_batch_add(batch, "mine", NULL) == SS_OK);
    assert(ss_batch_emit(batch) == SS_OK);
    ss_batch_destroy(batch);
    ss_bus_destroy(own);
    free(pool);
    assert(sum == 2L * ITERATIONS);
    return NULL;
}

/* Phase 3: slots of g_upstream emit on g_downstream, never the reverse:
 * two threads making cross-bus calls in opposite directions could
 * deadlock, since a slot holds its bus's lock while it calls the other */
static ss_bus_t* g_upstream;
static ss_bus_t* g_downstream;
static atomic_long g_forwarded;
static atomic_long g_direct;
static atomic_long g_sunk;

static void slot_sink(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    atomic_fetch_add(&g_sunk, 1);
}

static void slot_forward(const ss_data_t* data, void* user_data) {
    (void)user_data;
    assert(ss_emit(g_downstream, "sink", data) == SS_OK);
}

static void* cross_worker(void* arg) {
    long id = (long)(size_t)arg;
    int i;
    for (i = 0; i < ITERATIONS; i++) {
        if (id % 2 == 0) {
            assert(ss_emit_int(g_upstream, "forward", i) == SS_OK);
            atomic_fetch_add(&g_forwarded, 1);
        } else {
            ss_connection_t h = 0;
            assert(ss_emit_int(g_downstream, "sink", i) == SS_OK);
            atomic_fetch_add(&g_direct, 1);
            /* A connection that lives only briefly: its slot never runs */
            assert(ss_connect_ex(g_downstream, "other", slot_sink, NULL,
                                 SS_PRIORITY_NORMAL, &h) == SS_OK);
            assert(ss_disconnect_handle(g_downstream, h) == SS_OK);
        }
    }
    return NULL;
}

static void run_threads(void* (*fn)(void*)) {
    pthread_t threads[THREADS];
    long i;
    for (i = 0; i < THREADS; i++) {
        assert(pthread_create(&threads[i], NULL, fn, (void*)(size_t)i) == 0);
    }
    for (i = 0; i < THREADS; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
}

int main(void) {
    void* pool;

    printf("Starting concurrency stress test (%d threads x %d iterations)\n",
           THREADS, ITERATIONS);
    alarm(120);   /* a deadlock fails the run */

    /* Phase 1: one shared bus */
    assert(ss_bus_create(&bus, SS_BUS_THREAD_SAFE) == SS_OK);
#if SS_ENABLE_DEBUG_TRACE
    g_trace_sink = fopen("/dev/null", "w");
    assert(g_trace_sink != NULL);
#endif
    assert(ss_signal_register(bus, "shared") == SS_OK);
    assert(ss_signal_register(bus, "isr") == SS_OK);
    assert(ss_signal_register(bus, "reenter") == SS_OK);
    assert(ss_connect(bus, "isr", slot_isr, NULL) == SS_OK);
    assert(ss_connect(bus, "reenter", slot_reenter, NULL) == SS_OK);

    run_threads(worker);

    /* Every accepted ISR event is delivered exactly once */
    while (atomic_load(&g_isr_seen) < atomic_load(&g_isr_sent)) {
        assert(ss_process_isr_queue(bus) == SS_OK);
    }
    assert(ss_process_isr_queue(bus) == SS_OK);
    assert(atomic_load(&g_isr_seen) == atomic_load(&g_isr_sent));
    assert(ss_flush_deferred(bus) == SS_OK);

    ss_bus_destroy(bus);
#if SS_ENABLE_DEBUG_TRACE
    fclose(g_trace_sink);
#endif
    printf("Shared bus passed (%ld slot calls, %ld ISR events)\n",
           (long)atomic_load(&g_calls), (long)atomic_load(&g_isr_seen));

    /* Phase 2: a bus per thread, no locks */
    run_threads(own_bus_worker);
    printf("Separate buses passed\n");

    /* Phase 3: cross-bus calls in one direction */
    assert(ss_bus_create(&g_upstream, SS_BUS_THREAD_SAFE) == SS_OK);
    pool = malloc(ss_bus_pool_size());
    assert(pool != NULL);
    assert(ss_bus_create_in(&g_downstream, pool, ss_bus_pool_size(), SS_BUS_THREAD_SAFE)
           == SS_OK);
    assert(ss_signal_register(g_upstream, "forward") == SS_OK);
    assert(ss_connect(g_upstream, "forward", slot_forward, NULL) == SS_OK);
    assert(ss_signal_register(g_downstream, "sink") == SS_OK);
    assert(ss_signal_register(g_downstream, "other") == SS_OK);
    assert(ss_connect(g_downstream, "sink", slot_sink, NULL) == SS_OK);

    run_threads(cross_worker);
    assert(atomic_load(&g_sunk) == atomic_load(&g_forwarded) + atomic_load(&g_direct));
    ss_bus_destroy(g_upstream);
    ss_bus_destroy(g_downstream);
    free(pool);
    printf("Cross-bus calls passed (%ld events)\n", (long)atomic_load(&g_sunk));

    printf("Concurrency stress test passed\n");
    return 0;
}
#else
int main(void) {
    printf("Concurrency stress test skipped (needs thread safety and ISR support)\n");
    return 0;
}
#endif
