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
    
    assert(ss_init() == SS_OK);
    
    assert(ss_signal_register("test_signal") == SS_OK);
    assert(ss_signal_exists("test_signal") == 1);
    
    assert(ss_signal_register("test_signal") == SS_ERR_ALREADY_EXISTS);
    
    int counter = 0;
    assert(ss_connect("test_signal", test_slot_void, &counter) == SS_OK);
    
    assert(ss_emit_void("test_signal") == SS_OK);
    assert(counter == 1);
    
    assert(ss_emit_void("test_signal") == SS_OK);
    assert(counter == 2);
    
    assert(ss_disconnect("test_signal", test_slot_void) == SS_OK);
    assert(ss_emit_void("test_signal") == SS_OK);
    assert(counter == 2);
    
    assert(ss_signal_unregister("test_signal") == SS_OK);
    assert(ss_signal_exists("test_signal") == 0);
    
    ss_cleanup();
    printf("Basic functionality tests passed!\n");
}

void test_data_types(void) {
    printf("\n=== Testing Data Types ===\n");
    
    assert(ss_init() == SS_OK);
    
    assert(ss_signal_register("int_signal") == SS_OK);
    assert(ss_signal_register("string_signal") == SS_OK);
    
    g_test_counter = 0;
    assert(ss_connect("int_signal", test_slot_int, NULL) == SS_OK);
    assert(ss_connect("string_signal", test_slot_string, NULL) == SS_OK);
    
    assert(ss_emit_int("int_signal", 42) == SS_OK);
    assert(g_test_counter == 42);
    
    assert(ss_emit_int("int_signal", 8) == SS_OK);
    assert(g_test_counter == 50);
    
    assert(ss_emit_string("string_signal", "Hello, World!") == SS_OK);
    assert(ss_emit_string("string_signal", NULL) == SS_OK);
    
    ss_data_t* data = ss_data_create(SS_TYPE_DOUBLE);
    assert(data != NULL);
    assert(ss_data_set_double(data, 3.14159) == SS_OK);
    ss_data_destroy(data);
    
    ss_cleanup();
    printf("Data type tests passed!\n");
}

void test_multiple_slots(void) {
    printf("\n=== Testing Multiple Slots ===\n");
    
    assert(ss_init() == SS_OK);
    
    assert(ss_signal_register("multi_signal") == SS_OK);
    
    int counter1 = 0, counter2 = 0, counter3 = 0;
    
    assert(ss_connect("multi_signal", test_slot_void, &counter1) == SS_OK);
    assert(ss_connect("multi_signal", test_slot_void, &counter2) == SS_OK);
    assert(ss_connect("multi_signal", test_slot_void, &counter3) == SS_OK);
    
    assert(ss_emit_void("multi_signal") == SS_OK);
    assert(counter1 == 1 && counter2 == 1 && counter3 == 1);
    
    assert(ss_disconnect_all("multi_signal") == SS_OK);
    
    assert(ss_emit_void("multi_signal") == SS_OK);
    assert(counter1 == 1 && counter2 == 1 && counter3 == 1);
    
    ss_cleanup();
    printf("Multiple slots tests passed!\n");
}

void test_signal_introspection(void) {
    printf("\n=== Testing Signal Introspection ===\n");
    
#if SS_ENABLE_INTROSPECTION
    assert(ss_init() == SS_OK);
    
    assert(ss_signal_register("signal1") == SS_OK);
    assert(ss_signal_register("signal2") == SS_OK);
    assert(ss_signal_register("signal3") == SS_OK);
    
    assert(ss_get_signal_count() == 3);
    
    ss_signal_info_t* list = NULL;
    size_t count = 0;
    
    assert(ss_get_signal_list(&list, &count) == SS_OK);
    assert(count == 3);
    assert(list != NULL);
    
    for (size_t i = 0; i < count; i++) {
        printf("Signal: %s, slots: %zu\n", list[i].name, list[i].slot_count);
    }
    
    ss_free_signal_list(list, count);
    
    ss_cleanup();
#else
    printf("Introspection disabled - skipping tests\n");
#endif
    printf("Signal introspection tests passed!\n");
}

void test_error_handling(void) {
    printf("\n=== Testing Error Handling ===\n");
    
    assert(ss_init() == SS_OK);
    
    assert(ss_emit_void("nonexistent") == SS_ERR_NOT_FOUND);
    
    assert(ss_connect(NULL, test_slot_void, NULL) == SS_ERR_NULL_PARAM);
    assert(ss_connect("test", NULL, NULL) == SS_ERR_NULL_PARAM);
    
    assert(ss_signal_register(NULL) == SS_ERR_NULL_PARAM);
    assert(ss_signal_register("") == SS_ERR_NULL_PARAM);
    
    assert(ss_disconnect("nonexistent", test_slot_void) == SS_ERR_NOT_FOUND);
    
    ss_set_max_slots_per_signal(2);
    assert(ss_signal_register("limited") == SS_OK);
    assert(ss_connect("limited", test_slot_void, NULL) == SS_OK);
    assert(ss_connect("limited", test_slot_void, NULL) == SS_OK);
    assert(ss_connect("limited", test_slot_void, NULL) == SS_ERR_MAX_SLOTS);
    
    printf("Error: %s\n", ss_error_string(SS_ERR_NOT_FOUND));
    printf("Error: %s\n", ss_error_string(SS_ERR_MEMORY));
    
    ss_cleanup();
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
    ss_disconnect_handle(g_handle_to_disconnect);
}

