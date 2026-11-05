#include "ss_lib.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#define TEST_WATCHDOG(seconds) alarm(seconds) /* a deadlock fails the run */
#else
#define TEST_WATCHDOG(seconds) ((void)0)
#endif

static int g_test_counter = 0;

/* The bus of the running test. Tests that need more than one bus, or a
 * bus in a slot's user_data, make their own. */
static ss_bus_t* bus = NULL;

static ss_bus_t* new_bus(void) {
    ss_bus_t* created = NULL;
    assert(ss_bus_create(&created, 0) == SS_OK);
    assert(created != NULL);
    return created;
}

/* A second bus that works in every configuration: in static mode the
 * built-in buses may all be taken, so it lives in a caller pool */
static ss_bus_t* new_pool_bus(void** pool, unsigned int flags) {
    ss_bus_t* created = NULL;
    *pool = malloc(ss_bus_pool_size());
    assert(*pool != NULL);
    assert(ss_bus_create_in(&created, *pool, ss_bus_pool_size(), flags) == SS_OK);
    assert(created != NULL);
    return created;
}

void test_slot_void(const ss_data_t* data, void* user_data) {
    assert(data->type == SS_TYPE_VOID);
    int* counter = (int*)user_data;
    (*counter)++;
    printf("Void signal received, counter = %d\n", *counter);
}

void test_slot_int(const ss_data_t* data, void* user_data) {
    (void)user_data;
    assert(data->type == SS_TYPE_INT);
    int value = ss_data_get_int(data, 0);
    printf("Int signal received: %d\n", value);
    g_test_counter += value;
}

void test_slot_string(const ss_data_t* data, void* user_data) {
    (void)user_data;
    assert(data->type == SS_TYPE_STRING);
    const char* str = ss_data_get_string(data);
    printf("String signal received: %s\n", str ? str : "(null)");
}

void test_basic_functionality(void) {
    printf("\n=== Testing Basic Functionality ===\n");
    
    bus = new_bus();
    
    assert(ss_signal_register(bus, "test_signal") == SS_OK);
    assert(ss_signal_exists(bus, "test_signal") == 1);
    
    assert(ss_signal_register(bus, "test_signal") == SS_ERR_ALREADY_EXISTS);
    
    int counter = 0;
    assert(ss_connect(bus, "test_signal", test_slot_void, &counter) == SS_OK);
    
    assert(ss_emit_void(bus, "test_signal") == SS_OK);
    assert(counter == 1);
    
    assert(ss_emit_void(bus, "test_signal") == SS_OK);
    assert(counter == 2);
    
    assert(ss_disconnect(bus, "test_signal", test_slot_void) == SS_OK);
    assert(ss_emit_void(bus, "test_signal") == SS_OK);
    assert(counter == 2);
    
    assert(ss_signal_unregister(bus, "test_signal") == SS_OK);
    assert(ss_signal_exists(bus, "test_signal") == 0);
    
    ss_bus_destroy(bus);
    printf("Basic functionality tests passed!\n");
}

void test_data_types(void) {
    printf("\n=== Testing Data Types ===\n");
    
    bus = new_bus();
    
    assert(ss_signal_register(bus, "int_signal") == SS_OK);
    assert(ss_signal_register(bus, "string_signal") == SS_OK);
    
    g_test_counter = 0;
    assert(ss_connect(bus, "int_signal", test_slot_int, NULL) == SS_OK);
    assert(ss_connect(bus, "string_signal", test_slot_string, NULL) == SS_OK);
    
    assert(ss_emit_int(bus, "int_signal", 42) == SS_OK);
    assert(g_test_counter == 42);
    
    assert(ss_emit_int(bus, "int_signal", 8) == SS_OK);
    assert(g_test_counter == 50);
    
    assert(ss_emit_string(bus, "string_signal", "Hello, World!") == SS_OK);
    assert(ss_emit_string(bus, "string_signal", NULL) == SS_OK);
    
    ss_data_t* data = ss_data_create(SS_TYPE_DOUBLE);
    assert(data != NULL);
    assert(ss_data_set_double(data, 3.14159) == SS_OK);
    ss_data_destroy(data);
    
    ss_bus_destroy(bus);
    printf("Data type tests passed!\n");
}

void test_multiple_slots(void) {
    printf("\n=== Testing Multiple Slots ===\n");
    
    bus = new_bus();
    
    assert(ss_signal_register(bus, "multi_signal") == SS_OK);
    
    int counter1 = 0, counter2 = 0, counter3 = 0;
    
    assert(ss_connect(bus, "multi_signal", test_slot_void, &counter1) == SS_OK);
    assert(ss_connect(bus, "multi_signal", test_slot_void, &counter2) == SS_OK);
    assert(ss_connect(bus, "multi_signal", test_slot_void, &counter3) == SS_OK);
    
    assert(ss_emit_void(bus, "multi_signal") == SS_OK);
    assert(counter1 == 1 && counter2 == 1 && counter3 == 1);
    
    assert(ss_disconnect_all(bus, "multi_signal") == SS_OK);
    
    assert(ss_emit_void(bus, "multi_signal") == SS_OK);
    assert(counter1 == 1 && counter2 == 1 && counter3 == 1);
    
    ss_bus_destroy(bus);
    printf("Multiple slots tests passed!\n");
}

void test_signal_introspection(void) {
    printf("\n=== Testing Signal Introspection ===\n");
    
#if SS_ENABLE_INTROSPECTION
    bus = new_bus();
    
    assert(ss_signal_register(bus, "signal1") == SS_OK);
    assert(ss_signal_register(bus, "signal2") == SS_OK);
    assert(ss_signal_register(bus, "signal3") == SS_OK);
    
    assert(ss_get_signal_count(bus) == 3);
    
    ss_signal_info_t* list = NULL;
    size_t count = 0;
    
    assert(ss_get_signal_list(bus, &list, &count) == SS_OK);
    assert(count == 3);
    assert(list != NULL);
    
    for (size_t i = 0; i < count; i++) {
        printf("Signal: %s, slots: %zu\n", list[i].name, list[i].slot_count);
    }
    
    ss_free_signal_list(list, count);
    
    ss_bus_destroy(bus);
#else
    printf("Introspection disabled - skipping tests\n");
#endif
    printf("Signal introspection tests passed!\n");
}

void test_error_handling(void) {
    printf("\n=== Testing Error Handling ===\n");
    
    bus = new_bus();
    
    assert(ss_emit_void(bus, "nonexistent") == SS_ERR_NOT_FOUND);
    
    assert(ss_connect(bus, NULL, test_slot_void, NULL) == SS_ERR_NULL_PARAM);
    assert(ss_connect(bus, "test", NULL, NULL) == SS_ERR_NULL_PARAM);
    
    assert(ss_signal_register(bus, NULL) == SS_ERR_NULL_PARAM);
    assert(ss_signal_register(bus, "") == SS_ERR_NULL_PARAM);
    
    assert(ss_disconnect(bus, "nonexistent", test_slot_void) == SS_ERR_NOT_FOUND);
    
    ss_set_max_slots_per_signal(bus, 2);
    assert(ss_signal_register(bus, "limited") == SS_OK);
    assert(ss_connect(bus, "limited", test_slot_void, NULL) == SS_OK);
    assert(ss_connect(bus, "limited", test_slot_void, NULL) == SS_OK);
    assert(ss_connect(bus, "limited", test_slot_void, NULL) == SS_ERR_MAX_SLOTS);
    
    printf("Error: %s\n", ss_error_string(SS_ERR_NOT_FOUND));
    printf("Error: %s\n", ss_error_string(SS_ERR_MEMORY));
    
    ss_bus_destroy(bus);
    printf("Error handling tests passed!\n");
}

/* Globals for disconnect-during-emit test */
static ss_connection_t g_handle_to_disconnect = 0;
static int g_disconnect_slot_called = 0;
static int g_other_slot_called = 0;

void disconnect_other_slot(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    g_disconnect_slot_called++;
    /* Disconnect the other slot during emission */
    ss_disconnect_handle(bus, g_handle_to_disconnect);
}

void other_slot(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    g_other_slot_called++;
}

void test_disconnect_during_emit(void) {
    printf("\n=== Testing Disconnect During Emit ===\n");

    bus = new_bus();

    assert(ss_signal_register(bus, "emit_test") == SS_OK);

    /* Connect disconnect_other_slot at higher priority so it fires first */
    assert(ss_connect_ex(bus, "emit_test", other_slot, NULL,
                         SS_PRIORITY_NORMAL, &g_handle_to_disconnect) == SS_OK);
    ss_connection_t handle1;
    assert(ss_connect_ex(bus, "emit_test", disconnect_other_slot, NULL,
                         SS_PRIORITY_HIGH, &handle1) == SS_OK);

    g_disconnect_slot_called = 0;
    g_other_slot_called = 0;

    /* Emit — first slot disconnects second, but iteration should not crash */
    assert(ss_emit_void(bus, "emit_test") == SS_OK);
    assert(g_disconnect_slot_called == 1);

    /* Emit again — only first slot should fire now */
    g_disconnect_slot_called = 0;
    g_other_slot_called = 0;
    assert(ss_emit_void(bus, "emit_test") == SS_OK);
    assert(g_disconnect_slot_called == 1);
    assert(g_other_slot_called == 0);

    ss_bus_destroy(bus);
    printf("Disconnect during emit tests passed!\n");
}

