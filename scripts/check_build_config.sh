#!/bin/sh
# make check-build-config: build into one directory with other flags and
# check that the library is rebuilt with them, and that an unchanged
# configuration rebuilds nothing. Usage: check_build_config.sh BUILD_DIR
set -eu
build_dir=$1
mkdir -p "$build_dir"
work=$(mktemp -d "$build_dir/config-check.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

build() {
    "${MAKE:-make}" --no-print-directory "$@" BUILD_DIR="$work" > /dev/null
}

# Start with a library built with thread safety, then switch back and forth
build lib FEATURES=-DSS_ENABLE_THREAD_SAFETY=1
for enabled in 0 1 0; do
    build "$work/build_config_probe" FEATURES="-DSS_ENABLE_THREAD_SAFETY=$enabled"
    actual=$("$work/build_config_probe")
    [ "$actual" = "caller=$enabled library=$enabled" ] || {
        echo "check-build-config: stale library: $actual" >&2
        exit 1
    }
done

# The same flags again must not rebuild anything
cp -p "$work/ss_lib.o" "$work/object.before"
cp -p "$work/build_config_probe" "$work/probe.before"
build "$work/build_config_probe" FEATURES=-DSS_ENABLE_THREAD_SAFETY=0
if [ "$work/ss_lib.o" -nt "$work/object.before" ] ||
   [ "$work/build_config_probe" -nt "$work/probe.before" ]; then
    echo "check-build-config: an unchanged configuration rebuilt an output" >&2
    exit 1
fi
# A dry run with other flags must not delete or rewrite anything
cp -p "$work/.build-config" "$work/stamp.before"
"${MAKE:-make}" --no-print-directory -n "$work/build_config_probe" lib \
    FEATURES=-DSS_ENABLE_THREAD_SAFETY=1 BUILD_DIR="$work" > /dev/null
for f in ss_lib.o libss_lib.a build_config_probe; do
    [ -f "$work/$f" ] || { echo "check-build-config: make -n deleted $f" >&2; exit 1; }
done
cmp -s "$work/.build-config" "$work/stamp.before" ||
    { echo "check-build-config: make -n rewrote .build-config" >&2; exit 1; }

echo "Changed flags rebuild the library and its callers"
