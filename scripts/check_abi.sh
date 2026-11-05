#!/bin/sh
# No configuration macro may change a public type layout or enum value, so a
# caller and a library built with different flags disagree only on which
# functions are declared (a compile or link error), never on memory layout.
#
#   scripts/check_abi.sh [CC...]     default: $CC or cc
#
# 1. include/ss_lib.h has no preprocessor conditional inside the body of a
#    struct, union or enum.
# 2. A probe that sees only the public header prints the size and member
#    offsets of every public struct, and every public enum value. It is
#    compiled and run in every configuration of scripts/check_standards.sh
#    (plus EXTRA_CONFIGS below), as C89 and as the newest standard the
#    compiler knows, and every run must print the same values. A type that a
#    feature flag leaves out entirely is compared between the configurations
#    that declare it.
#
# Add new public types and members to the probe below.
set -eu

cd "$(dirname "$0")/.."

[ $# -gt 0 ] || set -- "${CC:-cc}"

# A second configuration for the types that only some configurations declare
# (the statistics structs), and custom data alone switched off
EXTRA_CONFIGS="
-DSS_ENABLE_CUSTOM_DATA=0
-DSS_EMBEDDED_BUILD -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failed=0

# 1. Conditionals inside a type body
if ! awk '
    depth == 0 && /^[ \t]*(typedef[ \t]+)?(struct|union|enum)([ \t][^;]*)?\{/ { depth = 0; intype = 1 }
    intype {
        if ($0 ~ /^[ \t]*#/) {
            printf "include/ss_lib.h:%d: preprocessor line inside a public type: %s\n", NR, $0
            bad = 1
        }
        line = $0
        depth += gsub(/\{/, "", line) - gsub(/\}/, "", line)
        if (depth <= 0) { depth = 0; intype = 0 }
    }
    END { exit bad }
' include/ss_lib.h >&2; then
    failed=1
fi

# 2. The probe
cat > "$tmp/probe.c" <<'EOF'
#include <stdio.h>
#include <stddef.h>
#include "ss_lib.h"

#define SIZE(t) printf("sizeof(%s) %lu\n", #t, (unsigned long)sizeof(t))
#define MEMBER(t, m) printf("%s.%s offset %lu size %lu\n", #t, #m, \
    (unsigned long)offsetof(t, m), (unsigned long)sizeof(((t*)0)->m))
#define VALUE(e) printf("%s %ld\n", #e, (long)(e))

int main(void) {
    SIZE(ss_error_t);
    VALUE(SS_OK);
    VALUE(SS_ERR_NULL_PARAM);
    VALUE(SS_ERR_MEMORY);
    VALUE(SS_ERR_NOT_FOUND);
    VALUE(SS_ERR_ALREADY_EXISTS);
    VALUE(SS_ERR_INVALID_TYPE);
    VALUE(SS_ERR_BUFFER_TOO_SMALL);
    VALUE(SS_ERR_MAX_SLOTS);
    VALUE(SS_ERR_TIMEOUT);
    VALUE(SS_ERR_WOULD_OVERFLOW);
    VALUE(SS_ERR_ISR_UNSAFE);
    VALUE(SS_ERR_CONFIG_MISMATCH);

    SIZE(ss_data_type_t);
    VALUE(SS_TYPE_VOID);
    VALUE(SS_TYPE_INT);
    VALUE(SS_TYPE_FLOAT);
    VALUE(SS_TYPE_DOUBLE);
    VALUE(SS_TYPE_STRING);
    VALUE(SS_TYPE_POINTER);

    SIZE(ss_priority_t);
    VALUE(SS_PRIORITY_LOW);
    VALUE(SS_PRIORITY_NORMAL);
    VALUE(SS_PRIORITY_HIGH);
    VALUE(SS_PRIORITY_CRITICAL);

    SIZE(ss_cleanup_func_t);
    SIZE(ss_slot_func_t);
    SIZE(ss_connection_t);

    SIZE(ss_data_t);
    MEMBER(ss_data_t, type);
    MEMBER(ss_data_t, owned);
    MEMBER(ss_data_t, value);
    MEMBER(ss_data_t, value.i_val);
    MEMBER(ss_data_t, value.f_val);
    MEMBER(ss_data_t, value.d_val);
    MEMBER(ss_data_t, value.s_val);
    MEMBER(ss_data_t, value.p_val);
    VALUE(SS_ABI_TAG);

#if SS_ENABLE_INTROSPECTION
    SIZE(ss_signal_info_t);
    MEMBER(ss_signal_info_t, name);
    MEMBER(ss_signal_info_t, slot_count);
    MEMBER(ss_signal_info_t, description);
    MEMBER(ss_signal_info_t, priority);
#endif

#if SS_ENABLE_MEMORY_STATS
    SIZE(ss_memory_stats_t);
    MEMBER(ss_memory_stats_t, signals_allocated);
    MEMBER(ss_memory_stats_t, signals_used);
    MEMBER(ss_memory_stats_t, slots_allocated);
    MEMBER(ss_memory_stats_t, slots_used);
    MEMBER(ss_memory_stats_t, total_bytes_allocated);
    MEMBER(ss_memory_stats_t, peak_bytes_allocated);
    MEMBER(ss_memory_stats_t, string_bytes);
#endif

#if SS_ENABLE_PERFORMANCE_STATS
    SIZE(ss_perf_stats_t);
    MEMBER(ss_perf_stats_t, total_emissions);
    MEMBER(ss_perf_stats_t, total_time_ns);
    MEMBER(ss_perf_stats_t, avg_time_ns);
    MEMBER(ss_perf_stats_t, max_time_ns);
    MEMBER(ss_perf_stats_t, min_time_ns);
#endif
    return 0;
}
EOF

# Configurations as words, spaces replaced by @; the default is ""
configs=$({ scripts/check_standards.sh --configs; printf '%s\n' "$EXTRA_CONFIGS"; } |
          sed -e '/^$/d' -e 's/ /@/g')

for cc in "$@"; do
    newest=$(scripts/check_standards.sh --newest "$cc")
    stds="c89"
    [ "$newest" = c89 ] || stds="$stds $newest"
    : > "$tmp/values"
    runs=0
    for std in $stds; do
        for cfg in "" $configs; do
            flags=$(echo "$cfg" | sed 's/@/ /g')
            # $flags holds several flags and must be word-split
            # shellcheck disable=SC2086
            if $cc -std="$std" -Wall -Wextra -pedantic -Werror -Iinclude $flags \
                   "$tmp/probe.c" -o "$tmp/probe" && "$tmp/probe" > "$tmp/out"; then
                # Each line: <run label without spaces> <key> <value...>
                sed "s|^|-std=$std${cfg:+@$cfg} |" "$tmp/out" >> "$tmp/values"
                runs=$((runs + 1))
            else
                echo "FAIL: $cc -std=$std $flags: probe does not build or run" >&2
                failed=1
            fi
        done
    done
    if ! awk -v cc="$cc" '
        {
            label = $1; key = $2
            value = $0; sub(/^[^ ]+ [^ ]+ /, "", value)
            gsub(/@/, " ", label)
            if (!(key in first)) { first[key] = value; where[key] = label; next }
            if (value != first[key] && !((key, value) in reported)) {
                reported[key, value] = 1
                printf "%s: %s differs between configurations:\n", cc, key
                printf "    %s with %s\n    %s with %s\n", first[key], where[key], value, label
                bad = 1
            }
        }
        END { exit bad }
    ' "$tmp/values" >&2; then
        failed=1
    fi
    echo "$cc: probe built and run in $runs configurations"
done

[ "$failed" -eq 0 ] || { echo "ABI check failed: public layouts depend on the configuration" >&2; exit 1; }
echo "ABI check passed: public layouts and enum values are the same in every configuration"