/* Priority ordering test helpers */
static int g_priority_order[4];
static int g_priority_idx = 0;

void priority_slot_critical(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    g_priority_order[g_priority_idx++] = SS_PRIORITY_CRITICAL;
}

void priority_slot_high(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    g_priority_order[g_priority_idx++] = SS_PRIORITY_HIGH;
}

void priority_slot_normal(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    g_priority_order[g_priority_idx++] = SS_PRIORITY_NORMAL;
}

void priority_slot_low(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    g_priority_order[g_priority_idx++] = SS_PRIORITY_LOW;
}

void test_priority_ordering(void) {
    printf("\n=== Testing Priority Ordering ===\n");

    bus = new_bus();
    assert(ss_signal_register(bus, "prio_test") == SS_OK);

    /* Connect in scrambled order */
    assert(ss_connect_ex(bus, "prio_test", priority_slot_normal, NULL,
                         SS_PRIORITY_NORMAL, NULL) == SS_OK);
    assert(ss_connect_ex(bus, "prio_test", priority_slot_critical, NULL,
                         SS_PRIORITY_CRITICAL, NULL) == SS_OK);
    assert(ss_connect_ex(bus, "prio_test", priority_slot_low, NULL,
                         SS_PRIORITY_LOW, NULL) == SS_OK);
    assert(ss_connect_ex(bus, "prio_test", priority_slot_high, NULL,
                         SS_PRIORITY_HIGH, NULL) == SS_OK);

    g_priority_idx = 0;
    assert(ss_emit_void(bus, "prio_test") == SS_OK);
    assert(g_priority_idx == 4);

    /* Verify execution order: CRITICAL > HIGH > NORMAL > LOW */
    assert(g_priority_order[0] == SS_PRIORITY_CRITICAL);
    assert(g_priority_order[1] == SS_PRIORITY_HIGH);
    assert(g_priority_order[2] == SS_PRIORITY_NORMAL);
    assert(g_priority_order[3] == SS_PRIORITY_LOW);

    ss_bus_destroy(bus);
    printf("Priority ordering tests passed!\n");
}

void test_input_validation(void) {
    printf("\n=== Testing Input Validation ===\n");

    bus = new_bus();

    /* Signal name too long */
    char long_name[300];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(ss_signal_register(bus, long_name) == SS_ERR_WOULD_OVERFLOW);

    /* Custom data with zero size */
#if SS_ENABLE_CUSTOM_DATA
    ss_data_t* data = ss_data_create(SS_TYPE_CUSTOM);
    assert(data != NULL);
    int dummy = 42;
    assert(ss_data_set_custom(data, &dummy, 0, NULL) == SS_ERR_NULL_PARAM);
    ss_data_destroy(data);
#endif

    /* Memory stats: string_bytes should not accumulate across calls */
#if SS_ENABLE_MEMORY_STATS
    assert(ss_signal_register(bus, "stats_test") == SS_OK);

    ss_memory_stats_t stats1, stats2;
    assert(ss_get_memory_stats(bus, &stats1) == SS_OK);
    assert(ss_get_memory_stats(bus, &stats2) == SS_OK);
    assert(stats1.string_bytes == stats2.string_bytes);

    ss_signal_unregister(bus, "stats_test");
#endif

    ss_bus_destroy(bus);
    printf("Input validation tests passed!\n");
}

#if SS_ENABLE_CUSTOM_DATA
static int g_custom_cleanup_called = 0;

void custom_cleanup_fn(const void* data, size_t size) {
    assert(data != NULL && size == sizeof(int) && *(const int*)data == 99);
    g_custom_cleanup_called++;
}

void test_custom_cleanup(void) {
    printf("\n=== Testing Custom Cleanup Callback ===\n");

    bus = new_bus();

    ss_data_t* data = ss_data_create(SS_TYPE_CUSTOM);
    assert(data != NULL);

    int dummy = 99;
    g_custom_cleanup_called = 0;
    assert(ss_data_set_custom(data, &dummy, sizeof(dummy), custom_cleanup_fn) == SS_OK);

    /* Destroying should call our cleanup function */
    ss_data_destroy(data);
    assert(g_custom_cleanup_called == 1);

    ss_bus_destroy(bus);
    printf("Custom cleanup callback tests passed!\n");
}
#endif

/* S4/A9: thread safety is chosen when the bus is created. A library built
 * without thread support refuses the flag instead of ignoring it, and so
 * does any library for a flag it does not know. */
void test_thread_safety_flag(void) {
    ss_bus_t* safe = NULL;
    printf("\n=== Testing the SS_BUS_THREAD_SAFE Flag ===\n");

#if SS_ENABLE_THREAD_SAFETY
    assert(ss_bus_create(&safe, SS_BUS_THREAD_SAFE) == SS_OK);
    assert(safe != NULL);
    assert(ss_signal_register(safe, "thread_test") == SS_OK);
    assert(ss_emit_void(safe, "thread_test") == SS_OK);
    ss_bus_destroy(safe);
#else
    safe = (ss_bus_t*)&safe;                  /* must be reset on failure */
    assert(ss_bus_create(&safe, SS_BUS_THREAD_SAFE) == SS_ERR_CONFIG_MISMATCH);
    assert(safe == NULL);
    {
        void* pool = malloc(ss_bus_pool_size());
        assert(pool != NULL);
        assert(ss_bus_create_in(&safe, pool, ss_bus_pool_size(), SS_BUS_THREAD_SAFE)
               == SS_ERR_CONFIG_MISMATCH);
        assert(safe == NULL);
        free(pool);
    }
#endif
    assert(ss_bus_create(&safe, 0x80u) == SS_ERR_CONFIG_MISMATCH);
    assert(safe == NULL);

    /* A bus that is not thread-safe works as before */
    bus = new_bus();
    assert(ss_signal_register(bus, "thread_test") == SS_OK);
    assert(ss_emit_void(bus, "thread_test") == SS_OK);
    ss_bus_destroy(bus);
    printf("SS_BUS_THREAD_SAFE flag tests passed!\n");
}

#if SS_ENABLE_ISR_SAFE
void test_isr_null_signal(void) {
    printf("\n=== Testing ISR NULL Signal ===\n");

    bus = new_bus();
    assert(ss_emit_from_isr(NULL, 42) == SS_ERR_NULL_PARAM);
    ss_bus_destroy(bus);

    printf("ISR NULL signal tests passed!\n");
}
#endif

void test_namespace(void) {
    printf("\n=== Testing Namespace Support ===\n");

    bus = new_bus();

    /* Register a namespaced signal */
    assert(ss_signal_register(bus, "ui::button") == SS_OK);

    int counter = 0;
    assert(ss_connect(bus, "ui::button", test_slot_void, &counter) == SS_OK);

    /* Emit via namespace helper using a void data payload */
    ss_data_t* ns_data = ss_data_create(SS_TYPE_VOID);
    assert(ns_data != NULL);
    assert(ss_emit_namespaced(bus, "ui", "button", ns_data) == SS_OK);
    assert(counter == 1);
    ss_data_destroy(ns_data);

    /* Set/get namespace */
    assert(ss_set_namespace(bus, "app") == SS_OK);
    assert(strcmp(ss_get_namespace(bus), "app") == 0);

    /* Clear namespace */
    assert(ss_set_namespace(bus, NULL) == SS_OK);
    assert(ss_get_namespace(bus) == NULL);

    /* NULL params */
    assert(ss_emit_namespaced(bus, NULL, "x", NULL) == SS_ERR_NULL_PARAM);
    assert(ss_emit_namespaced(bus, "x", NULL, NULL) == SS_ERR_NULL_PARAM);

    ss_bus_destroy(bus);
    printf("Namespace support tests passed!\n");
}

void test_deferred_emission(void) {
    printf("\n=== Testing Deferred Emission ===\n");

    bus = new_bus();

    assert(ss_signal_register(bus, "deferred1") == SS_OK);
    assert(ss_signal_register(bus, "deferred2") == SS_OK);

    g_test_counter = 0;
    assert(ss_connect(bus, "deferred1", test_slot_int, NULL) == SS_OK);
    assert(ss_connect(bus, "deferred2", test_slot_int, NULL) == SS_OK);

    /* Enqueue 3 deferred emissions */
    ss_data_t* d1 = ss_data_create(SS_TYPE_INT);
    assert(d1 != NULL);
    assert(ss_data_set_int(d1, 10) == SS_OK);
    assert(ss_emit_deferred(bus, "deferred1", d1) == SS_OK);
    ss_data_destroy(d1);

    ss_data_t* d2 = ss_data_create(SS_TYPE_INT);
    assert(d2 != NULL);
    assert(ss_data_set_int(d2, 20) == SS_OK);
    assert(ss_emit_deferred(bus, "deferred2", d2) == SS_OK);
    ss_data_destroy(d2);

    ss_data_t* d3 = ss_data_create(SS_TYPE_INT);
    assert(d3 != NULL);
    assert(ss_data_set_int(d3, 30) == SS_OK);
    assert(ss_emit_deferred(bus, "deferred1", d3) == SS_OK);
    ss_data_destroy(d3);

    /* Counter should be 0 — nothing emitted yet */
    assert(g_test_counter == 0);

    /* Flush — all 3 should fire */
    assert(ss_flush_deferred(bus) == SS_OK);
    assert(g_test_counter == 60);

    ss_bus_destroy(bus);
    printf("Deferred emission tests passed!\n");
}