void other_slot(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    g_other_slot_called++;
}

void test_disconnect_during_emit(void) {
    printf("\n=== Testing Disconnect During Emit ===\n");

    assert(ss_init() == SS_OK);

    assert(ss_signal_register("emit_test") == SS_OK);

    /* Connect disconnect_other_slot at higher priority so it fires first */
    assert(ss_connect_ex("emit_test", other_slot, NULL,
                         SS_PRIORITY_NORMAL, &g_handle_to_disconnect) == SS_OK);
    ss_connection_t handle1;
    assert(ss_connect_ex("emit_test", disconnect_other_slot, NULL,
                         SS_PRIORITY_HIGH, &handle1) == SS_OK);

    g_disconnect_slot_called = 0;
    g_other_slot_called = 0;

    /* Emit — first slot disconnects second, but iteration should not crash */
    assert(ss_emit_void("emit_test") == SS_OK);
    assert(g_disconnect_slot_called == 1);

    /* Emit again — only first slot should fire now */
    g_disconnect_slot_called = 0;
    g_other_slot_called = 0;
    assert(ss_emit_void("emit_test") == SS_OK);
    assert(g_disconnect_slot_called == 1);
    assert(g_other_slot_called == 0);

    ss_cleanup();
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

    assert(ss_init() == SS_OK);
    assert(ss_signal_register("prio_test") == SS_OK);

    /* Connect in scrambled order */
    assert(ss_connect_ex("prio_test", priority_slot_normal, NULL,
                         SS_PRIORITY_NORMAL, NULL) == SS_OK);
    assert(ss_connect_ex("prio_test", priority_slot_critical, NULL,
                         SS_PRIORITY_CRITICAL, NULL) == SS_OK);
    assert(ss_connect_ex("prio_test", priority_slot_low, NULL,
                         SS_PRIORITY_LOW, NULL) == SS_OK);
    assert(ss_connect_ex("prio_test", priority_slot_high, NULL,
                         SS_PRIORITY_HIGH, NULL) == SS_OK);

    g_priority_idx = 0;
    assert(ss_emit_void("prio_test") == SS_OK);
    assert(g_priority_idx == 4);

    /* Verify execution order: CRITICAL > HIGH > NORMAL > LOW */
    assert(g_priority_order[0] == SS_PRIORITY_CRITICAL);
    assert(g_priority_order[1] == SS_PRIORITY_HIGH);
    assert(g_priority_order[2] == SS_PRIORITY_NORMAL);
    assert(g_priority_order[3] == SS_PRIORITY_LOW);

    ss_cleanup();
    printf("Priority ordering tests passed!\n");
}

void test_input_validation(void) {
    printf("\n=== Testing Input Validation ===\n");

    assert(ss_init() == SS_OK);

    /* Signal name too long */
    char long_name[300];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(ss_signal_register(long_name) == SS_ERR_WOULD_OVERFLOW);

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
    assert(ss_signal_register("stats_test") == SS_OK);

    ss_memory_stats_t stats1, stats2;
    assert(ss_get_memory_stats(&stats1) == SS_OK);
    assert(ss_get_memory_stats(&stats2) == SS_OK);
    assert(stats1.string_bytes == stats2.string_bytes);

    ss_signal_unregister("stats_test");
#endif

    ss_cleanup();
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

    assert(ss_init() == SS_OK);

    ss_data_t* data = ss_data_create(SS_TYPE_CUSTOM);
    assert(data != NULL);

    int dummy = 99;
    g_custom_cleanup_called = 0;
    assert(ss_data_set_custom(data, &dummy, sizeof(dummy), custom_cleanup_fn) == SS_OK);

    /* Destroying should call our cleanup function */
    ss_data_destroy(data);
    assert(g_custom_cleanup_called == 1);

    ss_cleanup();
    printf("Custom cleanup callback tests passed!\n");
}
#endif

#if SS_ENABLE_THREAD_SAFETY
void test_thread_safety_config(void) {
    printf("\n=== Testing Thread Safety Configuration ===\n");
    
    assert(ss_init() == SS_OK);
    
    assert(ss_is_thread_safe() == 0);
    
    ss_set_thread_safe(1);
    assert(ss_is_thread_safe() == 1);
    
    assert(ss_signal_register("thread_test") == SS_OK);
    assert(ss_emit_void("thread_test") == SS_OK);
    
    ss_set_thread_safe(0);
    assert(ss_is_thread_safe() == 0);
    
    ss_cleanup();
    printf("Thread safety configuration tests passed!\n");
}
#endif

