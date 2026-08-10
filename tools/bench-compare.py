#!/usr/bin/env python3
"""Сравнивает два прогона Google Benchmark и находит деградацию.

    python3 tools/bench-compare.py baseline.json current.json [--threshold 10]

Возвращает 1, если cpu_time хотя бы одного бенчмарка вырос больше порога.
Сравнивать имеет смысл только прогоны на одной машине: абсолютные числа
между машинами несопоставимы.
"""
import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    return {
        entry["name"]: float(entry["cpu_time"])
        for entry in data["benchmarks"]
        if entry.get("run_type", "iteration") == "iteration"
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline")
    parser.add_argument("current")
    parser.add_argument(
        "--threshold",
        type=float,
        default=10.0,
        help="допустимый рост cpu_time в процентах",
    )
    args = parser.parse_args()

    base = load(args.baseline)
    current = load(args.current)

    print(f"{'benchmark':<24}{'base':>12}{'current':>12}{'change':>10}")
    regressed = []
    for name in sorted(current):
        if name not in base:
            print(f"{name:<24}{'—':>12}{current[name]:>12.1f}{'новый':>10}")
            continue
        change = (current[name] - base[name]) / base[name] * 100.0
        print(f"{name:<24}{base[name]:>12.1f}{current[name]:>12.1f}{change:>9.1f}%")
        if change > args.threshold:
            regressed.append((name, change))

    for name in sorted(set(base) - set(current)):
        print(f"{name:<24}{base[name]:>12.1f}{'—':>12}{'исчез':>10}")

    if regressed:
        print("\nДеградация:")
        for name, change in regressed:
            print(f"  {name}: +{change:.1f}%")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