void test_batch_operations(void) {
    printf("\n=== Testing Batch Operations ===\n");

    bus = new_bus();

    assert(ss_signal_register(bus, "batch_a") == SS_OK);
    assert(ss_signal_register(bus, "batch_b") == SS_OK);

    g_test_counter = 0;
    assert(ss_connect(bus, "batch_a", test_slot_int, NULL) == SS_OK);
    assert(ss_connect(bus, "batch_b", test_slot_int, NULL) == SS_OK);

    ss_batch_t* batch = ss_batch_create(bus);
    assert(batch != NULL);

    ss_data_t* d1 = ss_data_create(SS_TYPE_INT);
    assert(d1 != NULL);
    assert(ss_data_set_int(d1, 5) == SS_OK);
    assert(ss_batch_add(batch, "batch_a", d1) == SS_OK);
    ss_data_destroy(d1);

    ss_data_t* d2 = ss_data_create(SS_TYPE_INT);
    assert(d2 != NULL);
    assert(ss_data_set_int(d2, 15) == SS_OK);
    assert(ss_batch_add(batch, "batch_b", d2) == SS_OK);
    ss_data_destroy(d2);

    ss_data_t* d3 = ss_data_create(SS_TYPE_INT);
    assert(d3 != NULL);
    assert(ss_data_set_int(d3, 25) == SS_OK);
    assert(ss_batch_add(batch, "batch_a", d3) == SS_OK);
    ss_data_destroy(d3);

    /* Nothing emitted until batch_emit */
    assert(g_test_counter == 0);

    assert(ss_batch_emit(batch) == SS_OK);
    assert(g_test_counter == 45);

    ss_batch_destroy(batch);
    ss_bus_destroy(bus);
    printf("Batch operations tests passed!\n");
}

/* ------------------------------------------------------------------ */
/* Regression tests for the 2.2.0 review findings (S* / A* ids)        */
/* ------------------------------------------------------------------ */

/* S1: unregistering a signal from inside one of its own slots */
static void slot_unregister_self(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    assert(ss_signal_unregister(bus, "unreg_sig") == SS_OK);
    assert(ss_signal_exists(bus, "unreg_sig") == 0);
}

void test_unregister_during_emit(void) {
    int after = 0;
    printf("\n=== Testing Unregister During Emit (S1) ===\n");
    bus = new_bus();
    assert(ss_signal_register(bus, "unreg_sig") == SS_OK);
    assert(ss_connect_ex(bus, "unreg_sig", slot_unregister_self, NULL,
                         SS_PRIORITY_HIGH, NULL) == SS_OK);
    assert(ss_connect(bus, "unreg_sig", test_slot_void, &after) == SS_OK);

    assert(ss_emit_void(bus, "unreg_sig") == SS_OK);
    assert(after == 0);                       /* remaining slots are skipped */
    assert(ss_signal_exists(bus, "unreg_sig") == 0);

    /* The name is free again and the new signal starts empty */
    assert(ss_signal_register(bus, "unreg_sig") == SS_OK);
    assert(ss_emit_void(bus, "unreg_sig") == SS_OK);
    assert(after == 0);

    ss_bus_destroy(bus);
    printf("Unregister during emit tests passed!\n");
}

/* S1/A9: ss_bus_destroy() from inside a slot is deferred to the end of the
 * bus's outermost call, and affects only that bus */
typedef struct {
    ss_bus_t* self;        /* the bus whose slot this is */
    ss_bus_t* other;
    int other_calls;
} destroy_ctx_t;

static void slot_count_other(const ss_data_t* data, void* user_data) {
    (void)data;
    ((destroy_ctx_t*)user_data)->other_calls++;
}

static void slot_destroy_own_bus(const ss_data_t* data, void* user_data) {
    destroy_ctx_t* ctx = (destroy_ctx_t*)user_data;
    (void)data;
    ss_bus_destroy(ctx->self);
    /* The bus refuses every call until the outermost one returns */
    assert(ss_signal_exists(ctx->self, "cleanup_sig") == 0);
    assert(ss_emit_void(ctx->self, "cleanup_sig") == SS_ERR_NULL_PARAM);
    assert(ss_signal_register(ctx->self, "late") == SS_ERR_NULL_PARAM);
    assert(ss_batch_create(ctx->self) == NULL);
    ss_bus_destroy(ctx->self);                 /* a second request is harmless */
    /* The other bus, with the same signal name, is untouched */
    assert(ss_signal_exists(ctx->other, "cleanup_sig") == 1);
    assert(ss_emit_void(ctx->other, "cleanup_sig") == SS_OK);
}

void test_destroy_during_emit(void) {
    destroy_ctx_t ctx;
    void* pool;
    int after = 0;
    printf("\n=== Testing Bus Destroy During Emit (S1, A9) ===\n");
    ctx.self = new_bus();
    ctx.other = new_pool_bus(&pool, 0);
    ctx.other_calls = 0;
    assert(ss_signal_register(ctx.self, "cleanup_sig") == SS_OK);
    assert(ss_connect_ex(ctx.self, "cleanup_sig", slot_destroy_own_bus, &ctx,
                         SS_PRIORITY_HIGH, NULL) == SS_OK);
    assert(ss_connect(ctx.self, "cleanup_sig", test_slot_void, &after) == SS_OK);
    assert(ss_signal_register(ctx.other, "cleanup_sig") == SS_OK);
    assert(ss_connect(ctx.other, "cleanup_sig", slot_count_other, &ctx) == SS_OK);

    assert(ss_emit_void(ctx.self, "cleanup_sig") == SS_OK);
    assert(after == 0);                       /* remaining slots are skipped */
    assert(ctx.other_calls == 1);
    /* ctx.self is gone now; the other bus still works */
    assert(ss_emit_void(ctx.other, "cleanup_sig") == SS_OK);
    assert(ctx.other_calls == 2);

    /* Destroy from a slot of a nested emission, and from a flush */
    ctx.self = new_bus();
    assert(ss_signal_register(ctx.self, "cleanup_sig") == SS_OK);
    assert(ss_connect(ctx.self, "cleanup_sig", slot_destroy_own_bus, &ctx) == SS_OK);
    assert(ss_emit_deferred(ctx.self, "cleanup_sig", NULL) == SS_OK);
    assert(ss_emit_deferred(ctx.self, "cleanup_sig", NULL) == SS_OK);  /* dropped */
    assert(ss_flush_deferred(ctx.self) == SS_OK);
    assert(ctx.other_calls == 3);

    ss_bus_destroy(ctx.other);
    ss_bus_destroy(NULL);                     /* no-op */
    free(pool);                               /* the bus never frees its pool */
    printf("Bus destroy during emit tests passed!\n");
}

/* S2: a slot that queues a deferred emission while the queue is flushing */
static char g_seen[4][32];
static int g_seen_count = 0;

static void record_string(const ss_data_t* data) {
    const char* s = ss_data_get_string(data);
    assert(g_seen_count < 4);
    strcpy(g_seen[g_seen_count++], s ? s : "(null)");
}

static void slot_record_and_requeue(const ss_data_t* data, void* user_data) {
    ss_data_t next = {0};
    (void)user_data;
    record_string(data);
    if (g_seen_count == 1) {
        next.type = SS_TYPE_STRING;
        next.value.s_val = "second";
        assert(ss_emit_deferred(bus, "requeue_b", &next) == SS_OK);
    }
}

static void slot_record(const ss_data_t* data, void* user_data) {
    (void)user_data;
    record_string(data);
}

void test_deferred_requeue_during_flush(void) {
    ss_data_t first = {0};
    char payload[8];
    printf("\n=== Testing Deferred Requeue During Flush (S2) ===\n");

    bus = new_bus();
    assert(ss_signal_register(bus, "requeue_a") == SS_OK);
    assert(ss_signal_register(bus, "requeue_b") == SS_OK);
    assert(ss_connect_ex(bus, "requeue_a", slot_record_and_requeue, NULL,
                         SS_PRIORITY_HIGH, NULL) == SS_OK);
    assert(ss_connect(bus, "requeue_a", slot_record, NULL) == SS_OK);
    assert(ss_connect(bus, "requeue_b", slot_record, NULL) == SS_OK);

    /* The queue owns a copy: changing the caller's buffer has no effect */
    strcpy(payload, "first");
    first.type = SS_TYPE_STRING;
    first.value.s_val = payload;
    g_seen_count = 0;
    assert(ss_emit_deferred(bus, "requeue_a", &first) == SS_OK);
    strcpy(payload, "XXXXX");

    assert(ss_flush_deferred(bus) == SS_OK);
    assert(g_seen_count == 2);
    assert(strcmp(g_seen[0], "first") == 0);
    assert(strcmp(g_seen[1], "first") == 0);  /* not overwritten by "second" */

    /* The entry queued during the flush is delivered by the next flush */
    assert(ss_flush_deferred(bus) == SS_OK);
    assert(g_seen_count == 3);
    assert(strcmp(g_seen[2], "second") == 0);

    /* Pending entries are released by ss_bus_destroy without being emitted */
    assert(ss_emit_deferred(bus, "requeue_b", &first) == SS_OK);
    ss_bus_destroy(bus);
    assert(g_seen_count == 3);
    printf("Deferred requeue during flush tests passed!\n");
}

