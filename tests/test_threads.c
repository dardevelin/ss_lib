/*
 * Concurrency stress test (POSIX threads). Run under ThreadSanitizer:
 *   make test-tsan
 *
 * Every public operation that touches shared state is hammered from
 * several threads with thread safety enabled, including slots that call
 * back into the library and producers on the ISR queue. Any data race,
 * deadlock, lost or duplicated ISR event fails the run.
 */
#include "ss_lib.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#if SS_ENABLE_THREAD_SAFETY && SS_ENABLE_ISR_SAFE

#define THREADS 8
#define ITERATIONS 3000

static atomic_long g_calls;
static atomic_long g_isr_sent;
static atomic_long g_isr_seen;

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
    assert(ss_signal_exists("shared") == 1);
    (void)ss_emit_deferred("shared", NULL);
    (void)ss_emit_int("shared", 1);
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
                (void)ss_disconnect_handle(*h);
                *h = 0;
            }
            (void)ss_connect_ex("shared", slot_count, NULL,
                                (ss_priority_t)(i % 16), h);
            break;
        case 1:
            (void)ss_emit_int("shared", i);
            break;
        case 2:
            (void)ss_emit_deferred("shared", NULL);
            break;
        case 3:
            (void)ss_flush_deferred();
            break;
        case 4:
            if (ss_signal_register(churn) == SS_OK) {
                (void)ss_connect(churn, slot_count, NULL);
                (void)ss_emit_void(churn);
                (void)ss_signal_unregister(churn);
            }
            break;
        case 5:
            if (ss_emit_from_isr("isr", i) == SS_OK) {
                atomic_fetch_add(&g_isr_sent, 1);
            }
            break;
        case 6:
            (void)ss_process_isr_queue();
            break;
        case 7:
            (void)ss_emit_void("reenter");
            break;
        case 8: {
#if SS_ENABLE_INTROSPECTION
            ss_signal_info_t* list = NULL;
            size_t count = 0;
            if (ss_get_signal_list(&list, &count) == SS_OK) {
                ss_free_signal_list(list, count);
            }
#endif
#if SS_ENABLE_MEMORY_STATS
            ss_memory_stats_t stats;
            (void)ss_get_memory_stats(&stats);
#endif
            break;
        }
        default:
            (void)ss_disconnect("shared", slot_count);
            break;
        }
    }
    for (i = 0; i < 8; i++) {
        if (handles[i]) (void)ss_disconnect_handle(handles[i]);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREADS];
    long i;

    printf("Starting concurrency stress test (%d threads x %d iterations)\n",
           THREADS, ITERATIONS);
    alarm(120);   /* a deadlock fails the run */

    assert(ss_init() == SS_OK);
    ss_set_thread_safe(1);
    assert(ss_signal_register("shared") == SS_OK);
    assert(ss_signal_register("isr") == SS_OK);
    assert(ss_signal_register("reenter") == SS_OK);
    assert(ss_connect("isr", slot_isr, NULL) == SS_OK);
    assert(ss_connect("reenter", slot_reenter, NULL) == SS_OK);

    for (i = 0; i < THREADS; i++) {
        assert(pthread_create(&threads[i], NULL, worker, (void*)(size_t)i) == 0);
    }
    for (i = 0; i < THREADS; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }

    /* Every accepted ISR event is delivered exactly once */
    while (atomic_load(&g_isr_seen) < atomic_load(&g_isr_sent)) {
        assert(ss_process_isr_queue() == SS_OK);
    }
    assert(ss_process_isr_queue() == SS_OK);
    assert(atomic_load(&g_isr_seen) == atomic_load(&g_isr_sent));
    assert(ss_flush_deferred() == SS_OK);

    ss_cleanup();
    printf("Concurrency stress test passed (%ld slot calls, %ld ISR events)\n",
           (long)atomic_load(&g_calls), (long)atomic_load(&g_isr_seen));
    return 0;
}
#else
int main(void) {
    printf("Concurrency stress test skipped (needs thread safety and ISR support)\n");
    return 0;
}
#endif
