#!/bin/bash

# Script to create a single header file from ss_lib_v2
# This combines the header and implementation into one file for easy integration

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_FILE="$SCRIPT_DIR/ss_lib_single.h"
HEADER_FILE="$SCRIPT_DIR/include/ss_lib_v2.h"
CONFIG_FILE="$SCRIPT_DIR/include/ss_config.h"
SOURCE_FILE="$SCRIPT_DIR/src/ss_lib_v2.c"

# Check if required files exist
if [ ! -f "$HEADER_FILE" ]; then
    echo "Error: Header file not found: $HEADER_FILE"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Config file not found: $CONFIG_FILE"
    exit 1
fi

if [ ! -f "$SOURCE_FILE" ]; then
    echo "Error: Source file not found: $SOURCE_FILE"
    exit 1
fi

echo "Creating single header file: $OUTPUT_FILE"

# Start with header guard
cat > "$OUTPUT_FILE" << 'EOF'
/*
 * SS_Lib - Single Header Signal-Slot Library for C
 * 
 * This is an automatically generated single-header version of SS_Lib v2.
 * 
 * Usage:
 *   #define SS_IMPLEMENTATION
 *   #include "ss_lib_single.h"
 * 
 * Configuration:
 *   Define configuration macros before including this file.
 *   See ss_config.h section for available options.
 */

#ifndef SS_LIB_SINGLE_H
#define SS_LIB_SINGLE_H

EOF

# Add config file content (without guards)
echo "/* ===== Configuration Section ===== */" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
# Extract content between header guards
sed -n '/^#ifndef SS_CONFIG_H/,/^#endif.*SS_CONFIG_H/{
/^#ifndef SS_CONFIG_H/d
/^#endif.*SS_CONFIG_H/d
p
}' "$CONFIG_FILE" >> "$OUTPUT_FILE"

echo "" >> "$OUTPUT_FILE"
echo "/* ===== Header Section ===== */" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Add header file content (without guards and without config include)
sed -n '/^#ifndef SS_LIB_V2_H/,/^#endif.*SS_LIB_V2_H/{
/^#ifndef SS_LIB_V2_H/d
/^#endif.*SS_LIB_V2_H/d
p
}' "$HEADER_FILE" | grep -v '#include "ss_config.h"' >> "$OUTPUT_FILE"

# Add implementation section
cat >> "$OUTPUT_FILE" << 'EOF'

/* ===== Implementation Section ===== */

#ifdef SS_IMPLEMENTATION

EOF

# Add source file content (without includes of project headers)
# First, let's see what includes we need to preserve
echo "/* Standard library includes */" >> "$OUTPUT_FILE"
grep '^#include <' "$SOURCE_FILE" >> "$OUTPUT_FILE" || true

echo "" >> "$OUTPUT_FILE"

# Add the actual implementation without includes
sed '/^#include/d' "$SOURCE_FILE" >> "$OUTPUT_FILE"

# Close implementation guard
echo "" >> "$OUTPUT_FILE"
echo "#endif /* SS_IMPLEMENTATION */" >> "$OUTPUT_FILE"

# Close header guard
echo "" >> "$OUTPUT_FILE"
echo "#endif /* SS_LIB_SINGLE_H */" >> "$OUTPUT_FILE"

# Make the output prettier by removing multiple blank lines
sed -i '' -e '/^$/N;/^\n$/d' "$OUTPUT_FILE"

echo "Single header file created successfully!"
echo ""
echo "Usage example:"
echo "  #define SS_IMPLEMENTATION"
echo "  #include \"ss_lib_single.h\""
echo ""
echo "To configure features, define macros before including:"
echo "  #define SS_USE_STATIC_MEMORY 1"
echo "  #define SS_MAX_SIGNALS 32"
echo "  #define SS_IMPLEMENTATION"
echo "  #include \"ss_lib_single.h\""