/* S6: deferred and batched custom data are copied, not referenced */
#if SS_ENABLE_CUSTOM_DATA
typedef struct { int a; int b; } test_pair_t;
static test_pair_t g_pair_seen;
static int g_custom_cleanup_on_copy = 0;

static void slot_record_pair(const ss_data_t* data, void* user_data) {
    size_t size = 0;
    test_pair_t* p = (test_pair_t*)ss_data_get_custom(data, &size);
    (void)user_data;
    assert(p != NULL && size == sizeof(test_pair_t));
    g_pair_seen = *p;
}

static void count_cleanup_on_copy(const void* ptr, size_t size) {
    (void)ptr;
    (void)size;
    g_custom_cleanup_on_copy++;
}

void test_deferred_custom_copy(void) {
    test_pair_t pair = {1, 2};
    ss_data_t d = {0};
    ss_batch_t* batch;
    printf("\n=== Testing Deferred Custom Data Copy (S6) ===\n");

    bus = new_bus();
    assert(ss_signal_register(bus, "custom_sig") == SS_OK);
    assert(ss_connect(bus, "custom_sig", slot_record_pair, NULL) == SS_OK);

    d.type = SS_TYPE_CUSTOM;
    d.value.custom.ptr = &pair;
    d.value.custom.size = sizeof(pair);
    d.value.custom.cleanup = count_cleanup_on_copy;

    g_custom_cleanup_on_copy = 0;
    assert(ss_emit_deferred(bus, "custom_sig", &d) == SS_OK);
    pair.a = 99;
    assert(ss_flush_deferred(bus) == SS_OK);
    assert(g_pair_seen.a == 1 && g_pair_seen.b == 2);

    batch = ss_batch_create(bus);
    assert(batch != NULL);
    assert(ss_batch_add(batch, "custom_sig", &d) == SS_OK);
    pair.a = 7;
    assert(ss_batch_emit(batch) == SS_OK);
    assert(g_pair_seen.a == 99);
    ss_batch_destroy(batch);

    /* The caller's cleanup belongs to the caller's object, not our copy */
    assert(g_custom_cleanup_on_copy == 0);
    ss_bus_destroy(bus);
    printf("Deferred custom data copy tests passed!\n");
}

#endif

/* S12: the library frees only payloads it copied itself. A borrowed
 * payload (written by hand, or seen by a slot) is never freed, so reusing
 * such an object with a setter is safe. */
static int g_reused_views = 0;

static void slot_reuse_view(const ss_data_t* data, void* user_data) {
    ss_data_t copy = *data;            /* a slot's data is borrowed */
    (void)user_data;
    assert(ss_data_set_int(&copy, 1) == SS_OK);
    assert(ss_data_get_int(&copy, 0) == 1);
    g_reused_views++;
}

void test_data_ownership(void) {
    char text[8];
    ss_data_t d;
    ss_data_t* created;
    ss_data_t* owned;
    ss_batch_t* batch;
    printf("\n=== Testing Data Ownership (S12) ===\n");

    strcpy(text, "stack");

    /* A setter on a stack object holding a borrowed string frees nothing */
    memset(&d, 0, sizeof(d));
    d.type = SS_TYPE_STRING;
    d.value.s_val = text;
    assert(ss_data_set_int(&d, 5) == SS_OK);
    assert(ss_data_get_int(&d, 0) == 5);
    assert(strcmp(text, "stack") == 0);

    /* The copying setter replaces a borrowed string without freeing it */
    d.type = SS_TYPE_STRING;
    d.value.s_val = text;
    assert(ss_data_set_string(&d, "copy") == SS_OK);
    assert(ss_data_get_string(&d) != text);
    assert(strcmp(ss_data_get_string(&d), "copy") == 0);
    assert(ss_data_set_int(&d, 6) == SS_OK);       /* releases the copy */

    /* A string written by hand on a created object is not the library's */
    created = ss_data_create(SS_TYPE_STRING);
    assert(created != NULL);
    created->value.s_val = text;
    ss_data_destroy(created);

#if SS_ENABLE_CUSTOM_DATA
    {
        test_pair_t pair = {3, 4};
        memset(&d, 0, sizeof(d));
        d.type = SS_TYPE_CUSTOM;
        d.value.custom.ptr = &pair;
        d.value.custom.size = sizeof(pair);
        assert(ss_data_set_double(&d, 1.5) == SS_OK);
        assert(pair.a == 3 && pair.b == 4);
    }
#endif

    /* Slots see borrowed data, also when the emitter's object owns it */
    bus = new_bus();
    assert(ss_signal_register(bus, "reuse") == SS_OK);
    assert(ss_connect(bus, "reuse", slot_reuse_view, NULL) == SS_OK);
    assert(ss_signal_register(bus, "s12::reuse") == SS_OK);
    assert(ss_connect(bus, "s12::reuse", slot_reuse_view, NULL) == SS_OK);
    owned = ss_data_create(SS_TYPE_STRING);
    assert(owned != NULL);
    assert(ss_data_set_string(owned, "owned") == SS_OK);
    g_reused_views = 0;
    assert(ss_emit(bus, "reuse", owned) == SS_OK);
    assert(ss_emit_namespaced(bus, "s12", "reuse", owned) == SS_OK);
    assert(ss_emit_string(bus, "reuse", text) == SS_OK);
    assert(ss_emit_deferred(bus, "reuse", owned) == SS_OK);
    assert(ss_flush_deferred(bus) == SS_OK);
    batch = ss_batch_create(bus);
    assert(batch != NULL);
    assert(ss_batch_add(batch, "reuse", owned) == SS_OK);
    assert(ss_batch_emit(batch) == SS_OK);
    ss_batch_destroy(batch);
    assert(g_reused_views == 5);
    assert(strcmp(ss_data_get_string(owned), "owned") == 0);
    assert(strcmp(text, "stack") == 0);
#if SS_ENABLE_CUSTOM_DATA
    {
        test_pair_t pair = {5, 6};
        assert(ss_data_set_custom(owned, &pair, sizeof(pair), NULL) == SS_OK);
        assert(ss_emit(bus, "reuse", owned) == SS_OK);
        assert(ss_emit_deferred(bus, "reuse", owned) == SS_OK);
        assert(ss_flush_deferred(bus) == SS_OK);
        assert(g_reused_views == 7);
    }
#endif
    ss_data_destroy(owned);
    ss_bus_destroy(bus);
    printf("Data ownership tests passed!\n");
}

/* S12: a borrowing setter given the object's own owned payload keeps it */
void test_data_ref_self_assign(void) {
    ss_data_t d = SS_DATA_INIT;
    ss_data_t* created;
    const char* copy;
    printf("\n=== Testing _ref Setters With the Object's Own Payload ===\n");

    assert(ss_data_set_string(&d, "mine") == SS_OK);
    copy = ss_data_get_string(&d);
    assert(ss_data_set_string_ref(&d, copy) == SS_OK);
    assert(strcmp(ss_data_get_string(&d), "mine") == 0);
    assert(d.type == SS_TYPE_STRING && d.owned != 0 && ss_data_get_string(&d) == copy);
    ss_data_clear(&d);                       /* still owned: freed here, once */

    created = ss_data_create(SS_TYPE_STRING);
    assert(created != NULL);
    assert(ss_data_set_string(created, "created") == SS_OK);
    assert(ss_data_set_string_ref(created, ss_data_get_string(created)) == SS_OK);
    assert(strcmp(ss_data_get_string(created), "created") == 0);
    ss_data_destroy(created);

#if SS_ENABLE_CUSTOM_DATA
    {
        test_pair_t pair = {8, 9};
        size_t size = 0;
        test_pair_t* held;
        assert(ss_data_set_custom(&d, &pair, sizeof(pair), NULL) == SS_OK);
        held = (test_pair_t*)ss_data_get_custom(&d, &size);
        assert(ss_data_set_custom_ref(&d, held, size) == SS_OK);
        held = (test_pair_t*)ss_data_get_custom(&d, &size);
        assert(held->a == 8 && held->b == 9);
        assert(d.type == SS_TYPE_CUSTOM && d.owned != 0);
        assert(held != &pair && size == sizeof(pair));
        ss_data_clear(&d);
    }
#endif
    printf("_ref self-assignment tests passed!\n");
}

#if SS_ENABLE_CUSTOM_DATA
/* Queued custom payloads are aligned for any standard type, like malloc
 * memory: in static mode they live in the library's own queue storage */
typedef struct { char c; long double x; } test_align_ld_t;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(_MSC_VER)
typedef struct { char c; max_align_t x; } test_align_max_t;
#define TEST_PAYLOAD_ALIGN (offsetof(test_align_max_t, x) > offsetof(test_align_ld_t, x) \
                            ? offsetof(test_align_max_t, x) : offsetof(test_align_ld_t, x))
#else
#define TEST_PAYLOAD_ALIGN offsetof(test_align_ld_t, x)
#endif
#ifdef __SIZEOF_INT128__   /* GCC/Clang: malloc memory suits __int128 too */
__extension__ typedef struct { char c; __int128 x; } test_align_i128_t;
#define TEST_ALIGN (offsetof(test_align_i128_t, x) > TEST_PAYLOAD_ALIGN \
                    ? offsetof(test_align_i128_t, x) : TEST_PAYLOAD_ALIGN)
