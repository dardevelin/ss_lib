#include "ss_lib.h"
#include <stdio.h>
#include <assert.h>

static int g_counter = 0;

void on_signal(const ss_data_t* data, void* user_data) {
    (void)data;  // unused
    (void)user_data;  // unused
    g_counter++;
}

void on_int_signal(const ss_data_t* data, void* user_data) {
    (void)user_data;  // unused
    int value = ss_data_get_int(data, 0);
    printf("Received int: %d\n", value);
    g_counter += value;
}

int main(void) {
    printf("=== Basic Signal-Slot Tests ===\n");
    
    // Initialize
    assert(ss_init() == SS_OK);
    
    // Register signal
    assert(ss_signal_register("test") == SS_OK);
    assert(ss_signal_exists("test") == 1);
    
    // Connect slot
    assert(ss_connect("test", on_signal, NULL) == SS_OK);
    
    // Emit signal
    assert(ss_emit_void("test") == SS_OK);
    assert(g_counter == 1);
    
    // Test int signal
    assert(ss_signal_register("int_test") == SS_OK);
    assert(ss_connect("int_test", on_int_signal, NULL) == SS_OK);
    assert(ss_emit_int("int_test", 5) == SS_OK);
    assert(g_counter == 6);
    
    // Test string signal
    assert(ss_signal_register("string_test") == SS_OK);
    assert(ss_emit_string("string_test", "Hello, World!") == SS_OK);
    
    // Cleanup
    ss_cleanup();
    
    printf("All tests passed!\n");
    return 0;
}