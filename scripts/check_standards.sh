#!/bin/sh
# Compile the library, a caller of the public header and the single header
# with -Werror as every C standard from C89 to the newest the compiler knows.
#
#   scripts/check_standards.sh [CC...]        default: $CC or cc
#   scripts/check_standards.sh --newest CC    print the newest -std CC accepts
#   scripts/check_standards.sh --configs      print the configurations, one
#                                             per line, the default (empty) first
#
# Standards a compiler does not know are reported and skipped, so the same
# script runs on old toolchains and picks up new standards on new ones.
set -eu

cd "$(dirname "$0")/.."

# Newest first; c2x is the pre-C23 spelling of c23
NEWEST_FIRST="c2y c23 c2x c17 c11 c99 c89"

supports() {
    echo 'int x;' | "$1" -std="$2" -Werror -x c -c - -o /dev/null 2>/dev/null
}

newest() {
    for std in $NEWEST_FIRST; do
        if supports "$1" "$std"; then echo "$std"; return 0; fi
    done
    echo "$1 supports none of: $NEWEST_FIRST" >&2
    return 1
}

# The default configuration is the empty first line; scripts/check_abi.sh
# reads this list with --configs
CONFIGS="
-DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1 -DSS_ENABLE_DEBUG_TRACE=1
-DSS_USE_STATIC_MEMORY=1 -DSS_ENABLE_ISR_SAFE=1
-DSS_MINIMAL_BUILD
-DSS_EMBEDDED_BUILD -DSS_ENABLE_ISR_SAFE=1"

if [ "${1:-}" = "--newest" ]; then
    newest "${2:-${CC:-cc}}"
    exit
fi

if [ "${1:-}" = "--configs" ]; then
    printf '%s\n' "$CONFIGS"
    exit
fi

[ $# -gt 0 ] || set -- "${CC:-cc}"

WARN="-Wall -Wextra -pedantic -Werror -Wstrict-prototypes"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# A caller that only sees the public header
cat > "$tmp/caller.c" <<'EOF'
#include "ss_lib.h"
static void on_value(const ss_data_t* data, void* user_data) {
    (void)data;
    (void)user_data;
}
#if SS_ENABLE_CUSTOM_DATA
static void on_release(const void* data, size_t size) {
    (void)data;
    (void)size;
}
#endif
int main(void) {
    ss_data_t data = SS_DATA_INIT;
    ss_bus_t* bus = 0;
    static unsigned char pool[1];
    ss_bus_t* pooled = 0;
    if (ss_bus_create(&bus, SS_BUS_THREAD_SAFE) != SS_OK &&
        ss_bus_create(&bus, 0) != SS_OK) return 1;
    if (ss_bus_create_in(&pooled, pool, sizeof(pool), 0) != SS_ERR_BUFFER_TOO_SMALL) return 1;
    if (ss_bus_pool_size() == 0) return 1;
    if (ss_signal_register(bus, "value") != SS_OK) return 1;
    if (ss_connect(bus, "value", on_value, 0) != SS_OK) return 1;
    if (ss_emit_int(bus, "value", 1) != SS_OK) return 1;
    if (ss_data_set_string_ref(&data, "borrowed") != SS_OK) return 1;
    if (ss_emit(bus, "value", &data) != SS_OK) return 1;
#if SS_ENABLE_CUSTOM_DATA
    if (ss_data_set_custom(&data, "copied", 7, on_release) != SS_OK) return 1;
#endif
    ss_data_clear(&data);
    ss_bus_destroy(bus);
    return 0;
}
EOF
printf '#define SS_IMPLEMENTATION\n#include "ss_lib_single.h"\n' > "$tmp/single.c"

failed=0
for cc in "$@"; do
    stds="c89 c99 c11 c17"
    if supports "$cc" c23; then stds="$stds c23"
    elif supports "$cc" c2x; then stds="$stds c2x"
    else echo "$cc: no C23 support, skipped"
    fi
    if supports "$cc" c2y; then stds="$stds c2y"
    else echo "$cc: no C2y support, skipped"
    fi

    for std in $stds; do
        n=0
        # Default configuration first (the empty line of CONFIGS)
        for cfg in "" $(echo "$CONFIGS" | sed 's/ /@/g'); do
            cfg=$(echo "$cfg" | sed 's/@/ /g')
            # $cfg holds several flags and must be word-split
            # shellcheck disable=SC2086
            for src in src/ss_lib.c "$tmp/caller.c" "$tmp/single.c"; do
                if ! $cc -std="$std" $WARN -Iinclude $cfg -c "$src" -o "$tmp/out.o"; then
                    echo "FAIL: $cc -std=$std $cfg $src" >&2
                    failed=1
                fi
                n=$((n + 1))
            done
        done
        echo "$cc -std=$std: $n compiles"
    done
done

[ "$failed" -eq 0 ] || exit 1
echo "C standards check passed"
