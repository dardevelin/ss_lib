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
    make -s -C "$PROJECT_DIR" "build/benchmarks/$name/benchmark_ss_lib" \
        BUILD_DIR="build/benchmarks/$name" FEATURES="$flags"
    cp "$PROJECT_DIR/build/benchmarks/$name/benchmark_ss_lib" "$output"

    echo "Running benchmark: $name"
    echo "----------------------------------------"
    { echo "Configuration: $name"; "$output"; } | tee "$output.txt"
    cat "$output.txt" >> "$BUILD_DIR/all_results.txt"
    echo ""
}

rm -f "$BUILD_DIR/all_results.txt"

# Run different configurations
run_benchmark "dynamic_mt" "-DSS_ENABLE_THREAD_SAFETY=1"
run_benchmark "dynamic_st" "-DSS_ENABLE_THREAD_SAFETY=0"
run_benchmark "static_mt" "-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=256 -DSS_MAX_SLOTS=1024 -DSS_ENABLE_THREAD_SAFETY=1"
run_benchmark "static_st" "-DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=256 -DSS_MAX_SLOTS=1024 -DSS_ENABLE_THREAD_SAFETY=0"
run_benchmark "minimal" "-DSS_MINIMAL_BUILD -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=32 -DSS_MAX_SLOTS=128"

run_benchmark "isr_safe" "-DSS_ENABLE_ISR_SAFE=1 -DSS_USE_STATIC_MEMORY=1 -DSS_MAX_SIGNALS=64 -DSS_MAX_SLOTS=256"

# JSON report (and plots when matplotlib is installed) across configurations
echo "Generating report..."
(cd "$BUILD_DIR" && python3 "$SCRIPT_DIR/visualize.py" all_results.txt) || \
    echo "Note: report generation failed; raw results are in $BUILD_DIR"

echo "Benchmark complete! Results saved to: $BUILD_DIR"
