/*
 * Randomised re-entrancy stress test (GHSA-f5pm-2465-f895).
 *
 * The bugs in that advisory were slots that changed the library while it
 * still used the memory involved: unregistering a signal that is being
 * emitted, tearing everything down during an emission (ss_bus_destroy()
 * from a slot), queuing a deferred emission during a flush. This test
 * runs many seeded random programs in which slots call those functions,
 * and every other function that changes state, on any signal and up to
 * MAX_DEPTH emissions deep. Every payload
 * carries a check value that the slots verify, and the library's answers
 * are checked for consistency after each step. Built with make test-asan
 * or run under make test-valgrind, any use-after-free on these paths
 * fails the run.
 *
 * The run is deterministic: SEEDS programs of STEPS top-level steps each.
 * A failing seed is printed and can be replayed alone with
 * ./test_reentrancy <seed>.
 */
#include "ss_lib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEEDS 10000
#define STEPS 40
#define MAX_DEPTH 4
#define NAMES 6
#define HANDLES 16

static const char* const g_names[NAMES] = { "a", "b", "c", "ns::d", "e", "f" };

static unsigned long g_rng;
static int g_depth = 0;
static ss_bus_t* bus = NULL;
static int g_destroyed = 0;          /* a slot called ss_bus_destroy(bus) */
static long g_slot_calls = 0;
static long g_payloads_checked = 0;
static ss_connection_t g_handles[HANDLES];

/* xorshift32: the same sequence on every platform */
static unsigned rnd(unsigned n) {
    g_rng ^= (g_rng << 13) & 0xffffffffUL;
    g_rng ^= g_rng >> 17;
    g_rng ^= (g_rng << 5) & 0xffffffffUL;
    return (unsigned)(g_rng % n);
}

static const char* any_name(void) {
    return g_names[rnd(NAMES)];
}

/* String payloads are "<n>:<n>"; a slot that receives anything else got a
 * payload that was overwritten or freed. */
static void make_string(char* buf, size_t size, int n) {
    sprintf(buf, "%d:%d", n, n);
    assert(strlen(buf) < size);
}

static void check_string(const char* s) {
    int a = -1, b = -2;
    char tail;
    assert(s != NULL);
    assert(sscanf(s, "%d:%d%c", &a, &b, &tail) == 2);
    assert(a == b);
    g_payloads_checked++;
}

#if SS_ENABLE_CUSTOM_DATA
typedef struct {
    unsigned magic;
    int n;
    int not_n;
} payload_t;

#define PAYLOAD_MAGIC 0x55AA1234u

static void check_custom(const ss_data_t* data) {
    size_t size = 0;
    const payload_t* p = (const payload_t*)ss_data_get_custom(data, &size);
    assert(p != NULL && size == sizeof(payload_t));
    assert(p->magic == PAYLOAD_MAGIC && p->not_n == ~p->n);
    g_payloads_checked++;
}
#endif

static void step(void);

static void stress_slot(const ss_data_t* data, void* user_data) {
    int i, ops;
    (void)user_data;
    g_slot_calls++;
    switch (data->type) {
    case SS_TYPE_STRING:
        check_string(ss_data_get_string(data));
        break;
#if SS_ENABLE_CUSTOM_DATA
    case SS_TYPE_CUSTOM:
        check_custom(data);
        break;
#endif
    default:
        break;
    }
    if (g_depth >= MAX_DEPTH) return;
    g_depth++;
    ops = (int)rnd(3);
    for (i = 0; i < ops; i++) step();
    g_depth--;
}

static ss_error_t emit_deferred_payload(const char* name) {
    ss_data_t d;
    char buf[24];
    ss_error_t err;
    memset(&d, 0, sizeof(d));
#if SS_ENABLE_CUSTOM_DATA
    if (rnd(2)) {
        payload_t p;
        p.magic = PAYLOAD_MAGIC;
        p.n = (int)rnd(100000);
        p.not_n = ~p.n;
        d.type = SS_TYPE_CUSTOM;
        d.value.custom.ptr = &p;
        d.value.custom.size = sizeof(p);
        err = ss_emit_deferred(bus, name, &d);
        memset(&p, 0xEE, sizeof(p));          /* the queue holds a copy */
        return err;
    }
#endif
    make_string(buf, sizeof(buf), (int)rnd(100000));
    d.type = SS_TYPE_STRING;
    d.value.s_val = buf;
    err = ss_emit_deferred(bus, name, &d);
    memset(buf, 'X', sizeof(buf) - 1);        /* the queue holds a copy */
    return err;
}

