#!/bin/bash

# Benchmark runner for SS_Lib
# Compares performance across different configurations

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/benchmarks"

mkdir -p "$BUILD_DIR"

echo "SS_Lib Benchmark Suite"
echo "====================="
echo ""

# Function to run a benchmark configuration
run_benchmark() {
    local name="$1"
    local flags="$2"
    local output="$BUILD_DIR/benchmark_$name"
    
    echo "Building benchmark: $name"
    gcc -O3 -march=native $flags \
        -I"$PROJECT_DIR/include" \
        "$SCRIPT_DIR/benchmark_ss_lib.c" \
        "$PROJECT_DIR/src/ss_lib.c" \
        -pthread -lm \
        -o "$output"
    
    echo "Running benchmark: $name"
    echo "----------------------------------------"
    "$output"
    echo ""
}

# Run different configurations
run_benchmark "dynamic_mt" "-DSS_ENABLE_THREAD_SAFETY=1"
run_benchmark "dynamic_st" "-DSS_ENABLE_THREAD_SAFETY=0"
run_benchmark "static_mt" "-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=256 -DSS_MAX_SLOTS=1024 -DSS_ENABLE_THREAD_SAFETY=1"
run_benchmark "static_st" "-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=256 -DSS_MAX_SLOTS=1024 -DSS_ENABLE_THREAD_SAFETY=0"
run_benchmark "minimal" "-DSS_MINIMAL_BUILD -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=32 -DSS_MAX_SLOTS=128"

# If ISR-safe is available
if grep -q "SS_ENABLE_ISR_SAFE" "$PROJECT_DIR/include/ss_config.h"; then
    run_benchmark "isr_safe" "-DSS_ENABLE_ISR_SAFE=1 -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=64 -DSS_MAX_SLOTS=256"
fi

# Generate comparison report
echo "Generating comparison report..."
"$SCRIPT_DIR/compare_results.py" "$BUILD_DIR" > "$BUILD_DIR/comparison_report.txt" 2>/dev/null || {
    echo "Note: Python comparison script not found or failed. Raw results are in $BUILD_DIR"
}

echo "Benchmark complete! Results saved to: $BUILD_DIR"