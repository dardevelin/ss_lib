#include "ss_lib.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int g_test_counter = 0;

void test_slot_void(const ss_data_t* data, void* user_data) {
    assert(data->type == SS_TYPE_VOID);
    int* counter = (int*)user_data;
    (*counter)++;
    printf("Void signal received, counter = %d\n", *counter);
}

void test_slot_int(const ss_data_t* data, void* user_data) {
    assert(data->type == SS_TYPE_INT);
    int value = ss_data_get_int(data, 0);
    printf("Int signal received: %d\n", value);
    g_test_counter += value;
}

void test_slot_string(const ss_data_t* data, void* user_data) {
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

    /* Connect other_slot first, then disconnect_other_slot.
       Head insertion means disconnect_other_slot fires first during emit. */
    assert(ss_connect_ex("emit_test", other_slot, NULL,
                         SS_PRIORITY_NORMAL, &g_handle_to_disconnect) == SS_OK);
    ss_connection_t handle1;
    assert(ss_connect_ex("emit_test", disconnect_other_slot, NULL,
                         SS_PRIORITY_NORMAL, &handle1) == SS_OK);

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

    ss_cleanup();
    printf("Input validation tests passed!\n");
}

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

#if SS_ENABLE_ISR_SAFE
void test_isr_null_signal(void) {
    printf("\n=== Testing ISR NULL Signal ===\n");

    assert(ss_init() == SS_OK);
    assert(ss_emit_from_isr(NULL, 42) == SS_ERR_NULL_PARAM);
    ss_cleanup();

    printf("ISR NULL signal tests passed!\n");
}
#endif

int main(void) {
    printf("Starting Signal-Slot Library Tests\n");
    printf("==================================\n");

    test_basic_functionality();
    test_data_types();
    test_multiple_slots();
    test_signal_introspection();
    test_error_handling();
    test_disconnect_during_emit();
    test_input_validation();
    test_thread_safety_config();
#if SS_ENABLE_ISR_SAFE
    test_isr_null_signal();
#endif
    
    printf("\n==================================\n");
    printf("All tests passed successfully!\n");
    
    return 0;
}