#if SS_ENABLE_ISR_SAFE
void test_isr_null_signal(void) {
    printf("\n=== Testing ISR NULL Signal ===\n");

    assert(ss_init() == SS_OK);
    assert(ss_emit_from_isr(NULL, 42) == SS_ERR_NULL_PARAM);
    ss_cleanup();

    printf("ISR NULL signal tests passed!\n");
}
#endif

void test_namespace(void) {
    printf("\n=== Testing Namespace Support ===\n");

    assert(ss_init() == SS_OK);

    /* Register a namespaced signal */
    assert(ss_signal_register("ui::button") == SS_OK);

    int counter = 0;
    assert(ss_connect("ui::button", test_slot_void, &counter) == SS_OK);

    /* Emit via namespace helper using a void data payload */
    ss_data_t* ns_data = ss_data_create(SS_TYPE_VOID);
    assert(ns_data != NULL);
    assert(ss_emit_namespaced("ui", "button", ns_data) == SS_OK);
    assert(counter == 1);
    ss_data_destroy(ns_data);

    /* Set/get namespace */
    assert(ss_set_namespace("app") == SS_OK);
    assert(strcmp(ss_get_namespace(), "app") == 0);

    /* Clear namespace */
    assert(ss_set_namespace(NULL) == SS_OK);
    assert(ss_get_namespace() == NULL);

    /* NULL params */
    assert(ss_emit_namespaced(NULL, "x", NULL) == SS_ERR_NULL_PARAM);
    assert(ss_emit_namespaced("x", NULL, NULL) == SS_ERR_NULL_PARAM);

    ss_cleanup();
    printf("Namespace support tests passed!\n");
}

void test_deferred_emission(void) {
    printf("\n=== Testing Deferred Emission ===\n");

    assert(ss_init() == SS_OK);

    assert(ss_signal_register("deferred1") == SS_OK);
    assert(ss_signal_register("deferred2") == SS_OK);

    g_test_counter = 0;
    assert(ss_connect("deferred1", test_slot_int, NULL) == SS_OK);
    assert(ss_connect("deferred2", test_slot_int, NULL) == SS_OK);

    /* Enqueue 3 deferred emissions */
    ss_data_t* d1 = ss_data_create(SS_TYPE_INT);
    assert(d1 != NULL);
    assert(ss_data_set_int(d1, 10) == SS_OK);
    assert(ss_emit_deferred("deferred1", d1) == SS_OK);
    ss_data_destroy(d1);

    ss_data_t* d2 = ss_data_create(SS_TYPE_INT);
    assert(d2 != NULL);
    assert(ss_data_set_int(d2, 20) == SS_OK);
    assert(ss_emit_deferred("deferred2", d2) == SS_OK);
    ss_data_destroy(d2);

    ss_data_t* d3 = ss_data_create(SS_TYPE_INT);
    assert(d3 != NULL);
    assert(ss_data_set_int(d3, 30) == SS_OK);
    assert(ss_emit_deferred("deferred1", d3) == SS_OK);
    ss_data_destroy(d3);

    /* Counter should be 0 — nothing emitted yet */
    assert(g_test_counter == 0);

    /* Flush — all 3 should fire */
    assert(ss_flush_deferred() == SS_OK);
    assert(g_test_counter == 60);

    ss_cleanup();
    printf("Deferred emission tests passed!\n");
}

void test_batch_operations(void) {
    printf("\n=== Testing Batch Operations ===\n");

    assert(ss_init() == SS_OK);

    assert(ss_signal_register("batch_a") == SS_OK);
    assert(ss_signal_register("batch_b") == SS_OK);

    g_test_counter = 0;
    assert(ss_connect("batch_a", test_slot_int, NULL) == SS_OK);
    assert(ss_connect("batch_b", test_slot_int, NULL) == SS_OK);

    ss_batch_t* batch = ss_batch_create();
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
    ss_cleanup();
    printf("Batch operations tests passed!\n");
}

/* ------------------------------------------------------------------ */
/* Regression tests for the 2.2.0 review findings (S* / A* ids)        */
/* ------------------------------------------------------------------ */

/* S1: unregistering a signal from inside one of its own slots */
static void slot_unregister_self(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    assert(ss_signal_unregister("unreg_sig") == SS_OK);
    assert(ss_signal_exists("unreg_sig") == 0);
}