static void batch_step(void) {
    ss_batch_t* batch = ss_batch_create(bus);
    ss_data_t d;
    char buf[24];
    if (!batch) return;
    memset(&d, 0, sizeof(d));
    make_string(buf, sizeof(buf), (int)rnd(100000));
    d.type = SS_TYPE_STRING;
    d.value.s_val = buf;
    (void)ss_batch_add(batch, any_name(), &d);
    (void)ss_batch_add(batch, any_name(), NULL);
    memset(buf, 'X', sizeof(buf) - 1);
    (void)ss_batch_emit(batch);
    ss_batch_destroy(batch);
}

/* One random call; from a slot (g_depth > 0) or at the top level */
static void step(void) {
    const char* name = any_name();
    char buf[24];
    switch (rnd(g_depth > 0 ? 17 : 16)) {
    case 0:
        (void)ss_signal_register(bus, name);
        break;
    case 1:
        (void)ss_signal_unregister(bus, name);
        break;
    case 2:
    case 3:
        (void)ss_connect_ex(bus, name, stress_slot, NULL, (ss_priority_t)rnd(16),
                            &g_handles[rnd(HANDLES)]);
        break;
    case 4:
        (void)ss_disconnect(bus, name, stress_slot);
        break;
    case 5:
        (void)ss_disconnect_all(bus, name);
        break;
    case 6:
        (void)ss_disconnect_handle(bus, g_handles[rnd(HANDLES)]);
        break;
    case 7:
        (void)ss_emit_int(bus, name, (int)rnd(1000));
        break;
    case 8:
        make_string(buf, sizeof(buf), (int)rnd(100000));
        (void)ss_emit_string(bus, name, buf);
        break;
    case 9:
    case 10:
        (void)emit_deferred_payload(name);
        break;
    case 11:
        (void)ss_flush_deferred(bus);
        break;
    case 12:
        batch_step();
        break;
    case 13: {
#if SS_ENABLE_INTROSPECTION
        ss_signal_info_t* list = NULL;
        size_t count = 0;
        if (ss_get_signal_list(bus, &list, &count) == SS_OK) {
            size_t i;
            for (i = 0; i < count; i++) assert(list[i].name != NULL);
            ss_free_signal_list(list, count);
        }
#endif
        break;
    }
    case 14:
#if SS_ENABLE_ISR_SAFE
        (void)ss_emit_from_isr(name, (int)rnd(1000));
        (void)ss_process_isr_queue(bus);
#endif
        break;
    case 15:
        (void)ss_emit_void(bus, name);
        break;
    default:                                   /* slots only, rarely */
        if (rnd(8) == 0) {
            ss_bus_destroy(bus);
            g_destroyed = 1;
        }
        break;
    }
}

/* The library's answers agree with each other */
static void check_consistent(void) {
#if SS_ENABLE_INTROSPECTION
    size_t i, live = 0;
    for (i = 0; i < NAMES; i++) {
        if (ss_signal_exists(bus, g_names[i])) live++;
    }
    assert(ss_get_signal_count(bus) == live);
#endif
}

/* After a slot destroyed the bus, which was released when the outermost
 * call returned and must not be used again, continue on a new bus */
static void replace_destroyed_bus(void) {
    if (!g_destroyed) return;
    assert(ss_bus_create(&bus, 0) == SS_OK);
    assert(ss_signal_exists(bus, "a") == 0);
    g_destroyed = 0;
    memset(g_handles, 0, sizeof(g_handles));
}

static void run_program(unsigned long seed) {
    int i;
    g_rng = seed * 2654435761UL + 1;
    g_destroyed = 0;
    memset(g_handles, 0, sizeof(g_handles));
    assert(ss_bus_create(&bus, 0) == SS_OK);
    for (i = 0; i < NAMES; i++) {
        (void)ss_signal_register(bus, g_names[i]);
        (void)ss_connect_ex(bus, g_names[i], stress_slot, NULL, (ss_priority_t)rnd(16), NULL);
        (void)ss_connect_ex(bus, g_names[i], stress_slot, NULL, (ss_priority_t)rnd(16),
                            &g_handles[i]);
    }
    for (i = 0; i < STEPS; i++) {
        step();
        replace_destroyed_bus();
        check_consistent();
    }
    (void)ss_flush_deferred(bus);
    replace_destroyed_bus();
    check_consistent();
    ss_bus_destroy(bus);
}

int main(int argc, char** argv) {
    unsigned long seed, first = 1, last = SEEDS;
    if (argc > 1) first = last = strtoul(argv[1], NULL, 10);
    printf("Re-entrancy stress test (seeds %lu-%lu, %d steps each)\n", first, last, STEPS);
    for (seed = first; seed <= last; seed++) {
        run_program(seed);
    }
    printf("Re-entrancy stress test passed (%ld slot calls, %ld payloads checked)\n",
           g_slot_calls, g_payloads_checked);
    return 0;
}
