#!/usr/bin/env python3
"""
Benchmark visualization tool for SS_Lib
Generates charts and comparison reports from benchmark results
"""

import re
import sys
import os
import json
from collections import defaultdict

try:
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("Warning: matplotlib not installed. Install with: pip install matplotlib")

def parse_benchmark_output(filename):
    """Parse benchmark output file and extract results"""
    results = defaultdict(dict)
    current_config = "default"
    
    with open(filename, 'r') as f:
        for line in f:
            # Check for configuration headers
            config_match = re.match(r'Configuration:\s*(.+)', line)
            if config_match:
                current_config = config_match.group(1).strip()
                continue
            
            # Parse benchmark results
            result_match = re.match(r'^(.+?):\s*avg=\s*(\d+)\s*ns,\s*min=\s*(\d+)\s*ns,\s*max=\s*(\d+)\s*ns', line)
            if result_match:
                name = result_match.group(1).strip()
                avg = int(result_match.group(2))
                min_val = int(result_match.group(3))
                max_val = int(result_match.group(4))
                
                results[current_config][name] = {
                    'avg': avg,
                    'min': min_val,
                    'max': max_val
                }
    
    return results

def generate_comparison_table(results1, results2, label1="Base", label2="Current"):
    """Generate markdown comparison table"""
    print(f"\n## Performance Comparison: {label1} vs {label2}\n")
    print("| Benchmark | {} (avg ns) | {} (avg ns) | Change | Status |".format(label1, label2))
    print("|-----------|-------------|-------------|--------|--------|")
    
    all_benchmarks = set()
    for config in results1.values():
        all_benchmarks.update(config.keys())
    for config in results2.values():
        all_benchmarks.update(config.keys())
    
    for benchmark in sorted(all_benchmarks):
        # Get results from default config or first available
        r1 = None
        r2 = None
        
        for config in results1.values():
            if benchmark in config:
                r1 = config[benchmark]
                break
                
        for config in results2.values():
            if benchmark in config:
                r2 = config[benchmark]
                break
        
        if r1 and r2:
            avg1 = r1['avg']
            avg2 = r2['avg']
            change = ((avg2 - avg1) / avg1) * 100
            
            if change > 5:
                status = "🔴 Regression"
            elif change < -5:
                status = "🟢 Improvement"
            else:
                status = "⚪ No change"
            
            print(f"| {benchmark} | {avg1} | {avg2} | {change:+.1f}% | {status} |")
        elif r1:
            print(f"| {benchmark} | {r1['avg']} | N/A | - | ❌ Removed |")
        elif r2:
            print(f"| {benchmark} | N/A | {r2['avg']} | - | ✅ Added |")

def plot_benchmarks(results, output_dir="plots"):
    """Generate benchmark plots"""
    if not HAS_MATPLOTLIB:
        print("Skipping plots - matplotlib not installed")
        return
    
    os.makedirs(output_dir, exist_ok=True)
    
    # Plot 1: Emission benchmarks comparison
    emission_benchmarks = [
        "Emit void signal (no slots)",
        "Emit void signal (1 slots)",
        "Emit void signal (5 slots)",
        "Emit void signal (10 slots)"
    ]
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    configs = list(results.keys())
    x = np.arange(len(emission_benchmarks))
    width = 0.8 / len(configs)
    
    for i, config in enumerate(configs):
        values = []
        for bench in emission_benchmarks:
            if bench in results[config]:
                values.append(results[config][bench]['avg'])
            else:
                values.append(0)
        
        offset = (i - len(configs)/2 + 0.5) * width
        ax.bar(x + offset, values, width, label=config)
    
    ax.set_xlabel('Benchmark')
    ax.set_ylabel('Average Time (ns)')
    ax.set_title('Signal Emission Performance')
    ax.set_xticks(x)
    ax.set_xticklabels([b.replace("Emit void signal ", "") for b in emission_benchmarks])
    ax.legend()
    ax.grid(True, axis='y', alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'emission_performance.png'), dpi=150)
    plt.close()
    
    # Plot 2: Memory model comparison
    operations = ["Signal registration", "Slot connection", "Signal existence check"]
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    x = np.arange(len(operations))
    
    for i, config in enumerate(configs):
        values = []
        for op in operations:
            if op in results[config]:
                values.append(results[config][op]['avg'])
            else:
                values.append(0)
        
        offset = (i - len(configs)/2 + 0.5) * width
        ax.bar(x + offset, values, width, label=config)
    
    ax.set_xlabel('Operation')
    ax.set_ylabel('Average Time (ns)')
    ax.set_title('Basic Operations Performance')
    ax.set_xticks(x)
    ax.set_xticklabels(operations)
    ax.legend()
    ax.grid(True, axis='y', alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'operations_performance.png'), dpi=150)
    plt.close()
    
    print(f"Plots saved to {output_dir}/")

def generate_json_report(results, output_file="benchmark_results.json"):
    """Generate JSON report for further processing"""
    with open(output_file, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"JSON report saved to {output_file}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python visualize.py <benchmark_output.txt> [comparison_file.txt]")
        sys.exit(1)
    
    # Parse primary results
    results1 = parse_benchmark_output(sys.argv[1])
    
    # If comparison file provided
    if len(sys.argv) >= 3:
        results2 = parse_benchmark_output(sys.argv[2])
        generate_comparison_table(results1, results2, 
                                label1=os.path.basename(sys.argv[1]), 
                                label2=os.path.basename(sys.argv[2]))
    
    # Generate plots
    plot_benchmarks(results1)
    
    # Generate JSON report
    generate_json_report(results1)

if __name__ == "__main__":
    main()