/*
 * Allocation accounting tests (C89).
 *
 * The implementation is compiled into this file with SS_MALLOC, SS_CALLOC
 * and SS_FREE routed to a tracking allocator, so the tests can prove that:
 *   - every allocation is released by the matching free (no leaks),
 *   - every pointer the library frees came from SS_MALLOC/SS_CALLOC
 *     (the default SS_STRDUP must not bypass a custom allocator),
 *   - with SS_USE_STATIC_MEMORY the core paths never touch the heap,
 *   - an ss_data_t frees only the payload it owns, exactly once.
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
static unsigned long g_total_frees = 0;

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
            g_total_frees++;
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

static int is_live(const void* ptr) {
    size_t i;
    for (i = 0; i < g_live_count; i++) {
        if (g_live[i] == ptr) return 1;
    }
    return 0;
}

static int g_borrowed_seen = 0;

static void slot_check_borrowed(const ss_data_t* data, void* user_data) {
    (void)user_data;
    assert(data->owned == 0);
    g_borrowed_seen++;
}

/* Borrowing setters, and clearing borrowed data, never touch the heap in
 * either memory mode. */
static void run_borrowing_paths(void) {
    ss_data_t d = SS_DATA_INIT;
    ss_data_t junk;
    char text[8];
    unsigned long allocs = g_total_allocs;
    unsigned long frees = g_total_frees;

    strcpy(text, "stack");
    assert(d.type == SS_TYPE_VOID && d.owned == 0);

    memset(&junk, 0xA5, sizeof(junk));     /* ss_data_init accepts any content */
    ss_data_init(&junk);
    assert(junk.type == SS_TYPE_VOID && junk.owned == 0);
    ss_data_clear(&junk);
    ss_data_init(NULL);
    ss_data_clear(NULL);

    assert(ss_data_set_string_ref(&d, text) == SS_OK);
    assert(d.type == SS_TYPE_STRING && d.owned == 0);
    assert(ss_data_get_string(&d) == text);
    assert(ss_data_set_int(&d, 1) == SS_OK);
    assert(ss_data_set_string_ref(&d, text) == SS_OK);
    assert(ss_data_set_string_ref(&d, NULL) == SS_OK);
    assert(d.type == SS_TYPE_STRING && ss_data_get_string(&d) == NULL);
    assert(ss_data_set_string_ref(&d, text) == SS_OK);
    ss_data_clear(&d);
    assert(d.type == SS_TYPE_VOID && d.owned == 0);
    assert(ss_data_set_string_ref(NULL, text) == SS_ERR_NULL_PARAM);
#if SS_ENABLE_CUSTOM_DATA
    {
        int value = 5;
        size_t size = 0;
        assert(ss_data_set_custom_ref(&d, &value, sizeof(value)) == SS_OK);
        assert(d.type == SS_TYPE_CUSTOM && d.owned == 0);
        assert(ss_data_get_custom(&d, &size) == &value && size == sizeof(value));
        assert(ss_data_set_string_ref(&d, text) == SS_OK);
        assert(ss_data_set_custom_ref(&d, &value, sizeof(value)) == SS_OK);
        ss_data_clear(&d);
        assert(d.type == SS_TYPE_VOID);
        assert(ss_data_set_custom_ref(&d, NULL, sizeof(value)) == SS_ERR_NULL_PARAM);
        assert(ss_data_set_custom_ref(&d, &value, 0) == SS_ERR_NULL_PARAM);
        assert(value == 5);
    }
#endif
    assert(strcmp(text, "stack") == 0);
    assert(g_total_allocs == allocs);
    assert(g_total_frees == frees);
}

#if SS_ENABLE_CUSTOM_DATA
typedef struct { int a; int b; } pair_t;

static int g_cleanups = 0;
static const void* g_cleanup_ptr = NULL;
static size_t g_cleanup_size = 0;

/* Gets the library's copy, read-only, while it is still allocated */
static void record_cleanup(const void* data, size_t size) {
    const pair_t* p = (const pair_t*)data;
    assert(is_live(data));
    assert(size == sizeof(pair_t) && p->a == 1 && p->b == 2);
    g_cleanups++;
    g_cleanup_ptr = data;
    g_cleanup_size = size;
}
#endif

/* The copying setters own their copy: it is freed exactly once, by the
 * next setter, ss_data_clear() or ss_data_destroy(). */
