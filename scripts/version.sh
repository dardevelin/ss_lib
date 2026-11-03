#!/bin/sh
# Print the library version. The only source is SS_VERSION_STRING in
# include/ss_lib.h; the Makefile, CMake and the scripts all read it here.
sed -n 's/^#define SS_VERSION_STRING "\(.*\)"$/\1/p' "$(dirname "$0")/../include/ss_lib.h"
