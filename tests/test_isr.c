/*
 * ISR queue tests (C89): each bus has its own queue, and its events keep
 * their order.
 *
 * The implementation is compiled into this file with SS_ISR_CRITICAL_ENTER/
 * EXIT routed to a hook, so a test can "interrupt" ss_process_isr_queue() at
 * an exact point, as a real interrupt could, and queue events from there.
 * Every ordering case runs with two buses: the interrupt queues events on
 * the bus being drained or on the other one.
 *
 * Build: cc -Iinclude -DSS_ENABLE_ISR_SAFE=1 tests/test_isr.c
 *        [-DSS_TEST_IMPL='"../src/ss_lib_c89.c"']
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

void interrupt_hook(void);

#define SS_ISR_CRITICAL_ENTER() ((void)0)
#define SS_ISR_CRITICAL_EXIT() interrupt_hook()

#ifndef SS_TEST_IMPL
#define SS_TEST_IMPL "../src/ss_lib.c"
#endif
#include SS_TEST_IMPL

#include <stdio.h>
/* The asserts are the test: keep them in every build type (CMake's Release
 * configuration defines NDEBUG) */
#undef NDEBUG
#include <assert.h>

#if SS_ENABLE_ISR_SAFE

/* Two buses: A in a caller pool, B from ss_bus_create (the built-in bus in
 * static mode, which has one by default) */
static union {
    unsigned char bytes[sizeof(ss_bus_t) + 64];
    void* align;
} g_pool;
static ss_bus_t* bus_a = NULL;
static ss_bus_t* bus_b = NULL;

/* The hook runs the interrupt when the countdown reaches zero */
static int g_countdown = -1;
static void (*g_interrupt)(void) = NULL;

void interrupt_hook(void) {
    if (g_countdown > 0 && --g_countdown == 0 && g_interrupt) {
        void (*run)(void) = g_interrupt;
        g_interrupt = NULL;
        run();
    }
}

static void interrupt_after(int critical_sections, void (*isr)(void)) {
    g_countdown = critical_sections;
    g_interrupt = isr;
}

/* What each bus delivered, in order. The slot's user_data is the bus's
 * record. */
typedef struct {
    int values[2 * SS_ISR_QUEUE_SIZE + 4];
    int count;
} record_t;

static record_t g_rec_a;
static record_t g_rec_b;

static void record(const ss_data_t* data, void* user_data) {
    record_t* rec = (record_t*)user_data;
    assert(rec->count < (int)(sizeof(rec->values) / sizeof(rec->values[0])));
    rec->values[rec->count++] = ss_data_get_int(data, -1);
}

static ss_bus_t* make_bus(ss_bus_t** out, int pooled, record_t* rec) {
    if (pooled) {
        assert(ss_bus_create_in(out, g_pool.bytes, sizeof(g_pool.bytes), 0) == SS_OK);
    } else {
        assert(ss_bus_create(out, 0) == SS_OK);
    }
    assert(ss_signal_register(*out, "tick") == SS_OK);
    assert(ss_connect(*out, "tick", record, rec) == SS_OK);
    rec->count = 0;
    return *out;
}

static void make_buses(void) {
    make_bus(&bus_a, 1, &g_rec_a);
    make_bus(&bus_b, 0, &g_rec_b);
}

static void destroy_buses(void) {
    ss_bus_destroy(bus_a);
    ss_bus_destroy(bus_b);
    bus_a = bus_b = NULL;
}

static void drain(ss_bus_t* bus) {
    int i;
    for (i = 0; i < 4; i++) {
        assert(ss_process_isr_queue(bus) == SS_OK);
    }
}

/* An event queued on a bus is delivered only by that bus's
 * ss_process_isr_queue(), and the queues fill independently */