static void run_owning_paths(void) {
    ss_data_t d = SS_DATA_INIT;
    ss_data_t* created;
    const char* copy;
    unsigned long frees;

    assert(ss_data_set_string(&d, "owned") == SS_OK);
    assert(d.type == SS_TYPE_STRING && d.owned != 0);
    copy = ss_data_get_string(&d);
    assert(is_live(copy) && strcmp(copy, "owned") == 0);
    frees = g_total_frees;
    ss_data_clear(&d);
    assert(g_total_frees == frees + 1 && !is_live(copy));
    assert(d.type == SS_TYPE_VOID && d.owned == 0);
    ss_data_clear(&d);
    assert(g_total_frees == frees + 1);

    /* Each setter releases the owned payload it replaces, once */
    assert(ss_data_set_string(&d, "one") == SS_OK);
    frees = g_total_frees;
    assert(ss_data_set_string(&d, "two") == SS_OK);
    assert(g_total_frees == frees + 1);
    assert(ss_data_set_string(&d, NULL) == SS_OK);
    assert(g_total_frees == frees + 2 && d.owned == 0);
    assert(ss_data_set_string(&d, "three") == SS_OK);
    assert(ss_data_set_string_ref(&d, "four") == SS_OK);
    assert(g_total_frees == frees + 3 && d.owned == 0);
    ss_data_clear(&d);
    assert(g_total_frees == frees + 3);

    /* A _ref setter given the object's own payload leaves it owned */
    assert(ss_data_set_string(&d, "self") == SS_OK);
    copy = ss_data_get_string(&d);
    frees = g_total_frees;
    assert(ss_data_set_string_ref(&d, copy) == SS_OK);
    assert(g_total_frees == frees && d.owned != 0 && is_live(copy));
    ss_data_clear(&d);
    assert(g_total_frees == frees + 1 && !is_live(copy));

    /* ss_data_destroy is ss_data_clear plus freeing the object */
    created = ss_data_create(SS_TYPE_VOID);
    assert(created != NULL && created->owned == 0);
    assert(ss_data_set_string(created, "five") == SS_OK);
    frees = g_total_frees;
    ss_data_destroy(created);
    assert(g_total_frees == frees + 2);
    ss_data_destroy(NULL);

#if SS_ENABLE_CUSTOM_DATA
    {
        const pair_t pair = {1, 2};
        void* custom;
        size_t size = 0;

        /* The cleanup gets the copy once, as const, before it is freed */
        g_cleanups = 0;
        assert(ss_data_set_custom(&d, &pair, sizeof(pair), record_cleanup) == SS_OK);
        assert(d.type == SS_TYPE_CUSTOM && d.owned != 0);
        custom = ss_data_get_custom(&d, &size);
        assert(custom != (const void*)&pair && is_live(custom) && size == sizeof(pair));
        frees = g_total_frees;
        assert(ss_data_set_int(&d, 7) == SS_OK);
        assert(g_cleanups == 1 && g_cleanup_ptr == custom);
        assert(g_cleanup_size == sizeof(pair));
        assert(g_total_frees == frees + 1 && !is_live(custom));
        ss_data_clear(&d);
        assert(g_cleanups == 1);

        assert(ss_data_set_custom(&d, &pair, sizeof(pair), record_cleanup) == SS_OK);
        ss_data_clear(&d);
        ss_data_clear(&d);
        assert(g_cleanups == 2);

        assert(ss_data_set_custom(&d, &pair, sizeof(pair), record_cleanup) == SS_OK);
        assert(ss_data_set_custom_ref(&d, &g_cleanups, sizeof(g_cleanups)) == SS_OK);
        assert(g_cleanups == 3 && d.owned == 0);
        ss_data_clear(&d);
        assert(g_cleanups == 3);

        created = ss_data_create(SS_TYPE_CUSTOM);
        assert(created != NULL);
        assert(ss_data_set_custom(created, &pair, sizeof(pair), record_cleanup) == SS_OK);
        frees = g_total_frees;
        ss_data_destroy(created);
        assert(g_cleanups == 4 && g_total_frees == frees + 2);

        /* Own payload through the _ref setter: kept, cleaned up once later */
        assert(ss_data_set_custom(&d, &pair, sizeof(pair), record_cleanup) == SS_OK);
        custom = ss_data_get_custom(&d, &size);
        frees = g_total_frees;
        assert(ss_data_set_custom_ref(&d, custom, size) == SS_OK);
        assert(g_cleanups == 4 && g_total_frees == frees && d.owned != 0);
        assert(ss_data_get_custom(&d, NULL) == custom && is_live(custom));
        /* Another size is another payload: the copy is released as usual,
         * leaving a borrowed (dangling, never read) pointer */
        assert(ss_data_set_custom_ref(&d, custom, size - 1) == SS_OK);
        assert(g_cleanups == 5 && g_total_frees == frees + 1 && d.owned == 0);
        ss_data_clear(&d);
        assert(g_total_frees == frees + 1);

        /* Slots and queued copies borrow; the cleanup is never called on them */
        assert(ss_init() == SS_OK);
        assert(ss_signal_register("own") == SS_OK);
        assert(ss_connect("own", slot_check_borrowed, NULL) == SS_OK);
        assert(ss_data_set_custom(&d, &pair, sizeof(pair), record_cleanup) == SS_OK);
        g_borrowed_seen = 0;
        assert(ss_emit("own", &d) == SS_OK);
        assert(ss_emit_deferred("own", &d) == SS_OK);
        assert(ss_flush_deferred() == SS_OK);
        assert(ss_emit_deferred("own", &d) == SS_OK);    /* left pending */
        ss_cleanup();
        assert(g_borrowed_seen == 2 && g_cleanups == 5);
        ss_data_clear(&d);
        assert(g_cleanups == 6);
    }
#endif

    /* Slots see an owned string as borrowed */
    assert(ss_init() == SS_OK);
    assert(ss_signal_register("own") == SS_OK);
    assert(ss_connect("own", slot_check_borrowed, NULL) == SS_OK);
    assert(ss_data_set_string(&d, "seen") == SS_OK);
    g_borrowed_seen = 0;
    assert(ss_emit("own", &d) == SS_OK);
    assert(ss_emit_deferred("own", &d) == SS_OK);
    assert(ss_flush_deferred() == SS_OK);
    assert(g_borrowed_seen == 2);
    ss_cleanup();
    ss_data_clear(&d);
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

    run_borrowing_paths();
    assert(g_live_count == 0);
#if SS_USE_STATIC_MEMORY && SS_STATIC_CONTEXT
    assert(g_total_allocs == 0);
#endif

    run_owning_paths();
    assert(g_live_count == 0);

    run_allocating_paths();
    assert(g_live_count == 0);

    printf("All allocation accounting tests passed (%lu allocations, 0 leaked)\n",
           g_total_allocs);
    return 0;
}
