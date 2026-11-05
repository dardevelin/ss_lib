#include "ss_lib.h"
#include <stdio.h>

void on_test(const ss_data_t* data, void* user_data) {
    (void)user_data;
    printf("Signal received! Value: %d\n", ss_data_get_int(data, 0));
}

int main(void) {
    ss_bus_t* bus = NULL;
    printf("Testing SS_Lib\n");

    if (ss_bus_create(&bus, 0) != SS_OK) {
        printf("Failed to create the bus\n");
        return 1;
    }

    ss_signal_register(bus, "test");
    ss_connect(bus, "test", on_test, NULL);

    for (int i = 0; i < 5; i++) {
        ss_emit_int(bus, "test", i);
    }

    ss_bus_destroy(bus);
    printf("Done!\n");

    return 0;
}
