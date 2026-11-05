#!/bin/sh
# The version is defined once, in include/ss_lib.h (SS_VERSION_MAJOR/MINOR/
# PATCH and SS_VERSION_STRING). The Makefile, CMake, Doxygen (via the docs
# target) and pkg-config files are derived from it. This check fails when a
# copy that must be written by hand disagrees with it.
# Usage: scripts/check_version.sh [expected-tag]   e.g. v2.2.0 in a release;
# a pre-release tag (v2.3.0-rc.1) must match the version it leads up to
set -e
cd "$(dirname "$0")/.."

h=include/ss_lib.h
version=$(scripts/version.sh)
major=$(sed -n 's/^#define SS_VERSION_MAJOR \([0-9]*\)$/\1/p' "$h")
minor=$(sed -n 's/^#define SS_VERSION_MINOR \([0-9]*\)$/\1/p' "$h")
patch=$(sed -n 's/^#define SS_VERSION_PATCH \([0-9]*\)$/\1/p' "$h")
status=0

check() {  # check <description> <found>
    if [ "$2" != "$version" ]; then
        echo "version mismatch: $1 has '$2', include/ss_lib.h has '$version'" >&2
        status=1
    fi
}

[ -n "$version" ] || { echo "SS_VERSION_STRING not found in $h" >&2; exit 1; }
check "SS_VERSION_MAJOR/MINOR/PATCH" "$major.$minor.$patch"
check "@version in $h" "$(sed -n 's/^ \* @version \(.*\)$/\1/p' "$h")"
check "include/ss_lib_single.h" \
      "$(sed -n 's/^ \* SS_Lib \([0-9.]*\) - single-header build.*/\1/p' include/ss_lib_single.h)"
# Copies written by hand in the docs, updated at each release
check "docs/api-reference.md (SS_VERSION_MAJOR/MINOR/PATCH)" \
      "$(sed -n 's/^#define SS_VERSION_MAJOR \([0-9]*\)$/\1/p' docs/api-reference.md).$(
         sed -n 's/^#define SS_VERSION_MINOR \([0-9]*\)$/\1/p' docs/api-reference.md).$(
         sed -n 's/^#define SS_VERSION_PATCH \([0-9]*\)$/\1/p' docs/api-reference.md)"
check "docs/api-reference.md (SS_VERSION_STRING)" \
      "$(sed -n 's/^#define SS_VERSION_STRING "\(.*\)"$/\1/p' docs/api-reference.md)"
check "docs/api-reference.md (introduction)" \
      "$(sed -n 's/.*options of version \([0-9][0-9.]*[0-9]\)\..*/\1/p' docs/api-reference.md)"
check "docs/README.md" "$(sed -n 's/.*It describes version \([0-9][0-9.]*[0-9]\);.*/\1/p' docs/README.md)"
check "docs/architecture.md" \
      "$(sed -n 's/.*data structures of SS_Lib \([0-9][0-9.]*[0-9]\)\..*/\1/p' docs/architecture.md)"
check "CLAUDE.md" "$(sed -n 's/.*signal-slot library for C (v\([0-9.]*\)).*/\1/p' CLAUDE.md)"
check ".claude/skills/ss-lib/SKILL.md" \
      "$(sed -n 's/.*signal-slot library for C (v\([0-9.]*\)).*/\1/p' .claude/skills/ss-lib/SKILL.md)"
grep -q "^## \[$version\]" CHANGELOG.md || {
    echo "version mismatch: CHANGELOG.md has no '## [$version]' section" >&2
    status=1
}
if [ -n "$1" ]; then
    case "$1" in
        v[0-9]*.[0-9]*.[0-9]*) ;;
        *) echo "release tag '$1' is not vMAJOR.MINOR.PATCH[-PRERELEASE]" >&2; status=1 ;;
    esac
    tag=${1#v}
    check "release tag" "${tag%%-*}"
fi

[ $status -eq 0 ] && echo "Version $version is consistent"
exit $status
