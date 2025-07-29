#include "ss_lib_v2.h"
#include <stdio.h>

void on_test(const ss_data_t* data, void* user_data) {
    printf("Signal received! Value: %d\n", ss_data_get_int(data, 0));
}

int main(void) {
    printf("Testing V2 Library\n");
    
    if (ss_init() != SS_OK) {
        printf("Failed to init\n");
        return 1;
    }
    
    ss_signal_register("test");
    ss_connect("test", on_test, NULL);
    
    for (int i = 0; i < 5; i++) {
        ss_emit_int("test", i);
    }
    
    ss_cleanup();
    printf("Done!\n");
    
    return 0;
}