#else
#define TEST_ALIGN TEST_PAYLOAD_ALIGN
#endif

static size_t g_payload_misalignment = 0;
static int g_payload_calls = 0;

static void slot_check_payload_alignment(const ss_data_t* data, void* user_data) {
    size_t size = 0;
    const long double* value = (const long double*)ss_data_get_custom(data, &size);
    (void)user_data;
    assert(value != NULL && size == sizeof(long double));
    g_payload_misalignment += (size_t)((uintptr_t)value % TEST_ALIGN);
    g_payload_calls++;
}

void test_queued_payload_alignment(void) {
    long double value = 1.5L;
    ss_data_t d = {0};
    ss_batch_t* batch;
    printf("\n=== Testing Queued Payload Alignment ===\n");

    bus = new_bus();
    assert(ss_signal_register(bus, "aligned") == SS_OK);
    assert(ss_connect(bus, "aligned", slot_check_payload_alignment, NULL) == SS_OK);
    d.type = SS_TYPE_CUSTOM;
    d.value.custom.ptr = &value;
    d.value.custom.size = sizeof(value);

    g_payload_misalignment = 0;
    g_payload_calls = 0;
    assert(ss_emit_deferred(bus, "aligned", &d) == SS_OK);
    assert(ss_flush_deferred(bus) == SS_OK);
    batch = ss_batch_create(bus);
    assert(batch != NULL);
    assert(ss_batch_add(batch, "aligned", &d) == SS_OK);
    assert(ss_batch_emit(batch) == SS_OK);
    ss_batch_destroy(batch);
    assert(g_payload_calls == 2);
    if (g_payload_misalignment != 0) {
        printf("payload not aligned to %u bytes\n", (unsigned)TEST_ALIGN);
    }
    assert(g_payload_misalignment == 0);
    ss_bus_destroy(bus);
    printf("Queued payload alignment tests passed!\n");
}
#endif

/* S7: ISR queue is drained in order by ss_process_isr_queue(bus) */
#if SS_ENABLE_ISR_SAFE
static int g_isr_values[64];
static int g_isr_count = 0;

static void slot_record_isr(const ss_data_t* data, void* user_data) {
    (void)user_data;
    assert(g_isr_count < 64);
    g_isr_values[g_isr_count++] = ss_data_get_int(data, -1);
}

void test_isr_queue_processing(void) {
    int i;
    char long_name[300];
    printf("\n=== Testing ISR Queue Processing (S7) ===\n");

    bus = new_bus();
    assert(ss_signal_register(bus, "isr_sig") == SS_OK);
    assert(ss_connect(bus, "isr_sig", slot_record_isr, NULL) == SS_OK);

    g_isr_count = 0;
    assert(ss_emit_from_isr("isr_sig", 1) == SS_OK);
    assert(ss_emit_from_isr("isr_sig", 2) == SS_OK);
    assert(ss_emit_from_isr("isr_sig", 3) == SS_OK);
    assert(g_isr_count == 0);                 /* nothing runs in the ISR */

    assert(ss_process_isr_queue(bus) == SS_OK);
    assert(g_isr_count == 3);
    assert(g_isr_values[0] == 1 && g_isr_values[1] == 2 && g_isr_values[2] == 3);

    /* A full queue rejects, and draining makes room again */
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        assert(ss_emit_from_isr("isr_sig", 100 + i) == SS_OK);
    }
    assert(ss_emit_from_isr("isr_sig", -1) == SS_ERR_WOULD_OVERFLOW);
    assert(ss_process_isr_queue(bus) == SS_OK);
    assert(g_isr_count == 3 + SS_ISR_QUEUE_SIZE);
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        assert(g_isr_values[3 + i] == 100 + i);
    }
    assert(ss_emit_from_isr("isr_sig", 5) == SS_OK);
    assert(ss_process_isr_queue(bus) == SS_OK);

    /* S10: over-long names are rejected, never truncated */
    memset(long_name, 'i', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(ss_emit_from_isr(long_name, 1) == SS_ERR_WOULD_OVERFLOW);

    /* Events for unknown signals are dropped and reported */
    assert(ss_emit_from_isr("no_such_signal", 1) == SS_OK);
    assert(ss_process_isr_queue(bus) == SS_ERR_NOT_FOUND);

    ss_bus_destroy(bus);
    assert(ss_process_isr_queue(NULL) == SS_ERR_NULL_PARAM);
    printf("ISR queue processing tests passed!\n");
}
#endif

/* S9: an unregistered signal leaves nothing behind for the next one */
void test_unregister_resets_signal(void) {
    printf("\n=== Testing Unregister Resets Signal (S9) ===\n");
#if SS_ENABLE_INTROSPECTION
    {
        ss_signal_info_t* list = NULL;
        size_t count = 0;
        bus = new_bus();
        assert(ss_signal_register_ex(bus, "s9_old", "old description",
                                     SS_PRIORITY_HIGH) == SS_OK);
        assert(ss_signal_unregister(bus, "s9_old") == SS_OK);
        assert(ss_signal_register(bus, "s9_new") == SS_OK);
        assert(ss_get_signal_list(bus, &list, &count) == SS_OK);
        assert(count == 1);
        assert(strcmp(list[0].name, "s9_new") == 0);
        assert(list[0].description == NULL);
        assert(list[0].priority == SS_PRIORITY_NORMAL);
        ss_free_signal_list(list, count);
        ss_bus_destroy(bus);
    }
#endif
    printf("Unregister resets signal tests passed!\n");
}

