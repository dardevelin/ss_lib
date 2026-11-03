#!/bin/sh
# Check a `make install` the way a user meets it: compile a program with the
# flags from the installed ss_lib.pc, link it against the installed shared
# library and run it. On macOS it must start without library-path variables;
# on Linux a non-system prefix needs LD_LIBRARY_PATH, as for any library.
# Usage: scripts/check_install.sh PREFIX [CC]
set -eu

prefix=$1
cc=${2:-cc}
pc="$prefix/lib/pkgconfig/ss_lib.pc"
[ -f "$pc" ] || { echo "no $pc" >&2; exit 1; }

# The Cflags pkg-config would print, without needing pkg-config
cflags=$(sed -n 's/^Cflags: //p' "$pc" | sed "s|\${includedir}|$prefix/include|")

cat > "$prefix/check.c" <<'C'
#include <ss_lib.h>
#include <stdio.h>
static int calls = 0;
static void on_tick(const ss_data_t* data, void* user_data) {
    (void)user_data;
    calls += ss_data_get_int(data, 0);
}
int main(void) {
    if (ss_init_abi(SS_ABI_TAG) != SS_OK) return 1;
    if (ss_signal_register("tick") != SS_OK) return 1;
    if (ss_connect("tick", on_tick, NULL) != SS_OK) return 1;
    if (ss_emit_int("tick", 2) != SS_OK) return 1;
    ss_cleanup();
    printf("installed SS_Lib %s works\n", ss_version());
    return calls == 2 ? 0 : 1;
}
C

# $cflags holds several flags and must be word-split
# shellcheck disable=SC2086
"$cc" "$prefix/check.c" $cflags -L"$prefix/lib" -lss_lib -pthread -o "$prefix/check"

case "$(uname -s)" in
    Darwin)
        # The program must find the library through its install name alone
        env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH "$prefix/check" ;;
    *)
        LD_LIBRARY_PATH="$prefix/lib" "$prefix/check" ;;
esac
