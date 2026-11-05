#!/bin/sh
# Check an installed SS_Lib (from `make install` or `cmake --install`) the way
# users meet it: build a program with the flags from the installed ss_lib.pc,
# and, when cmake is available, a CMake project using find_package(ss_lib),
# then run them against the installed library. On macOS they must start
# without library-path variables; on Linux a non-system prefix needs
# LD_LIBRARY_PATH, as for any library.
# The expected versions come from the installed package itself (the
# Version field of ss_lib.pc), so the check holds for any release, and it
# fails when the installed files disagree about that version.
# Usage: scripts/check_install.sh PREFIX [CC]
set -eu

prefix=$1
cc=${2:-cc}
pc="$prefix/lib/pkgconfig/ss_lib.pc"
[ -f "$pc" ] || { echo "no $pc" >&2; exit 1; }

# pkg-config: the fields it would print, without needing pkg-config
field() {
    sed -n "s/^$1: //p" "$pc" | sed -e "s|\${includedir}|$prefix/include|" \
        -e "s|\${libdir}|$prefix/lib|"
}
version=$(field Version)
major=${version%%.*}
case "$version" in
    *[!0-9.]* | "" | .* | *.) echo "$pc: bad Version '$version'" >&2; exit 1 ;;
esac

# A shared library, when one is installed, is named after the major version;
# a static-only install (CMake without SS_BUILD_SHARED) has the archive
case "$(uname -s)" in
    Darwin) shared=libss_lib.dylib soname="libss_lib.$major.dylib" ;;
    *) shared=libss_lib.so soname="libss_lib.so.$major" ;;
esac
if [ -e "$prefix/lib/$shared" ]; then
    [ -e "$prefix/lib/$soname" ] || { echo "$prefix/lib/$soname is missing" >&2; exit 1; }
else
    [ -e "$prefix/lib/libss_lib.a" ] || { echo "no SS_Lib library in $prefix/lib" >&2; exit 1; }
fi

run() {
    case "$(uname -s)" in
        Darwin)
            # The program must find the library through its install name alone
            env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH "$1" ;;
        *)
            LD_LIBRARY_PATH="$prefix/lib" "$1" ;;
    esac
}

cat > "$prefix/check.c" <<'C'
#include <ss_lib.h>
#include <stdio.h>
#include <string.h>
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
    if (strcmp(ss_version(), SS_VERSION_STRING) != 0) return 1;
    return calls == 2 ? 0 : 1;
}
C

cflags=$(field Cflags)
libs=$(field Libs)
# They hold several flags each and must be word-split
# shellcheck disable=SC2086
"$cc" "$prefix/check.c" $cflags $libs -o "$prefix/check"
out=$(run "$prefix/check")
echo "$out"
[ "$out" = "installed SS_Lib $version works" ] || \
    { echo "the installed library is not version $version" >&2; exit 1; }

# CMake: find_package(ss_lib) must follow semver and give a working target
if ! command -v cmake > /dev/null 2>&1; then
    echo "cmake not found: skipping the find_package(ss_lib) check"
    exit 0
fi
proj=$(mktemp -d "$prefix/cmake-check.XXXXXX")
cp "$prefix/check.c" "$proj/check.c"
cat > "$proj/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.10)
project(ss_lib_install_check C)
# Semver against the installed version M.m.p: any M.x request up to the
# installed version is accepted; other majors and newer versions are not
string(REGEX MATCH "^[0-9]+" major "${SS_LIB_VERSION}")
math(EXPR previous "${major} - 1")
math(EXPR next "${major} + 1")
set(rejected ${previous} ${next} ${major}.999)
set(accepted ${major} ${major}.0 ${SS_LIB_VERSION})
if(NOT CMAKE_VERSION VERSION_LESS 3.19)
    list(APPEND rejected ${previous}...<${major} ${next}...<${next}.999)
    list(APPEND accepted ${major}...<${next} ${major}.0...${major}.999)
endif()
foreach(request ${rejected} ${accepted})
    unset(ss_lib_DIR CACHE)
    find_package(ss_lib ${request} QUIET NO_DEFAULT_PATH PATHS "${SS_LIB_PREFIX}")
    list(FIND accepted "${request}" expected)
    if(ss_lib_FOUND AND expected EQUAL -1)
        message(FATAL_ERROR "SS_Lib ${ss_lib_VERSION} accepted a request for ${request}")
    elseif(NOT ss_lib_FOUND AND NOT expected EQUAL -1)
        message(FATAL_ERROR "SS_Lib was rejected for a request for ${request}")
    endif()
endforeach()
unset(ss_lib_DIR CACHE)
find_package(ss_lib ${SS_LIB_VERSION} EXACT REQUIRED NO_DEFAULT_PATH PATHS "${SS_LIB_PREFIX}")
add_executable(check check.c)
target_link_libraries(check ss_lib::ss_lib)
CMAKE
cmake -S "$proj" -B "$proj/build" -DSS_LIB_PREFIX="$prefix" -DSS_LIB_VERSION="$version" \
    -DCMAKE_C_COMPILER="$cc" > /dev/null
cmake --build "$proj/build" > /dev/null
out=$(run "$proj/build/check")
echo "$out"
[ "$out" = "installed SS_Lib $version works" ] || \
    { echo "find_package(ss_lib) gave a library that is not version $version" >&2; exit 1; }
