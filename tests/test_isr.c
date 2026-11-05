/*
 * ISR queue ordering tests (C89).
 *
 * The implementation is compiled into this file with SS_ISR_CRITICAL_ENTER/
 * EXIT routed to a hook, so a test can "interrupt" ss_process_isr_queue() at
 * an exact point, as a real interrupt could, and queue events from there.
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

static int g_values[2 * SS_ISR_QUEUE_SIZE];
static int g_count = 0;

static void record(const ss_data_t* data, void* user_data) {
    (void)user_data;
    assert(g_count < (int)(sizeof(g_values) / sizeof(g_values[0])));
    g_values[g_count++] = ss_data_get_int(data, -1);
}

/* One interrupt queues two events: the first lands in entry 0, the second
 * in entry 1 */
static void isr_two_events(void) {
    assert(ss_emit_from_isr("tick", 1) == SS_OK);
    assert(ss_emit_from_isr("tick", 2) == SS_OK);
}

static void drain_all(void) {
    int i;
    for (i = 0; i < 4; i++) {
        assert(ss_process_isr_queue() == SS_OK);
    }
}

/* Events queued while ss_process_isr_queue() scans the queue keep their
 * order, wherever the interrupt hits the scan */
static void test_interrupted_scan_keeps_order(void) {
    int at;
    printf("\n=== Testing ISR order with an interrupted scan ===\n");
    for (at = 1; at <= 2 * SS_ISR_QUEUE_SIZE; at++) {
        assert(ss_init() == SS_OK);
        assert(ss_signal_register("tick") == SS_OK);
        assert(ss_connect("tick", record, NULL) == SS_OK);
        g_count = 0;

        interrupt_after(at, isr_two_events);
        drain_all();
        assert(g_interrupt == NULL);         /* the interrupt ran */
        g_countdown = -1;

        assert(g_count == 2);
        if (g_values[0] != 1 || g_values[1] != 2) {
            printf("interrupt after %d critical sections: delivered %d, %d\n",
                   at, g_values[0], g_values[1]);
        }
        assert(g_values[0] == 1 && g_values[1] == 2);
        ss_cleanup();
    }
    printf("✓ Interrupts during the scan keep queue order\n");
}

/* An interrupt that queues an event during delivery, while an older event
 * is still queued: the older one is delivered first */
static void isr_one_event(void) {
    assert(ss_emit_from_isr("tick", 3) == SS_OK);
}

static void slot_interrupted(const ss_data_t* data, void* user_data) {
    record(data, user_data);
    if (ss_data_get_int(data, -1) == 1) interrupt_after(1, isr_one_event);
}

static void test_interrupt_during_delivery(void) {
    printf("\n=== Testing ISR order with an interrupt during delivery ===\n");
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("tick") == SS_OK);
    assert(ss_connect("tick", slot_interrupted, NULL) == SS_OK);
    g_count = 0;
    assert(ss_emit_from_isr("tick", 1) == SS_OK);
    assert(ss_emit_from_isr("tick", 2) == SS_OK);
    drain_all();
    g_countdown = -1;
    assert(g_count == 3);
    assert(g_values[0] == 1 && g_values[1] == 2 && g_values[2] == 3);
    ss_cleanup();
    printf("✓ Events queued during delivery come after older ones\n");
}

int main(void) {
    printf("SS_Lib ISR Queue Order Tests\n");
    printf("============================\n");
    test_interrupted_scan_keeps_order();
    test_interrupt_during_delivery();
    printf("\n✅ All ISR order tests passed!\n");
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
