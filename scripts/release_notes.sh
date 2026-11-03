#!/bin/sh
# Print the CHANGELOG.md section of a version (without its heading): the
# release notes the release workflow publishes. Fails when it is missing
# or empty, so a release cannot go out without notes.
# Usage: scripts/release_notes.sh 2.2.0   (a leading v is accepted)
set -e
cd "$(dirname "$0")/.."

version=${1#v}
version=${version%%-*}
[ -n "$version" ] || { echo "usage: $0 VERSION" >&2; exit 2; }

notes=$(awk -v v="$version" '
    index($0, "## [" v "]") == 1 { found = 1; next }
    found && /^## \[/ { exit }
    found && /^\[[^]]*\]: / { exit }
    found { print }
' CHANGELOG.md | sed -e '/./,$!d')

if [ -z "$(printf '%s' "$notes" | tr -d '[:space:]')" ]; then
    echo "CHANGELOG.md has no notes for $version" >&2
    exit 1
fi
printf '%s\n' "$notes"