void test_unregister_during_emit(void) {
    int after = 0;
    printf("\n=== Testing Unregister During Emit (S1) ===\n");
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("unreg_sig") == SS_OK);
    assert(ss_connect_ex("unreg_sig", slot_unregister_self, NULL,
                         SS_PRIORITY_HIGH, NULL) == SS_OK);
    assert(ss_connect("unreg_sig", test_slot_void, &after) == SS_OK);

    assert(ss_emit_void("unreg_sig") == SS_OK);
    assert(after == 0);                       /* remaining slots are skipped */
    assert(ss_signal_exists("unreg_sig") == 0);

    /* The name is free again and the new signal starts empty */
    assert(ss_signal_register("unreg_sig") == SS_OK);
    assert(ss_emit_void("unreg_sig") == SS_OK);
    assert(after == 0);

    ss_cleanup();
    printf("Unregister during emit tests passed!\n");
}

/* S1: ss_cleanup() from inside a slot is deferred to the end of the emit */
static void slot_cleanup_library(const ss_data_t* data, void* user_data) {
    (void)data; (void)user_data;
    ss_cleanup();
    /* The library behaves as uninitialised from here on */
    assert(ss_signal_exists("cleanup_sig") == 0);
    assert(ss_emit_void("cleanup_sig") == SS_ERR_NULL_PARAM);
}

void test_cleanup_during_emit(void) {
    int after = 0;
    printf("\n=== Testing Cleanup During Emit (S1) ===\n");
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("cleanup_sig") == SS_OK);
    assert(ss_connect_ex("cleanup_sig", slot_cleanup_library, NULL,
                         SS_PRIORITY_HIGH, NULL) == SS_OK);
    assert(ss_connect("cleanup_sig", test_slot_void, &after) == SS_OK);

    assert(ss_emit_void("cleanup_sig") == SS_OK);
    assert(after == 0);

    /* Cleanup completed when the emit returned; re-init works */
    assert(ss_signal_exists("cleanup_sig") == 0);
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("cleanup_sig") == SS_OK);
    ss_cleanup();
    printf("Cleanup during emit tests passed!\n");
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
        assert(ss_emit_deferred("requeue_b", &next) == SS_OK);
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

    assert(ss_init() == SS_OK);
    assert(ss_signal_register("requeue_a") == SS_OK);
    assert(ss_signal_register("requeue_b") == SS_OK);
    assert(ss_connect_ex("requeue_a", slot_record_and_requeue, NULL,
                         SS_PRIORITY_HIGH, NULL) == SS_OK);
    assert(ss_connect("requeue_a", slot_record, NULL) == SS_OK);
    assert(ss_connect("requeue_b", slot_record, NULL) == SS_OK);

    /* The queue owns a copy: changing the caller's buffer has no effect */
    strcpy(payload, "first");
    first.type = SS_TYPE_STRING;
    first.value.s_val = payload;
    g_seen_count = 0;
    assert(ss_emit_deferred("requeue_a", &first) == SS_OK);
    strcpy(payload, "XXXXX");

    assert(ss_flush_deferred() == SS_OK);
    assert(g_seen_count == 2);
    assert(strcmp(g_seen[0], "first") == 0);
    assert(strcmp(g_seen[1], "first") == 0);  /* not overwritten by "second" */

    /* The entry queued during the flush is delivered by the next flush */
    assert(ss_flush_deferred() == SS_OK);
    assert(g_seen_count == 3);
    assert(strcmp(g_seen[2], "second") == 0);

    /* Pending entries are released by cleanup without being emitted */
    assert(ss_emit_deferred("requeue_b", &first) == SS_OK);
    ss_cleanup();
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

    assert(ss_init() == SS_OK);
    assert(ss_signal_register("custom_sig") == SS_OK);
    assert(ss_connect("custom_sig", slot_record_pair, NULL) == SS_OK);

    d.type = SS_TYPE_CUSTOM;
    d.custom_data = &pair;
    d.size = sizeof(pair);
    d.custom_cleanup = count_cleanup_on_copy;

    g_custom_cleanup_on_copy = 0;
    assert(ss_emit_deferred("custom_sig", &d) == SS_OK);
    pair.a = 99;
    assert(ss_flush_deferred() == SS_OK);
    assert(g_pair_seen.a == 1 && g_pair_seen.b == 2);

    batch = ss_batch_create();
    assert(batch != NULL);
    assert(ss_batch_add(batch, "custom_sig", &d) == SS_OK);
    pair.a = 7;
    assert(ss_batch_emit(batch) == SS_OK);
    assert(g_pair_seen.a == 99);
    ss_batch_destroy(batch);

    /* The caller's cleanup belongs to the caller's object, not our copy */
    assert(g_custom_cleanup_on_copy == 0);
    ss_cleanup();
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
        d.custom_data = &pair;
        d.size = sizeof(pair);
        assert(ss_data_set_double(&d, 1.5) == SS_OK);
        assert(pair.a == 3 && pair.b == 4);
    }
