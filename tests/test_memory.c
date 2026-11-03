/*
 * Allocation accounting tests (C89).
 *
 * The implementation is compiled into this file with SS_MALLOC, SS_CALLOC
 * and SS_FREE routed to a tracking allocator, so the tests can prove that:
 *   - every allocation is released by the matching free (no leaks),
 *   - every pointer the library frees came from SS_MALLOC/SS_CALLOC
 *     (the default SS_STRDUP must not bypass a custom allocator),
 *   - with SS_USE_STATIC_MEMORY the core paths never touch the heap.
 *
 * Build: cc -Iinclude tests/test_memory.c [-DSS_USE_STATIC_MEMORY=1]
 *        [-DSS_TEST_IMPL='"../src/ss_lib_c89.c"']
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>

void* test_malloc(size_t size);
void* test_calloc(size_t count, size_t size);
void test_free(void* ptr);

#define SS_MALLOC(size) test_malloc(size)
#define SS_CALLOC(count, size) test_calloc((count), (size))
#define SS_FREE(ptr) test_free(ptr)

#ifndef SS_TEST_IMPL
#define SS_TEST_IMPL "../src/ss_lib.c"
#endif
#include SS_TEST_IMPL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MAX_LIVE 4096

static void* g_live[MAX_LIVE];
static size_t g_live_count = 0;
static unsigned long g_total_allocs = 0;

static void track(void* ptr) {
    assert(g_live_count < MAX_LIVE);
    g_live[g_live_count++] = ptr;
    g_total_allocs++;
}

void* test_malloc(size_t size) {
    void* ptr = malloc(size ? size : 1);
    if (ptr) track(ptr);
    return ptr;
}

void* test_calloc(size_t count, size_t size) {
    void* ptr = calloc(count ? count : 1, size ? size : 1);
    if (ptr) track(ptr);
    return ptr;
}

void test_free(void* ptr) {
    size_t i;
    if (!ptr) return;
    for (i = 0; i < g_live_count; i++) {
        if (g_live[i] == ptr) {
            g_live[i] = g_live[--g_live_count];
            free(ptr);
            return;
        }
    }
    fprintf(stderr, "FAIL: SS_FREE(%p) of memory not from SS_MALLOC/SS_CALLOC\n", ptr);
    abort();
}

static int g_calls = 0;

static void count_slot(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
    g_calls++;
}

/* Exercise every path that registers, connects, emits, defers or tears
 * down, leaving deferred entries pending at cleanup. */
static void run_core_paths(void) {
    ss_data_t s;
    ss_connection_t handle = 0;

    memset(&s, 0, sizeof(s));
    s.type = SS_TYPE_STRING;
    s.value.s_val = "payload";

    assert(ss_init() == SS_OK);
    assert(ss_set_namespace("mem") == SS_OK);
    assert(ss_signal_register_ex("a", "first signal", SS_PRIORITY_HIGH) == SS_OK);
    assert(ss_signal_register("b") == SS_OK);
    assert(ss_connect("a", count_slot, NULL) == SS_OK);
    assert(ss_connect_ex("a", count_slot, NULL, SS_PRIORITY_LOW, &handle) == SS_OK);
    assert(ss_connect("b", count_slot, NULL) == SS_OK);
    assert(ss_emit_string("a", "hello") == SS_OK);
    assert(ss_emit_deferred("a", &s) == SS_OK);
    assert(ss_flush_deferred() == SS_OK);
#if SS_ENABLE_CUSTOM_DATA
    {
        int value = 7;
        ss_data_t c;
        memset(&c, 0, sizeof(c));
        c.type = SS_TYPE_CUSTOM;
        c.custom_data = &value;
        c.size = sizeof(value);
        assert(ss_emit_deferred("b", &c) == SS_OK);
    }
#endif
    assert(ss_emit_deferred("b", &s) == SS_OK);       /* left pending */
    assert(ss_disconnect_handle(handle) == SS_OK);
    assert(ss_disconnect("a", count_slot) == SS_OK);
    assert(ss_signal_unregister("a") == SS_OK);
    assert(ss_set_namespace(NULL) == SS_OK);
    ss_cleanup();
}

/* Paths whose job is to hand the caller an allocated object */
static void run_allocating_paths(void) {
    ss_batch_t* batch;
    ss_data_t* data;
    ss_data_t s;
    int value = 3;

    memset(&s, 0, sizeof(s));
    s.type = SS_TYPE_STRING;
    s.value.s_val = "batched";

    assert(ss_init() == SS_OK);
    assert(ss_signal_register_ex("c", "described", SS_PRIORITY_NORMAL) == SS_OK);
    assert(ss_connect("c", count_slot, NULL) == SS_OK);

#if SS_ENABLE_INTROSPECTION
    {
        ss_signal_info_t* list = NULL;
        size_t count = 0;
        assert(ss_get_signal_list(&list, &count) == SS_OK);
        assert(count == 1);
        ss_free_signal_list(list, count);
    }
#endif

    batch = ss_batch_create();
    assert(batch != NULL);
    assert(ss_batch_add(batch, "c", &s) == SS_OK);
    assert(ss_batch_emit(batch) == SS_OK);
    assert(ss_batch_add(batch, "c", &s) == SS_OK);   /* left pending */
    ss_batch_destroy(batch);

    data = ss_data_create(SS_TYPE_STRING);
    assert(data != NULL);
    assert(ss_data_set_string(data, "one") == SS_OK);
    assert(ss_data_set_string(data, "two") == SS_OK);
#if SS_ENABLE_CUSTOM_DATA
    assert(ss_data_set_custom(data, &value, sizeof(value), NULL) == SS_OK);
#endif
    (void)value;
    ss_data_destroy(data);

    ss_cleanup();
}

int main(void) {
    printf("Starting allocation accounting tests (%s memory)\n",
           SS_USE_STATIC_MEMORY ? "static" : "dynamic");

    g_total_allocs = 0;
    run_core_paths();
    assert(g_calls > 0);
    assert(g_live_count == 0);
#if SS_USE_STATIC_MEMORY && SS_STATIC_CONTEXT
    /* README/configuration.md promise: no heap use in static mode */
    assert(g_total_allocs == 0);

    /* Fixed-size buffers report overflow instead of allocating */
    {
        char long_text[SS_MAX_DESCRIPTION_LENGTH + 8];
        memset(long_text, 'd', sizeof(long_text) - 1);
        long_text[sizeof(long_text) - 1] = '\0';
        assert(ss_init() == SS_OK);
        assert(ss_signal_register_ex("d", long_text, SS_PRIORITY_NORMAL)
               == SS_ERR_WOULD_OVERFLOW);
        assert(ss_signal_exists("d") == 0);
        ss_cleanup();
        assert(g_total_allocs == 0);
    }
#endif

    run_allocating_paths();
    assert(g_live_count == 0);

    printf("All allocation accounting tests passed (%lu allocations, 0 leaked)\n",
           g_total_allocs);
    return 0;
}
