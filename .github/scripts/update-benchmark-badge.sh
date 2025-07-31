#!/bin/bash
# Script to update benchmark badge with performance metrics

set -e

# Parse benchmark results and calculate average emission time
RESULT=$(grep "Emit void signal (no slots)" benchmark_results.txt | grep -oE "avg=\s*[0-9]+" | grep -oE "[0-9]+")

if [ -z "$RESULT" ]; then
    echo "Failed to parse benchmark results"
    exit 1
fi

# Determine badge color based on performance
if [ "$RESULT" -lt 50 ]; then
    COLOR="brightgreen"
    STATUS="excellent"
elif [ "$RESULT" -lt 100 ]; then
    COLOR="green"
    STATUS="good"
elif [ "$RESULT" -lt 200 ]; then
    COLOR="yellow"
    STATUS="fair"
else
    COLOR="red"
    STATUS="slow"
fi

# Create badge JSON
cat > badge.json << EOF
{
  "schemaVersion": 1,
  "label": "performance",
  "message": "${RESULT}ns",
  "color": "${COLOR}"
}
EOF

echo "Benchmark result: ${RESULT}ns (${STATUS})"