#endif

    /* Slots see borrowed data, also when the emitter's object owns it */
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("reuse") == SS_OK);
    assert(ss_connect("reuse", slot_reuse_view, NULL) == SS_OK);
    assert(ss_signal_register("s12::reuse") == SS_OK);
    assert(ss_connect("s12::reuse", slot_reuse_view, NULL) == SS_OK);
    owned = ss_data_create(SS_TYPE_STRING);
    assert(owned != NULL);
    assert(ss_data_set_string(owned, "owned") == SS_OK);
    g_reused_views = 0;
    assert(ss_emit("reuse", owned) == SS_OK);
    assert(ss_emit_namespaced("s12", "reuse", owned) == SS_OK);
    assert(ss_emit_string("reuse", text) == SS_OK);
    assert(ss_emit_deferred("reuse", owned) == SS_OK);
    assert(ss_flush_deferred() == SS_OK);
    batch = ss_batch_create();
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
        assert(ss_emit("reuse", owned) == SS_OK);
        assert(ss_emit_deferred("reuse", owned) == SS_OK);
        assert(ss_flush_deferred() == SS_OK);
        assert(g_reused_views == 7);
    }
#endif
    ss_data_destroy(owned);
    ss_cleanup();
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

    assert(ss_init() == SS_OK);
    assert(ss_signal_register("aligned") == SS_OK);
    assert(ss_connect("aligned", slot_check_payload_alignment, NULL) == SS_OK);
    d.type = SS_TYPE_CUSTOM;
    d.custom_data = &value;
    d.size = sizeof(value);

    g_payload_misalignment = 0;
    g_payload_calls = 0;
    assert(ss_emit_deferred("aligned", &d) == SS_OK);
    assert(ss_flush_deferred() == SS_OK);
    batch = ss_batch_create();
    assert(batch != NULL);
    assert(ss_batch_add(batch, "aligned", &d) == SS_OK);
    assert(ss_batch_emit(batch) == SS_OK);
    ss_batch_destroy(batch);
    assert(g_payload_calls == 2);
    if (g_payload_misalignment != 0) {
        printf("payload not aligned to %u bytes\n", (unsigned)TEST_ALIGN);
    }
    assert(g_payload_misalignment == 0);
    ss_cleanup();
    printf("Queued payload alignment tests passed!\n");
}
#endif

/* S7: ISR queue is drained in order by ss_process_isr_queue() */
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

    assert(ss_init() == SS_OK);
    assert(ss_signal_register("isr_sig") == SS_OK);
    assert(ss_connect("isr_sig", slot_record_isr, NULL) == SS_OK);

    g_isr_count = 0;
    assert(ss_emit_from_isr("isr_sig", 1) == SS_OK);
    assert(ss_emit_from_isr("isr_sig", 2) == SS_OK);
    assert(ss_emit_from_isr("isr_sig", 3) == SS_OK);
    assert(g_isr_count == 0);                 /* nothing runs in the ISR */

    assert(ss_process_isr_queue() == SS_OK);
    assert(g_isr_count == 3);
    assert(g_isr_values[0] == 1 && g_isr_values[1] == 2 && g_isr_values[2] == 3);

    /* A full queue rejects, and draining makes room again */
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        assert(ss_emit_from_isr("isr_sig", 100 + i) == SS_OK);
    }
    assert(ss_emit_from_isr("isr_sig", -1) == SS_ERR_WOULD_OVERFLOW);
    assert(ss_process_isr_queue() == SS_OK);
    assert(g_isr_count == 3 + SS_ISR_QUEUE_SIZE);
    for (i = 0; i < SS_ISR_QUEUE_SIZE; i++) {
        assert(g_isr_values[3 + i] == 100 + i);
    }
    assert(ss_emit_from_isr("isr_sig", 5) == SS_OK);
    assert(ss_process_isr_queue() == SS_OK);

    /* S10: over-long names are rejected, never truncated */
    memset(long_name, 'i', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(ss_emit_from_isr(long_name, 1) == SS_ERR_WOULD_OVERFLOW);

    /* Events for unknown signals are dropped and reported */
    assert(ss_emit_from_isr("no_such_signal", 1) == SS_OK);
    assert(ss_process_isr_queue() == SS_ERR_NOT_FOUND);

    ss_cleanup();
    assert(ss_process_isr_queue() == SS_ERR_NULL_PARAM);
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
        assert(ss_init() == SS_OK);
        assert(ss_signal_register_ex("s9_old", "old description",
                                     SS_PRIORITY_HIGH) == SS_OK);
        assert(ss_signal_unregister("s9_old") == SS_OK);
        assert(ss_signal_register("s9_new") == SS_OK);
        assert(ss_get_signal_list(&list, &count) == SS_OK);
        assert(count == 1);
        assert(strcmp(list[0].name, "s9_new") == 0);
        assert(list[0].description == NULL);
        assert(list[0].priority == SS_PRIORITY_NORMAL);
        ss_free_signal_list(list, count);
        ss_cleanup();
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

    assert(ss_init() == SS_OK);
    assert(ss_emit_deferred(long_name, NULL) == SS_ERR_WOULD_OVERFLOW);
    batch = ss_batch_create();
    assert(batch != NULL);
    assert(ss_batch_add(batch, long_name, NULL) == SS_ERR_WOULD_OVERFLOW);
    ss_batch_destroy(batch);
    assert(ss_flush_deferred() == SS_OK);
    ss_cleanup();
    printf("Over-long names rejected tests passed!\n");
}

