#!/bin/sh
# src/ss_lib_c89.c is the C89 copy of src/ss_lib.c. Both are written in the
# common subset of C89 and C11, so everything from the feature-macro marker
# on must be identical. Fails, printing the difference, when a change
# reached only one file. With --fix, copies the body of ss_lib.c over.
set -e
cd "$(dirname "$0")/.."
marker='SS_FEATURE_MACROS_BEGIN'
body() { sed -n "/$marker/,\$p" "$1"; }
header() { sed -n "1,/$marker/p" "$1" | sed '$d'; }

if [ "$1" = "--fix" ]; then
    { header src/ss_lib_c89.c; body src/ss_lib.c; } > src/ss_lib_c89.c.tmp
    mv src/ss_lib_c89.c.tmp src/ss_lib_c89.c
    echo "src/ss_lib_c89.c updated from src/ss_lib.c"
    exit 0
fi

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
body src/ss_lib.c > "$tmp"
if body src/ss_lib_c89.c | diff -u "$tmp" -; then
    echo "src/ss_lib_c89.c is in sync with src/ss_lib.c"
else
    echo "src/ss_lib_c89.c differs from src/ss_lib.c: port the change or run $0 --fix" >&2
    exit 1
fi
