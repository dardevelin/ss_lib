#!/bin/sh
# Reuse one output directory while changing the caller/library ABI.
# Invoke through make check-build-config; SANITIZER=address proves the
# original stack overread as well as checking the corrected build graph.
set -eu
build_dir=$1
mkdir -p "$build_dir"
work=$(mktemp -d "$build_dir/config-check.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

build() {
    "${MAKE:-make}" --no-print-directory "$@" BUILD_DIR="$work"
}

# Start with only the larger-layout library, then create a smaller caller.
build lib FEATURES=-DSS_ENABLE_CUSTOM_DATA=1
for enabled in 0 1 0; do
    build "$work/build_config_probe" FEATURES="-DSS_ENABLE_CUSTOM_DATA=$enabled"
    actual=$("$work/build_config_probe")
    [ "$actual" = "custom_data=$enabled" ] || {
        echo "stale caller configuration: $actual (expected $enabled)" >&2
        exit 1
    }
done

# An identical configuration must remain incremental.
cp -p "$work/ss_lib.o" "$work/object.before"
cp -p "$work/build_config_probe" "$work/probe.before"
build "$work/build_config_probe" FEATURES=-DSS_ENABLE_CUSTOM_DATA=0
cmp "$work/ss_lib.o" "$work/object.before"
if [ "$work/ss_lib.o" -nt "$work/object.before" ] ||
   [ "$work/build_config_probe" -nt "$work/probe.before" ]; then
    echo "unchanged configuration rebuilt an artifact" >&2
    exit 1
fi
echo "Build configuration changes rebuild compatible libraries and callers"
