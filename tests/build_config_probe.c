/* Built repeatedly in one directory to catch stale library/caller ABIs. */
#include "ss_lib.h"
#include <stdio.h>
#include <string.h>

static int delivered;

static void receive(const ss_data_t* data, void* user_data) {
    (void)user_data;
    if (ss_data_get_int(data, 0) == 42) delivered++;
}

int main(void) {
    ss_data_t data;
    memset(&data, 0, sizeof(data));
    data.type = SS_TYPE_INT;
    data.value.i_val = 42;
    /* Exercise the ordinary initializer too: a mismatched library copies
     * beyond this stack object when its custom-data layout is larger. */
    if (ss_init() != SS_OK || ss_signal_register("probe") != SS_OK ||
        ss_connect("probe", receive, NULL) != SS_OK ||
        ss_emit_deferred("probe", &data) != SS_OK ||
        ss_flush_deferred() != SS_OK || delivered != 1) return 1;
    ss_cleanup();
    if (ss_init_abi(SS_ABI_TAG) != SS_OK) return 2;
    ss_cleanup();
    printf("custom_data=%d\n", SS_ENABLE_CUSTOM_DATA);
    return 0;
}