static void test_events_stay_on_their_bus(void) {
    int i;
    printf("\n=== Testing ISR events stay on their bus ===\n");
    make_buses();
    assert(ss_emit_from_isr(NULL, "tick", 1) == SS_ERR_NULL_PARAM);

    assert(ss_emit_from_isr(bus_a, "tick", 1) == SS_OK);
    assert(ss_emit_from_isr(bus_b, "tick", 2) == SS_OK);
    assert(ss_emit_from_isr(bus_a, "tick", 3) == SS_OK);

    assert(ss_process_isr_queue(bus_b) == SS_OK);
    assert(g_rec_a.count == 0);
    assert(g_rec_b.count == 1 && g_rec_b.values[0] == 2);

    assert(ss_process_isr_queue(bus_a) == SS_OK);
    assert(g_rec_a.count == 2 && g_rec_a.values[0] == 1 && g_rec_a.values[1] == 3);
    assert(g_rec_b.count == 1);

    /* A full queue on A leaves B's queue empty */
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        assert(ss_emit_from_isr(bus_a, "tick", 100 + i) == SS_OK);
    }
    assert(ss_emit_from_isr(bus_a, "tick", -1) == SS_ERR_WOULD_OVERFLOW);
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        assert(ss_emit_from_isr(bus_b, "tick", 200 + i) == SS_OK);
    }
    assert(ss_emit_from_isr(bus_b, "tick", -1) == SS_ERR_WOULD_OVERFLOW);
    assert(ss_process_isr_queue(bus_b) == SS_OK);
    assert(g_rec_a.count == 2);
    assert(g_rec_b.count == 1 + SS_ISR_QUEUE_SIZE);
    assert(ss_process_isr_queue(bus_a) == SS_OK);
    assert(g_rec_a.count == 2 + SS_ISR_QUEUE_SIZE);
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        assert(g_rec_a.values[2 + i] == 100 + i);
        assert(g_rec_b.values[1 + i] == 200 + i);
    }
    destroy_buses();
    printf("✓ Each bus delivers only its own ISR events\n");
}

/* Destroying a bus drops the events queued on it: a bus created later in
 * the same memory (the same pool, or the same built-in bus in static mode)
 * starts with an empty queue, and the other bus keeps its events */
static void test_destroy_drops_events(void) {
    printf("\n=== Testing bus destruction drops its ISR events ===\n");
    make_buses();
    assert(ss_emit_from_isr(bus_a, "tick", 1) == SS_OK);
    assert(ss_emit_from_isr(bus_b, "tick", 2) == SS_OK);
    ss_bus_destroy(bus_a);

    make_bus(&bus_a, 1, &g_rec_a);
    assert(ss_process_isr_queue(bus_a) == SS_OK);
    assert(g_rec_a.count == 0);
    assert(ss_process_isr_queue(bus_b) == SS_OK);
    assert(g_rec_b.count == 1 && g_rec_b.values[0] == 2);

    assert(ss_emit_from_isr(bus_b, "tick", 3) == SS_OK);
    ss_bus_destroy(bus_b);
    make_bus(&bus_b, 0, &g_rec_b);
    assert(ss_process_isr_queue(bus_b) == SS_OK);
    assert(g_rec_b.count == 0);
    destroy_buses();
    printf("✓ Destroying a bus drops its queued ISR events\n");
}

/* A slot destroys its bus during delivery: the remaining events, and one
 * queued by an interrupt before the destruction completes (the bus memory
 * is still valid then), are dropped, and the other bus is unaffected */
static void slot_destroys_bus(const ss_data_t* data, void* user_data) {
    record(data, user_data);
    ss_bus_destroy(bus_a);
    assert(ss_emit_from_isr(bus_a, "tick", 9) == SS_OK);
}

static void test_destroy_from_slot(void) {
    printf("\n=== Testing ISR events of a bus destroyed by a slot ===\n");
    make_buses();
    assert(ss_disconnect_all(bus_a, "tick") == SS_OK);
    assert(ss_connect(bus_a, "tick", slot_destroys_bus, &g_rec_a) == SS_OK);
    assert(ss_emit_from_isr(bus_a, "tick", 1) == SS_OK);
    assert(ss_emit_from_isr(bus_a, "tick", 2) == SS_OK);
    assert(ss_emit_from_isr(bus_b, "tick", 3) == SS_OK);
    assert(ss_process_isr_queue(bus_a) == SS_OK);   /* bus_a is gone after this */
    assert(g_rec_a.count == 1 && g_rec_a.values[0] == 1);

    make_bus(&bus_a, 1, &g_rec_a);
    assert(ss_process_isr_queue(bus_a) == SS_OK);
    assert(g_rec_a.count == 0);
    assert(ss_process_isr_queue(bus_b) == SS_OK);
    assert(g_rec_b.count == 1 && g_rec_b.values[0] == 3);
    destroy_buses();
    printf("✓ A bus destroyed during delivery drops the rest of its events\n");
}

/* One interrupt queues two events: the first lands in entry 0, the second
 * in entry 1 of the target bus's queue. g_target is the bus it emits on. */
