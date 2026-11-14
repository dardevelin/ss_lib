/* Built repeatedly in one directory by scripts/check_build_config.sh to
 * catch a library left over from other flags. Prints the caller's
 * SS_ENABLE_THREAD_SAFETY and the library's: ss_bus_create() accepts
 * SS_BUS_THREAD_SAFE only from a library built with it. */
#include "ss_lib.h"
#include <stdio.h>

int main(void) {
    ss_bus_t* bus = NULL;
    ss_error_t err = ss_bus_create(&bus, SS_BUS_THREAD_SAFE);
    if (err != SS_OK && err != SS_ERR_CONFIG_MISMATCH) return 1;
    printf("caller=%d library=%d\n", SS_ENABLE_THREAD_SAFETY, err == SS_OK);
    ss_bus_destroy(bus);
    return 0;
}