/* S11: the signal list owns its strings */
void test_signal_list_owns_strings(void) {
    printf("\n=== Testing Signal List Owns Strings (S11) ===\n");
#if SS_ENABLE_INTROSPECTION
    {
        ss_signal_info_t* list = NULL;
        size_t count = 0;
        assert(ss_init() == SS_OK);
        assert(ss_signal_register_ex("s11", "about s11", SS_PRIORITY_LOW) == SS_OK);
        assert(ss_get_signal_list(&list, &count) == SS_OK);
        assert(ss_signal_unregister("s11") == SS_OK);
        assert(count == 1);
        assert(strcmp(list[0].name, "s11") == 0);
        assert(strcmp(list[0].description, "about s11") == 0);
        ss_free_signal_list(list, count);
        ss_cleanup();
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
    assert(ss_signal_exists("outer") == 1);
    assert(ss_connect("inner", slot_nested, NULL) == SS_OK);
    assert(ss_emit_void("inner") == SS_OK);
    assert(ss_emit_deferred("inner", NULL) == SS_OK);
}

void test_reentrant_calls_with_thread_safety(void) {
    printf("\n=== Testing Re-entrant Calls With Thread Safety (S5) ===\n");
    TEST_WATCHDOG(10);

    assert(ss_init() == SS_OK);
    ss_set_thread_safe(1);
    assert(ss_signal_register("outer") == SS_OK);
    assert(ss_signal_register("inner") == SS_OK);
    assert(ss_connect("outer", slot_reenter, NULL) == SS_OK);

    g_nested_called = 0;
    assert(ss_emit_void("outer") == SS_OK);
    assert(g_nested_called == 1);
    assert(ss_flush_deferred() == SS_OK);
    assert(g_nested_called == 2);

    /* Toggling inside a slot must not leave the mutex held */
    ss_set_thread_safe(0);
    ss_set_thread_safe(1);
    assert(ss_emit_void("inner") == SS_OK);

    ss_cleanup();
    TEST_WATCHDOG(0);
    printf("Re-entrant calls with thread safety tests passed!\n");
}
#endif

/* Disconnect bookkeeping while a signal is emitting */
static ss_connection_t g_self_handle = 0;

/* user_data: counter for the replacement slot */
static void slot_replace_self(const ss_data_t* data, void* user_data) {
    (void)data;
    assert(ss_disconnect_handle(g_self_handle) == SS_OK);
    assert(ss_disconnect_handle(g_self_handle) == SS_ERR_NOT_FOUND);
    /* The limit counts live slots, so the freed place can be reused now */
    assert(ss_connect("replace_sig", test_slot_void, user_data) == SS_OK);
}

void test_disconnect_bookkeeping_during_emit(void) {
    int replacement = 0;
    printf("\n=== Testing Disconnect Bookkeeping During Emit ===\n");
    assert(ss_init() == SS_OK);
    ss_set_max_slots_per_signal(1);
    assert(ss_signal_register("replace_sig") == SS_OK);
    assert(ss_connect_ex("replace_sig", slot_replace_self, &replacement,
                         SS_PRIORITY_NORMAL, &g_self_handle) == SS_OK);
    assert(ss_emit_void("replace_sig") == SS_OK);
    assert(replacement == 0);                 /* connected after the cursor */
    assert(ss_emit_void("replace_sig") == SS_OK);
    assert(replacement == 1);
    ss_cleanup();
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
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("batch_sig") == SS_OK);
    assert(ss_connect("batch_sig", slot_extend_batch, NULL) == SS_OK);

    g_batch = ss_batch_create();
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

    ss_cleanup();
    printf("Batch re-entrancy tests passed!\n");
}