static ss_bus_t** g_target = NULL;

static void isr_two_events(void) {
    assert(ss_emit_from_isr(*g_target, "tick", 1) == SS_OK);
    assert(ss_emit_from_isr(*g_target, "tick", 2) == SS_OK);
}

/* Events queued while ss_process_isr_queue() scans the queue keep their
 * order, wherever the interrupt hits the scan. The interrupt emits on the
 * drained bus (same_bus) or on the other bus, which already holds an older
 * event: the drained bus never delivers the other bus's events, and the
 * other bus delivers its three events in order. */
static void interrupted_scan(int same_bus) {
    int at;
    for (at = 1; at <= 2 * SS_ISR_QUEUE_SIZE; at++) {
        record_t* target;
        make_buses();
        g_target = same_bus ? &bus_a : &bus_b;
        target = same_bus ? &g_rec_a : &g_rec_b;
        assert(ss_emit_from_isr(bus_b, "tick", 0) == SS_OK);

        interrupt_after(at, isr_two_events);
        drain(bus_a);
        assert(g_interrupt == NULL);         /* the interrupt ran */
        g_countdown = -1;
        if (!same_bus) {
            assert(g_rec_a.count == 0);
            assert(g_rec_b.count == 0);
        }
        drain(bus_b);

        assert(g_rec_b.count >= 1 && g_rec_b.values[0] == 0);
        assert(target->count == (same_bus ? 2 : 3));
        if (target->values[target->count - 2] != 1 ||
            target->values[target->count - 1] != 2) {
            printf("interrupt after %d critical sections: delivered %d, %d\n", at,
                   target->values[target->count - 2], target->values[target->count - 1]);
        }
        assert(target->values[target->count - 2] == 1);
        assert(target->values[target->count - 1] == 2);
        if (same_bus) assert(g_rec_b.count == 1);
        destroy_buses();
    }
}

static void test_interrupted_scan_keeps_order(void) {
    printf("\n=== Testing ISR order with an interrupted scan ===\n");
    interrupted_scan(1);
    printf("✓ Interrupts during the scan keep queue order\n");
    interrupted_scan(0);
    printf("✓ Interrupts on another bus during the scan stay on that bus, in order\n");
}

/* An interrupt that queues an event during delivery, while an older event
 * is still queued: the older one is delivered first. The interrupt also
 * queues an event on the other bus, which this delivery leaves alone. */
static void isr_one_event(void) {
    assert(ss_emit_from_isr(bus_a, "tick", 3) == SS_OK);
    assert(ss_emit_from_isr(bus_b, "tick", 4) == SS_OK);
}

static void slot_interrupted(const ss_data_t* data, void* user_data) {
    record(data, user_data);
    if (ss_data_get_int(data, -1) == 1) interrupt_after(1, isr_one_event);
}

static void test_interrupt_during_delivery(void) {
    printf("\n=== Testing ISR order with an interrupt during delivery ===\n");
    make_buses();
    assert(ss_disconnect_all(bus_a, "tick") == SS_OK);
    assert(ss_connect(bus_a, "tick", slot_interrupted, &g_rec_a) == SS_OK);
    assert(ss_emit_from_isr(bus_a, "tick", 1) == SS_OK);
    assert(ss_emit_from_isr(bus_a, "tick", 2) == SS_OK);
    drain(bus_a);
    g_countdown = -1;
    assert(g_rec_a.count == 3);
    assert(g_rec_a.values[0] == 1 && g_rec_a.values[1] == 2 && g_rec_a.values[2] == 3);
    assert(g_rec_b.count == 0);
    assert(ss_process_isr_queue(bus_b) == SS_OK);
    assert(g_rec_b.count == 1 && g_rec_b.values[0] == 4);
    destroy_buses();
    printf("✓ Events queued during delivery come after older ones\n");
}

int main(void) {
    printf("SS_Lib ISR Queue Tests\n");
    printf("======================\n");
    test_events_stay_on_their_bus();
    test_destroy_drops_events();
    test_destroy_from_slot();
    test_interrupted_scan_keeps_order();
    test_interrupt_during_delivery();
    printf("\n✅ All ISR queue tests passed!\n");
    return 0;
}

#else

void interrupt_hook(void) {
}

int main(void) {
    printf("ISR queue disabled (SS_ENABLE_ISR_SAFE=0): nothing to test\n");
    return 0;
}

#endif
