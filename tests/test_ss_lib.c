#include "ss_lib.h"
#include <stdio.h>
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

int main(void) {
    printf("Starting Signal-Slot Library Tests\n");
    printf("==================================\n");
    
    test_basic_functionality();
    test_data_types();
    test_multiple_slots();
    test_signal_introspection();
    test_error_handling();
    test_thread_safety_config();
    
    printf("\n==================================\n");
    printf("All tests passed successfully!\n");
    
    return 0;
}