/* A6: the default namespace qualifies unqualified names */
void test_default_namespace(void) {
    char long_ns[300];
    int calls = 0;
    ss_data_t none = {0};                     /* SS_TYPE_VOID */
    printf("\n=== Testing Default Namespace (A6) ===\n");
    assert(ss_init() == SS_OK);

    assert(ss_set_namespace("app") == SS_OK);
    assert(ss_signal_register("click") == SS_OK);
    assert(ss_signal_exists("app::click") == 1);
    assert(ss_signal_exists("click") == 1);
    assert(ss_connect("click", test_slot_void, &calls) == SS_OK);

    assert(ss_emit_void("click") == SS_OK);
    assert(ss_emit_void("app::click") == SS_OK);
    assert(ss_emit_namespaced("app", "click", &none) == SS_OK);
    assert(ss_emit_deferred("click", NULL) == SS_OK);
    assert(ss_flush_deferred() == SS_OK);
    assert(calls == 4);

    /* Qualified names are never prefixed again */
    assert(ss_signal_register("ui::ok") == SS_OK);
    assert(ss_signal_exists("app::ui::ok") == 0);

    /* Without a namespace, "click" is a different signal */
    assert(ss_set_namespace(NULL) == SS_OK);
    assert(ss_signal_exists("click") == 0);
    assert(ss_signal_exists("app::click") == 1);

    /* An empty namespace clears it too */
    assert(ss_set_namespace("") == SS_OK);
    assert(ss_get_namespace() == NULL);

    memset(long_ns, 'n', sizeof(long_ns) - 1);
    long_ns[sizeof(long_ns) - 1] = '\0';
    assert(ss_set_namespace(long_ns) == SS_ERR_WOULD_OVERFLOW);
    assert(ss_get_namespace() == NULL);

    ss_cleanup();
    printf("Default namespace tests passed!\n");
}

/* A6: ss_init_static places the library context in a caller pool */
void test_init_static_pool(void) {
    size_t need = ss_get_required_pool_size();
    unsigned char* raw;
    printf("\n=== Testing Static Pool Initialisation (A6) ===\n");

    assert(need > 0);

    /* 2.1 compatibility: NULL or too small a pool behaves like ss_init() */
    assert(ss_init_static(NULL, 0) == SS_OK);
    assert(ss_signal_register("fallback_sig") == SS_OK);
    ss_cleanup();

    raw = (unsigned char*)malloc(need + 1);
    assert(raw != NULL);
    memset(raw, 0xA5, need + 1);
    assert(ss_init_static(raw + 1, need - 1) == SS_OK);
    assert(raw[need / 2] == 0xA5);            /* pool not used */
    ss_cleanup();

    /* A misaligned pool of the advertised size still works */
    assert(ss_init_static(raw + 1, need) == SS_OK);
    assert(ss_signal_register("pool_sig") == SS_OK);
    assert(ss_emit_void("pool_sig") == SS_OK);
    ss_cleanup();                             /* must not free the pool */
    assert(ss_signal_exists("pool_sig") == 0);

    free(raw);
    printf("Static pool initialisation tests passed!\n");
}

/* A8: a caller built with a different ss_data_t layout or ABI revision is
 * refused */
void test_abi_check(void) {
    printf("\n=== Testing ABI Check (A8) ===\n");
    assert(SS_ABI_TAG == sizeof(ss_data_t) * 16u + SS_ABI_REVISION);
    assert(SS_ABI_REVISION > 0 && SS_ABI_REVISION < 16);
    assert(ss_init_abi(SS_ABI_TAG + 16u) == SS_ERR_CONFIG_MISMATCH);   /* size */
    assert(ss_init_abi(SS_ABI_TAG + 1u) == SS_ERR_CONFIG_MISMATCH);    /* revision */
    assert(ss_init_abi(sizeof(ss_data_t)) == SS_ERR_CONFIG_MISMATCH);  /* 2.x tag */
    assert(ss_signal_exists("anything") == 0);   /* still uninitialised */
    assert(ss_init_abi(SS_ABI_TAG) == SS_OK);
    ss_cleanup();
    assert(ss_init_static_abi(NULL, 0, SS_ABI_TAG + 1u) == SS_ERR_CONFIG_MISMATCH);
    assert(ss_init_static_abi(NULL, 0, SS_ABI_TAG) == SS_OK);
    ss_cleanup();
    assert(strcmp(ss_error_string(SS_ERR_CONFIG_MISMATCH), "Unknown error") != 0);
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

    assert(ss_init() == SS_OK);
    assert(ss_signal_register("custom") == SS_OK);
    assert(ss_connect("custom", slot_count_custom_unsupported, NULL) == SS_OK);
    d.type = SS_TYPE_CUSTOM;
    d.value.custom.ptr = &value;
    d.value.custom.size = sizeof(value);
    g_custom_unsupported_calls = 0;

    assert(ss_emit_deferred("custom", &d) == SS_ERR_INVALID_TYPE);
    assert(ss_flush_deferred() == SS_OK);
    batch = ss_batch_create();
    assert(batch != NULL);
    assert(ss_batch_add(batch, "custom", &d) == SS_ERR_INVALID_TYPE);
    assert(ss_batch_emit(batch) == SS_OK);
    ss_batch_destroy(batch);
    assert(g_custom_unsupported_calls == 0);

    /* Emitting directly only passes the caller's object on */
    assert(ss_emit("custom", &d) == SS_OK);
    assert(g_custom_unsupported_calls == 1);
    ss_cleanup();
    assert(strcmp(ss_error_string(SS_ERR_INVALID_TYPE), "Unknown error") != 0);
    printf("Queued custom data without custom support tests passed!\n");
}
#endif

