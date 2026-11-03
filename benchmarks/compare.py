#!/usr/bin/env python3
"""Compare two SS_Lib benchmark outputs and print a Markdown table.

Usage: python3 benchmarks/compare.py BASE_RESULTS CURRENT_RESULTS

Standard library only; supported on the Python versions tested in CI.
"""

import re
import sys

RESULT_LINE = re.compile(r"^(.+?):\s*avg=\s*(\d+)\s*ns")
THRESHOLD_PERCENT = 5.0


def parse_results(path):
    """Map benchmark name -> average ns from a benchmark_ss_lib output file."""
    results = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = RESULT_LINE.match(line)
            if match:
                results[match.group(1).strip()] = int(match.group(2))
    return results


def status(change):
    if change > THRESHOLD_PERCENT:
        return "regression"
    if change < -THRESHOLD_PERCENT:
        return "improvement"
    return "no change"


def format_table(base, current):
    lines = [
        "### Performance Changes",
        "",
        "| Benchmark | Base (ns) | Current (ns) | Change | Status |",
        "|-----------|-----------|--------------|--------|--------|",
    ]
    for name in sorted(set(base) | set(current)):
        if name in base and name in current:
            b, c = base[name], current[name]
            change = ((c - b) / b * 100.0) if b else 0.0
            lines.append(f"| {name} | {b} | {c} | {change:+.1f}% | {status(change)} |")
        elif name in base:
            lines.append(f"| {name} | {base[name]} | N/A | - | removed |")
        else:
            lines.append(f"| {name} | N/A | {current[name]} | - | added |")
    return "\n".join(lines)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    print(format_table(parse_results(argv[1]), parse_results(argv[2])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