/* S10: over-long names are rejected by deferred and batch emission */
void test_name_too_long_rejected(void) {
    char long_name[300];
    ss_batch_t* batch;
    printf("\n=== Testing Over-long Names Rejected (S10) ===\n");

    memset(long_name, 'n', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    bus = new_bus();
    assert(ss_emit_deferred(bus, long_name, NULL) == SS_ERR_WOULD_OVERFLOW);
    batch = ss_batch_create(bus);
    assert(batch != NULL);
    assert(ss_batch_add(batch, long_name, NULL) == SS_ERR_WOULD_OVERFLOW);
    ss_batch_destroy(batch);
    assert(ss_flush_deferred(bus) == SS_OK);
    ss_bus_destroy(bus);
    printf("Over-long names rejected tests passed!\n");
}

/* S11: the signal list owns its strings */
void test_signal_list_owns_strings(void) {
    printf("\n=== Testing Signal List Owns Strings (S11) ===\n");
#if SS_ENABLE_INTROSPECTION
    {
        ss_signal_info_t* list = NULL;
        size_t count = 0;
        bus = new_bus();
        assert(ss_signal_register_ex(bus, "s11", "about s11", SS_PRIORITY_LOW) == SS_OK);
        assert(ss_get_signal_list(bus, &list, &count) == SS_OK);
        assert(ss_signal_unregister(bus, "s11") == SS_OK);
        assert(count == 1);
        assert(strcmp(list[0].name, "s11") == 0);
        assert(strcmp(list[0].description, "about s11") == 0);
        ss_free_signal_list(list, count);
        ss_bus_destroy(bus);
    }
#endif
    printf("Signal list owns strings tests passed!\n");
}

/* S5: slots may call back into the library while thread safety is on */
#if SS_ENABLE_THREAD_SAFETY
static int g_nested_called = 0;

static void slot_nested(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    g_nested_called++;
}

static void slot_reenter(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    assert(ss_signal_exists(bus, "outer") == 1);
    assert(ss_connect(bus, "inner", slot_nested, NULL) == SS_OK);
    assert(ss_emit_void(bus, "inner") == SS_OK);
    assert(ss_emit_deferred(bus, "inner", NULL) == SS_OK);
}

void test_reentrant_calls_with_thread_safety(void) {
    printf("\n=== Testing Re-entrant Calls With Thread Safety (S5) ===\n");
    TEST_WATCHDOG(10);

    assert(ss_bus_create(&bus, SS_BUS_THREAD_SAFE) == SS_OK);
    assert(ss_signal_register(bus, "outer") == SS_OK);
    assert(ss_signal_register(bus, "inner") == SS_OK);
    assert(ss_connect(bus, "outer", slot_reenter, NULL) == SS_OK);

    g_nested_called = 0;
    assert(ss_emit_void(bus, "outer") == SS_OK);
    assert(g_nested_called == 1);
    assert(ss_flush_deferred(bus) == SS_OK);
    assert(g_nested_called == 2);

    ss_bus_destroy(bus);

    /* A slot of a thread-safe bus calls into another thread-safe bus, and
     * destroys its own bus while holding its lock */
    {
        destroy_ctx_t ctx;
        void* pool;
        assert(ss_bus_create(&ctx.self, SS_BUS_THREAD_SAFE) == SS_OK);
        ctx.other = new_pool_bus(&pool, SS_BUS_THREAD_SAFE);
        ctx.other_calls = 0;
        assert(ss_signal_register(ctx.self, "cleanup_sig") == SS_OK);
        assert(ss_connect(ctx.self, "cleanup_sig", slot_destroy_own_bus, &ctx) == SS_OK);
        assert(ss_signal_register(ctx.other, "cleanup_sig") == SS_OK);
        assert(ss_connect(ctx.other, "cleanup_sig", slot_count_other, &ctx) == SS_OK);
        assert(ss_emit_void(ctx.self, "cleanup_sig") == SS_OK);
        assert(ctx.other_calls == 1);
        assert(ss_emit_void(ctx.other, "cleanup_sig") == SS_OK);
        assert(ctx.other_calls == 2);
        ss_bus_destroy(ctx.other);
        free(pool);
    }
    TEST_WATCHDOG(0);
    printf("Re-entrant calls with thread safety tests passed!\n");
}
#endif

/* Disconnect bookkeeping while a signal is emitting */
static ss_connection_t g_self_handle = 0;

/* user_data: counter for the replacement slot */
static void slot_replace_self(const ss_data_t* data, void* user_data) {
    (void)data;
    assert(ss_disconnect_handle(bus, g_self_handle) == SS_OK);
    assert(ss_disconnect_handle(bus, g_self_handle) == SS_ERR_NOT_FOUND);
    /* The limit counts live slots, so the freed place can be reused now */
    assert(ss_connect(bus, "replace_sig", test_slot_void, user_data) == SS_OK);
}

void test_disconnect_bookkeeping_during_emit(void) {
    int replacement = 0;
    printf("\n=== Testing Disconnect Bookkeeping During Emit ===\n");
    bus = new_bus();
    ss_set_max_slots_per_signal(bus, 1);
    assert(ss_signal_register(bus, "replace_sig") == SS_OK);
    assert(ss_connect_ex(bus, "replace_sig", slot_replace_self, &replacement,
                         SS_PRIORITY_NORMAL, &g_self_handle) == SS_OK);
    assert(ss_emit_void(bus, "replace_sig") == SS_OK);
    assert(replacement == 0);                 /* connected after the cursor */
    assert(ss_emit_void(bus, "replace_sig") == SS_OK);
    assert(replacement == 1);
    ss_bus_destroy(bus);
    printf("Disconnect bookkeeping during emit tests passed!\n");
}

/* Batches can be extended or destroyed from their own slots */
static ss_batch_t* g_batch = NULL;
static int g_batch_slot_calls = 0;

static void slot_extend_batch(const ss_data_t* data, void* user_data) {
    (void)user_data;
    g_batch_slot_calls++;
    if (ss_data_get_int(data, 0) == 1) {
        ss_data_t two = {0};
        two.type = SS_TYPE_INT;
        two.value.i_val = 2;
        assert(ss_batch_add(g_batch, "batch_sig", &two) == SS_OK);
    } else if (ss_data_get_int(data, 0) == 3) {
        ss_batch_destroy(g_batch);   /* deferred until the emit returns */
        g_batch = NULL;
    }
}

void test_batch_reentrancy(void) {
    ss_data_t d = {0};
    printf("\n=== Testing Batch Re-entrancy ===\n");
    bus = new_bus();
    assert(ss_signal_register(bus, "batch_sig") == SS_OK);
    assert(ss_connect(bus, "batch_sig", slot_extend_batch, NULL) == SS_OK);

    g_batch = ss_batch_create(bus);
    assert(g_batch != NULL);
    d.type = SS_TYPE_INT;
    d.value.i_val = 1;
    assert(ss_batch_add(g_batch, "batch_sig", &d) == SS_OK);
    g_batch_slot_calls = 0;
    assert(ss_batch_emit(g_batch) == SS_OK);
    assert(g_batch_slot_calls == 1);          /* added entry waits */
    assert(ss_batch_emit(g_batch) == SS_OK);
    assert(g_batch_slot_calls == 2);

    d.value.i_val = 3;
    assert(ss_batch_add(g_batch, "batch_sig", &d) == SS_OK);
    assert(ss_batch_add(g_batch, "batch_sig", &d) == SS_OK);
    assert(ss_batch_emit(g_batch) == SS_OK);  /* destroyed by its own slot */
    assert(g_batch == NULL);
    assert(g_batch_slot_calls == 3);          /* remaining entry discarded */

    ss_bus_destroy(bus);
    printf("Batch re-entrancy tests passed!\n");
}

/* A6: the default namespace qualifies unqualified names */
void test_default_namespace(void) {
    char long_ns[300];
    int calls = 0;
    ss_data_t none = {0};                     /* SS_TYPE_VOID */
    printf("\n=== Testing Default Namespace (A6) ===\n");
    bus = new_bus();

    assert(ss_set_namespace(bus, "app") == SS_OK);
    assert(ss_signal_register(bus, "click") == SS_OK);
    assert(ss_signal_exists(bus, "app::click") == 1);
    assert(ss_signal_exists(bus, "click") == 1);
    assert(ss_connect(bus, "click", test_slot_void, &calls) == SS_OK);

    assert(ss_emit_void(bus, "click") == SS_OK);
    assert(ss_emit_void(bus, "app::click") == SS_OK);
    assert(ss_emit_namespaced(bus, "app", "click", &none) == SS_OK);
    assert(ss_emit_deferred(bus, "click", NULL) == SS_OK);
    assert(ss_flush_deferred(bus) == SS_OK);
    assert(calls == 4);

    /* Qualified names are never prefixed again */
    assert(ss_signal_register(bus, "ui::ok") == SS_OK);
    assert(ss_signal_exists(bus, "app::ui::ok") == 0);

    /* Without a namespace, "click" is a different signal */
    assert(ss_set_namespace(bus, NULL) == SS_OK);
    assert(ss_signal_exists(bus, "click") == 0);
    assert(ss_signal_exists(bus, "app::click") == 1);

    /* An empty namespace clears it too */
    assert(ss_set_namespace(bus, "") == SS_OK);
    assert(ss_get_namespace(bus) == NULL);

    memset(long_ns, 'n', sizeof(long_ns) - 1);
    long_ns[sizeof(long_ns) - 1] = '\0';
    assert(ss_set_namespace(bus, long_ns) == SS_ERR_WOULD_OVERFLOW);
    assert(ss_get_namespace(bus) == NULL);

    ss_bus_destroy(bus);
    printf("Default namespace tests passed!\n");
}

/* A9: two buses share nothing, not even signal names */
static int g_errors_a = 0;

static void count_errors_a(ss_error_t error, const char* msg) {
    (void)error;
    (void)msg;
    g_errors_a++;
}

static void slot_add_int(const ss_data_t* data, void* user_data) {
    *(int*)user_data += ss_data_get_int(data, 0);
}

void test_two_buses_independent(void) {
    ss_bus_t* a = new_bus();
    void* pool;
    ss_bus_t* b = new_pool_bus(&pool, 0);
    ss_batch_t* batch;
    ss_connection_t handle_a = 0, handle_b = 0;
    int sum_a = 0, sum_b = 0;
    printf("\n=== Testing Two Independent Buses (A9) ===\n");
    assert(a != b);

    /* The same names on both buses are different signals */
    assert(ss_signal_register(a, "same") == SS_OK);
    assert(ss_signal_exists(b, "same") == 0);
    assert(ss_signal_register(b, "same") == SS_OK);
    assert(ss_connect_ex(a, "same", slot_add_int, &sum_a, SS_PRIORITY_NORMAL,
                         &handle_a) == SS_OK);
    assert(ss_connect_ex(b, "same", slot_add_int, &sum_b, SS_PRIORITY_NORMAL,
                         &handle_b) == SS_OK);
    assert(ss_emit_int(a, "same", 1) == SS_OK);
    assert(sum_a == 1 && sum_b == 0);
    assert(ss_emit_int(b, "same", 10) == SS_OK);
    assert(sum_a == 1 && sum_b == 10);

    /* Deferred queues are per bus */
    assert(ss_emit_deferred(a, "same", NULL) == SS_OK);
    {
        ss_data_t two = SS_DATA_INIT;
        assert(ss_data_set_int(&two, 2) == SS_OK);
        assert(ss_emit_deferred(a, "same", &two) == SS_OK);
        assert(ss_data_set_int(&two, 20) == SS_OK);
        assert(ss_emit_deferred(b, "same", &two) == SS_OK);
    }
    assert(ss_flush_deferred(b) == SS_OK);
    assert(sum_a == 1 && sum_b == 30);
    assert(ss_flush_deferred(a) == SS_OK);
    assert(sum_a == 3 && sum_b == 30);

    /* Namespaces, limits and error handlers are per bus */
    assert(ss_set_namespace(a, "ns") == SS_OK);
    assert(ss_get_namespace(b) == NULL);
    assert(ss_signal_register(a, "same") == SS_OK);       /* "ns::same" */
    assert(ss_signal_exists(b, "ns::same") == 0);
    assert(ss_set_namespace(a, NULL) == SS_OK);
    ss_set_max_slots_per_signal(a, 1);
    assert(ss_get_max_slots_per_signal(a) == 1);
    assert(ss_get_max_slots_per_signal(b) == SS_DEFAULT_MAX_SLOTS_PER_SIGNAL);
    assert(ss_connect(b, "same", slot_add_int, &sum_b) == SS_OK);
    assert(ss_connect(a, "same", slot_add_int, &sum_a) == SS_ERR_MAX_SLOTS);
    ss_set_error_handler(a, count_errors_a);
    g_errors_a = 0;
    assert(ss_emit_void(b, "missing") == SS_ERR_NOT_FOUND);
    assert(g_errors_a == 0);
    assert(ss_emit_void(a, "missing") == SS_ERR_NOT_FOUND);
    assert(g_errors_a == 1);
#if SS_ENABLE_INTROSPECTION
    assert(ss_get_signal_count(a) == 2 && ss_get_signal_count(b) == 1);
#endif

    /* Connection handles belong to their bus (both may have the same value) */
    (void)handle_b;
    assert(ss_disconnect_handle(a, handle_a) == SS_OK);
    assert(ss_disconnect_handle(a, handle_a) == SS_ERR_NOT_FOUND);
    assert(ss_emit_int(b, "same", 100) == SS_OK);
    assert(sum_b == 230);                                 /* two slots on b */

    /* A batch emits on the bus it was created for */
    batch = ss_batch_create(b);
    assert(batch != NULL);
    assert(ss_batch_add(batch, "same", NULL) == SS_OK);
    assert(ss_batch_emit(batch) == SS_OK);
    assert(sum_a == 3 && sum_b == 230);
    ss_batch_destroy(batch);
    assert(ss_batch_create(NULL) == NULL);

    /* Unregistering on one bus leaves the other alone */
    assert(ss_signal_unregister(a, "same") == SS_OK);
    assert(ss_signal_exists(a, "same") == 0);
    assert(ss_signal_exists(b, "same") == 1);

    ss_bus_destroy(a);
    assert(ss_emit_int(b, "same", 1) == SS_OK);           /* b survives a */
    assert(sum_b == 232);
    ss_bus_destroy(b);
    free(pool);

    /* Every stateful call refuses a NULL bus */
    assert(ss_signal_register(NULL, "x") == SS_ERR_NULL_PARAM);
    assert(ss_emit_void(NULL, "x") == SS_ERR_NULL_PARAM);
    assert(ss_signal_exists(NULL, "x") == 0);
    assert(ss_flush_deferred(NULL) == SS_ERR_NULL_PARAM);
    assert(ss_get_namespace(NULL) == NULL);
    assert(ss_get_max_slots_per_signal(NULL) == 0);
    printf("Two independent buses tests passed!\n");
}

/* A9: ss_bus_create_in places a bus in a caller pool, in either memory
 * mode, and refuses a pool that is NULL or too small (2.x fell back to
 * a heap context instead) */
void test_bus_in_pool(void) {
    size_t need = ss_bus_pool_size();
    unsigned char* raw;
    ss_bus_t* pooled = NULL;
    printf("\n=== Testing Bus in a Caller Pool (A6, A9) ===\n");

    assert(need > 0);
    assert(ss_bus_create_in(NULL, NULL, need, 0) == SS_ERR_NULL_PARAM);
    pooled = (ss_bus_t*)&pooled;
    assert(ss_bus_create_in(&pooled, NULL, need, 0) == SS_ERR_NULL_PARAM);
    assert(pooled == NULL);

    raw = (unsigned char*)malloc(need + 1);
    assert(raw != NULL);
    memset(raw, 0xA5, need + 1);
    pooled = (ss_bus_t*)&pooled;
    assert(ss_bus_create_in(&pooled, raw, 1, 0) == SS_ERR_BUFFER_TOO_SMALL);
    assert(pooled == NULL);
    assert(ss_bus_create_in(&pooled, raw, 0, 0) == SS_ERR_BUFFER_TOO_SMALL);
    /* malloc memory is aligned, so raw + 1 needs all the alignment slack */
    assert(ss_bus_create_in(&pooled, raw + 1, need - 1, 0) == SS_ERR_BUFFER_TOO_SMALL);
    assert(pooled == NULL);
    assert(raw[need / 2] == 0xA5);            /* pool not used */

    /* A misaligned pool of the advertised size works */
    assert(ss_bus_create_in(&pooled, raw + 1, need, 0) == SS_OK);
    assert(pooled != NULL);
    assert(ss_signal_register(pooled, "pool_sig") == SS_OK);
    assert(ss_emit_void(pooled, "pool_sig") == SS_OK);
    ss_bus_destroy(pooled);                   /* must not free the pool */

    /* The pool can hold a new bus after the old one is destroyed */
    assert(ss_bus_create_in(&pooled, raw + 1, need, 0) == SS_OK);
    assert(ss_signal_exists(pooled, "pool_sig") == 0);
    ss_bus_destroy(pooled);

    free(raw);
    printf("Bus in a caller pool tests passed!\n");
}

/* A9, static mode: SS_STATIC_BUSES built-in buses, then
 * SS_ERR_WOULD_OVERFLOW, never the heap. Dynamic mode has no limit. */
void test_static_buses_exhausted(void) {
#if SS_USE_STATIC_MEMORY
    ss_bus_t* buses[SS_STATIC_BUSES + 1];
    ss_bus_t* extra = NULL;
    int i;
    printf("\n=== Testing Built-in Buses Exhausted (A9) ===\n");
    for (i = 0; i < SS_STATIC_BUSES; i++) {
        assert(ss_bus_create(&buses[i], 0) == SS_OK);
        assert(ss_signal_register(buses[i], "mine") == SS_OK);
    }
    extra = (ss_bus_t*)&extra;
    assert(ss_bus_create(&extra, 0) == SS_ERR_WOULD_OVERFLOW);
    assert(extra == NULL);
    /* Destroying one frees its place; the new bus starts empty */
    ss_bus_destroy(buses[0]);
    assert(ss_bus_create(&buses[0], 0) == SS_OK);
    assert(ss_signal_exists(buses[0], "mine") == 0);
    for (i = 0; i < SS_STATIC_BUSES; i++) ss_bus_destroy(buses[i]);
    printf("Built-in buses exhausted tests passed!\n");
#else
    ss_bus_t* buses[4];
    int i;
    printf("\n=== Testing Many Buses (A9) ===\n");
    for (i = 0; i < 4; i++) assert(ss_bus_create(&buses[i], 0) == SS_OK);
    for (i = 0; i < 4; i++) ss_bus_destroy(buses[i]);
    printf("Many buses tests passed!\n");
#endif
    assert(ss_bus_create(NULL, 0) == SS_ERR_NULL_PARAM);
}

/* A8/A9: the create macros pass the caller's SS_ABI_TAG; a caller built
 * with a different ss_data_t layout or ABI revision is refused */
void test_abi_check(void) {
    ss_bus_t* checked = NULL;
    void* pool;
    printf("\n=== Testing ABI Check (A8) ===\n");
    assert(SS_ABI_TAG == sizeof(ss_data_t) * 16u + SS_ABI_REVISION);
    assert(SS_ABI_REVISION > 0 && SS_ABI_REVISION < 16);
    checked = (ss_bus_t*)&checked;
    assert(ss_bus_create_abi(&checked, 0, SS_ABI_TAG + 16u) == SS_ERR_CONFIG_MISMATCH);
    assert(checked == NULL);
    assert(ss_bus_create_abi(&checked, 0, SS_ABI_TAG + 1u) == SS_ERR_CONFIG_MISMATCH);
    assert(ss_bus_create_abi(&checked, 0, sizeof(ss_data_t)) == SS_ERR_CONFIG_MISMATCH);
    assert(checked == NULL);
    assert(ss_bus_create_abi(&checked, 0, SS_ABI_TAG) == SS_OK);
    ss_bus_destroy(checked);

    pool = malloc(ss_bus_pool_size());
    assert(pool != NULL);
    assert(ss_bus_create_in_abi(&checked, pool, ss_bus_pool_size(), 0, SS_ABI_TAG + 1u)
           == SS_ERR_CONFIG_MISMATCH);
    assert(checked == NULL);
    assert(ss_bus_create_in_abi(&checked, pool, ss_bus_pool_size(), 0, SS_ABI_TAG) == SS_OK);
    ss_bus_destroy(checked);
    free(pool);
    assert(strcmp(ss_error_string(SS_ERR_CONFIG_MISMATCH), "Unknown error") != 0);
    assert(strcmp(ss_error_string(SS_ERR_BUFFER_TOO_SMALL), "Unknown error") != 0);
    printf("ABI check tests passed!\n");
}

/* A8: SS_TYPE_CUSTOM and value.custom exist in every configuration, but a
 * library built without custom data cannot copy a custom payload, so
 * queuing one is refused and nothing is emitted later */
#if !SS_ENABLE_CUSTOM_DATA
static int g_custom_unsupported_calls = 0;

static void slot_count_custom_unsupported(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    g_custom_unsupported_calls++;
}

void test_custom_queued_without_support(void) {
    int value = 7;
    ss_data_t d = SS_DATA_INIT;
    ss_batch_t* batch;
    printf("\n=== Testing Queued Custom Data Without Custom Support (A8) ===\n");

    bus = new_bus();
    assert(ss_signal_register(bus, "custom") == SS_OK);
    assert(ss_connect(bus, "custom", slot_count_custom_unsupported, NULL) == SS_OK);
    d.type = SS_TYPE_CUSTOM;
    d.value.custom.ptr = &value;
    d.value.custom.size = sizeof(value);
    g_custom_unsupported_calls = 0;

    assert(ss_emit_deferred(bus, "custom", &d) == SS_ERR_INVALID_TYPE);
    assert(ss_flush_deferred(bus) == SS_OK);
    batch = ss_batch_create(bus);
    assert(batch != NULL);
    assert(ss_batch_add(batch, "custom", &d) == SS_ERR_INVALID_TYPE);
    assert(ss_batch_emit(batch) == SS_OK);
    ss_batch_destroy(batch);
    assert(g_custom_unsupported_calls == 0);

    /* Emitting directly only passes the caller's object on */
    assert(ss_emit(bus, "custom", &d) == SS_OK);
    assert(g_custom_unsupported_calls == 1);
    ss_bus_destroy(bus);
    assert(strcmp(ss_error_string(SS_ERR_INVALID_TYPE), "Unknown error") != 0);
    printf("Queued custom data without custom support tests passed!\n");
}
#endif

/* Debug trace writes only to the stream given to ss_enable_trace(bus) */
#if SS_ENABLE_DEBUG_TRACE
void test_debug_trace(void) {
    FILE* out = tmpfile();
    long written;
    printf("\n=== Testing Debug Trace ===\n");
    assert(out != NULL);
    bus = new_bus();
    assert(ss_signal_register(bus, "quiet") == SS_OK);    /* trace off: nothing */
    ss_enable_trace(bus, out);
    assert(ss_signal_register(bus, "traced") == SS_OK);
    written = ftell(out);
    assert(written > 0);
    ss_disable_trace(bus);
    assert(ss_signal_register(bus, "quiet_again") == SS_OK);
    assert(ftell(out) == written);
    ss_bus_destroy(bus);
    fclose(out);
    printf("Debug trace tests passed!\n");
}
#endif

/* Review R1: a ring slot reused after wrapping must not free a stale payload */
static void slot_ignore(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
}

void test_queue_wraparound(void) {
    ss_data_t s = {0};
    ss_data_t n = {0};
    ss_batch_t* batch;
    int i;
    printf("\n=== Testing Queue Wrap-around ===\n");
    bus = new_bus();
    assert(ss_signal_register(bus, "wrap") == SS_OK);
    assert(ss_connect(bus, "wrap", slot_ignore, NULL) == SS_OK);
    s.type = SS_TYPE_STRING;
    s.value.s_val = "payload";
    n.type = SS_TYPE_INT;

    for (i = 0; i < SS_DEFERRED_QUEUE_SIZE + 1; i++) {   /* wraps to slot 0 */
        assert(ss_emit_deferred(bus, "wrap", &s) == SS_OK);
        assert(ss_flush_deferred(bus) == SS_OK);
    }
    assert(ss_emit_deferred(bus, "wrap", &n) == SS_OK);         /* no payload */
    assert(ss_emit_deferred(bus, "wrap", NULL) == SS_OK);
    assert(ss_flush_deferred(bus) == SS_OK);

    batch = ss_batch_create(bus);
    assert(batch != NULL);
    for (i = 0; i < SS_BATCH_MAX_ENTRIES + 1; i++) {
        assert(ss_batch_add(batch, "wrap", &s) == SS_OK);
        assert(ss_batch_emit(batch) == SS_OK);
    }
    assert(ss_batch_add(batch, "wrap", &n) == SS_OK);
    assert(ss_batch_emit(batch) == SS_OK);
    ss_batch_destroy(batch);
    ss_bus_destroy(bus);
    printf("Queue wrap-around tests passed!\n");
}

/* Review R4: a nested flush does not let the outer flush take new entries */
static int g_nested_seen[8];
static int g_nested_count = 0;

static void slot_nested_flush(const ss_data_t* data, void* user_data) {
    int v = ss_data_get_int(data, -1);
    (void)user_data;
    g_nested_seen[g_nested_count++] = v;
    if (v == 1) {
        assert(ss_flush_deferred(bus) == SS_OK);   /* emits 2 */
        assert(ss_emit_deferred(bus, "nested_q", &(ss_data_t){ .type = SS_TYPE_INT,
                                                          .value.i_val = 3 }) == SS_OK);
    }
}

void test_nested_flush(void) {
    printf("\n=== Testing Nested Flush ===\n");
    bus = new_bus();
    assert(ss_signal_register(bus, "nested_q") == SS_OK);
    assert(ss_connect(bus, "nested_q", slot_nested_flush, NULL) == SS_OK);
    g_nested_count = 0;
    assert(ss_emit_deferred(bus, "nested_q", &(ss_data_t){ .type = SS_TYPE_INT,
                                                      .value.i_val = 1 }) == SS_OK);
    assert(ss_emit_deferred(bus, "nested_q", &(ss_data_t){ .type = SS_TYPE_INT,
                                                      .value.i_val = 2 }) == SS_OK);
    assert(ss_flush_deferred(bus) == SS_OK);
    assert(g_nested_count == 2);              /* 3 was queued during the flush */
    assert(g_nested_seen[0] == 1 && g_nested_seen[1] == 2);
    assert(ss_flush_deferred(bus) == SS_OK);
    assert(g_nested_count == 3 && g_nested_seen[2] == 3);
    ss_bus_destroy(bus);
    printf("Nested flush tests passed!\n");
}

/* Review R3: parameter errors reach the error handler */
static int g_handler_calls = 0;

static void count_errors(ss_error_t error, const char* msg) {
    (void)error;
    assert(msg != NULL);
    g_handler_calls++;
}

void test_error_handler_coverage(void) {
    ss_batch_t* batch;
    printf("\n=== Testing Error Handler Coverage ===\n");
    bus = new_bus();
    ss_set_error_handler(bus, count_errors);
    g_handler_calls = 0;
    assert(ss_emit_namespaced(bus, NULL, "x", NULL) == SS_ERR_NULL_PARAM);
    assert(ss_disconnect_handle(bus, 0) == SS_ERR_NULL_PARAM);
    batch = ss_batch_create(bus);
    assert(batch != NULL);
    assert(ss_batch_add(batch, NULL, NULL) == SS_ERR_NULL_PARAM);
    ss_batch_destroy(batch);
    assert(ss_emit_void(bus, "missing") == SS_ERR_NOT_FOUND);
    assert(g_handler_calls == 4);
    /* Without a batch there is no bus, so no handler to report to */
    assert(ss_batch_add(NULL, "x", NULL) == SS_ERR_NULL_PARAM);
    assert(ss_batch_emit(NULL) == SS_ERR_NULL_PARAM);
    assert(g_handler_calls == 4);
    ss_bus_destroy(bus);
    printf("Error handler coverage tests passed!\n");
}

/* Review R7: restoring the namespace from ss_get_namespace(bus) is safe */
void test_namespace_self_assign(void) {
    printf("\n=== Testing Namespace Self-assignment ===\n");
    bus = new_bus();
    assert(ss_set_namespace(bus, "keep") == SS_OK);
    assert(ss_set_namespace(bus, ss_get_namespace(bus)) == SS_OK);
    assert(strcmp(ss_get_namespace(bus), "keep") == 0);
    ss_bus_destroy(bus);
    printf("Namespace self-assignment tests passed!\n");
}

/* Version macros match the runtime string */
void test_version(void) {
    char expected[32];
    printf("\n=== Testing Version ===\n");
    snprintf(expected, sizeof(expected), "%d.%d.%d", SS_VERSION_MAJOR, SS_VERSION_MINOR, SS_VERSION_PATCH);
    assert(strcmp(expected, SS_VERSION_STRING) == 0);
    assert(strcmp(ss_version(), SS_VERSION_STRING) == 0);
    printf("Version tests passed!\n");
}

int main(void) {
    printf("Starting Signal-Slot Library Tests\n");
    printf("==================================\n");

    test_basic_functionality();
    test_data_types();
    test_multiple_slots();
    test_signal_introspection();
    test_error_handling();
    test_disconnect_during_emit();
    test_priority_ordering();
    test_input_validation();
#if SS_ENABLE_CUSTOM_DATA
    test_custom_cleanup();
#endif
    test_thread_safety_flag();
    test_namespace();
    test_deferred_emission();
    test_batch_operations();
#if SS_ENABLE_ISR_SAFE
    test_isr_null_signal();
#endif

    test_unregister_during_emit();
    test_destroy_during_emit();
    test_deferred_requeue_during_flush();
    test_data_ownership();
    test_data_ref_self_assign();
#if SS_ENABLE_CUSTOM_DATA
    test_deferred_custom_copy();
    test_queued_payload_alignment();
#endif
#if SS_ENABLE_ISR_SAFE
    test_isr_queue_processing();
#endif
    test_unregister_resets_signal();
    test_name_too_long_rejected();
    test_signal_list_owns_strings();
#if SS_ENABLE_THREAD_SAFETY
    test_reentrant_calls_with_thread_safety();
#endif
    test_disconnect_bookkeeping_during_emit();
    test_batch_reentrancy();
    test_default_namespace();
    test_two_buses_independent();
    test_bus_in_pool();
    test_static_buses_exhausted();
    test_abi_check();
#if !SS_ENABLE_CUSTOM_DATA
    test_custom_queued_without_support();
#endif
    test_queue_wraparound();
    test_nested_flush();
    test_error_handler_coverage();
    test_namespace_self_assign();
    test_version();
#if SS_ENABLE_DEBUG_TRACE
    test_debug_trace();
#endif

    printf("\n==================================\n");
    printf("All tests passed successfully!\n");

    return 0;
}