/* Debug trace writes only to the stream given to ss_enable_trace() */
#if SS_ENABLE_DEBUG_TRACE
void test_debug_trace(void) {
    FILE* out = tmpfile();
    long written;
    printf("\n=== Testing Debug Trace ===\n");
    assert(out != NULL);
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("quiet") == SS_OK);    /* trace off: nothing */
    ss_enable_trace(out);
    assert(ss_signal_register("traced") == SS_OK);
    written = ftell(out);
    assert(written > 0);
    ss_disable_trace();
    assert(ss_signal_register("quiet_again") == SS_OK);
    assert(ftell(out) == written);
    ss_cleanup();
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
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("wrap") == SS_OK);
    assert(ss_connect("wrap", slot_ignore, NULL) == SS_OK);
    s.type = SS_TYPE_STRING;
    s.value.s_val = "payload";
    n.type = SS_TYPE_INT;

    for (i = 0; i < SS_DEFERRED_QUEUE_SIZE + 1; i++) {   /* wraps to slot 0 */
        assert(ss_emit_deferred("wrap", &s) == SS_OK);
        assert(ss_flush_deferred() == SS_OK);
    }
    assert(ss_emit_deferred("wrap", &n) == SS_OK);         /* no payload */
    assert(ss_emit_deferred("wrap", NULL) == SS_OK);
    assert(ss_flush_deferred() == SS_OK);

    batch = ss_batch_create();
    assert(batch != NULL);
    for (i = 0; i < SS_BATCH_MAX_ENTRIES + 1; i++) {
        assert(ss_batch_add(batch, "wrap", &s) == SS_OK);
        assert(ss_batch_emit(batch) == SS_OK);
    }
    assert(ss_batch_add(batch, "wrap", &n) == SS_OK);
    assert(ss_batch_emit(batch) == SS_OK);
    ss_batch_destroy(batch);
    ss_cleanup();
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
        assert(ss_flush_deferred() == SS_OK);   /* emits 2 */
        assert(ss_emit_deferred("nested_q", &(ss_data_t){ .type = SS_TYPE_INT,
                                                          .value.i_val = 3 }) == SS_OK);
    }
}

void test_nested_flush(void) {
    printf("\n=== Testing Nested Flush ===\n");
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("nested_q") == SS_OK);
    assert(ss_connect("nested_q", slot_nested_flush, NULL) == SS_OK);
    g_nested_count = 0;
    assert(ss_emit_deferred("nested_q", &(ss_data_t){ .type = SS_TYPE_INT,
                                                      .value.i_val = 1 }) == SS_OK);
    assert(ss_emit_deferred("nested_q", &(ss_data_t){ .type = SS_TYPE_INT,
                                                      .value.i_val = 2 }) == SS_OK);
    assert(ss_flush_deferred() == SS_OK);
    assert(g_nested_count == 2);              /* 3 was queued during the flush */
    assert(g_nested_seen[0] == 1 && g_nested_seen[1] == 2);
    assert(ss_flush_deferred() == SS_OK);
    assert(g_nested_count == 3 && g_nested_seen[2] == 3);
    ss_cleanup();
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
    printf("\n=== Testing Error Handler Coverage ===\n");
    assert(ss_init() == SS_OK);
    ss_set_error_handler(count_errors);
    g_handler_calls = 0;
    assert(ss_emit_namespaced(NULL, "x", NULL) == SS_ERR_NULL_PARAM);
    assert(ss_disconnect_handle(0) == SS_ERR_NULL_PARAM);
    assert(ss_batch_add(NULL, "x", NULL) == SS_ERR_NULL_PARAM);
    assert(ss_batch_emit(NULL) == SS_ERR_NULL_PARAM);
    assert(ss_emit_void("missing") == SS_ERR_NOT_FOUND);
    assert(g_handler_calls == 5);
    ss_cleanup();
    printf("Error handler coverage tests passed!\n");
}

/* Review R7: restoring the namespace from ss_get_namespace() is safe */
void test_namespace_self_assign(void) {
    printf("\n=== Testing Namespace Self-assignment ===\n");
    assert(ss_init() == SS_OK);
    assert(ss_set_namespace("keep") == SS_OK);
    assert(ss_set_namespace(ss_get_namespace()) == SS_OK);
    assert(strcmp(ss_get_namespace(), "keep") == 0);
    ss_cleanup();
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
#if SS_ENABLE_THREAD_SAFETY
    test_thread_safety_config();
#endif
    test_namespace();
    test_deferred_emission();
    test_batch_operations();
#if SS_ENABLE_ISR_SAFE
    test_isr_null_signal();
#endif

    test_unregister_during_emit();
    test_cleanup_during_emit();
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
    test_init_static_pool();
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
