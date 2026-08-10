#!/usr/bin/env python3
"""Сравнивает два прогона Google Benchmark и находит деградацию.

    python3 tools/bench-compare.py baseline.json current.json [--threshold 10]

Возвращает 1, если cpu_time хотя бы одного бенчмарка вырос больше порога
либо если бенчмарк из базы пропал из текущего прогона.

Прогон с --benchmark_repetitions даёт несколько строк на бенчмарк; берётся
медиана — она не съезжает от одного выброса. Сравнивать имеет смысл только
прогоны на одной машине: абсолютные числа между машинами несопоставимы.
"""
import argparse
import json
import statistics
import sys


def load(path):
    """Возвращает отображение «имя бенчмарка → cpu_time».

    Google Benchmark выдаёт либо строки отдельных повторов
    (run_type "iteration"), либо только агрегаты (run_type "aggregate",
    имена с суффиксом _mean, _median, _stddev, _cv), если задан
    --benchmark_report_aggregates_only. Учитываются оба вида: медиана,
    посчитанная самим Google Benchmark, имеет приоритет над медианой,
    посчитанной здесь.
    """
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)

    entries = data.get("benchmarks")
    if entries is None:
        raise SystemExit(f'{path}: нет ключа "benchmarks" — это не отчёт Google Benchmark')

    samples = {}
    medians = {}
    for entry in entries:
        name = entry["name"]
        if entry.get("run_type", "iteration") == "aggregate":
            aggregate = entry.get("aggregate_name", "")
            if aggregate != "median":
                continue
            suffix = "_" + aggregate
            base = name[: -len(suffix)] if name.endswith(suffix) else name
            medians[base] = float(entry["cpu_time"])
            continue
        samples.setdefault(name, []).append(float(entry["cpu_time"]))

    result = {name: statistics.median(values) for name, values in samples.items()}
    result.update(medians)
    return result


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
        if base[name] == 0.0:
            # Нулевая база непригодна для сравнения: делить не на что.
            print(f"{name:<24}{base[name]:>12.1f}{current[name]:>12.1f}{'база 0':>10}")
            continue
        change = (current[name] - base[name]) / base[name] * 100.0
        print(f"{name:<24}{base[name]:>12.1f}{current[name]:>12.1f}{change:>9.1f}%")
        if change > args.threshold:
            regressed.append((name, change))

    missing = sorted(set(base) - set(current))
    for name in missing:
        print(f"{name:<24}{base[name]:>12.1f}{'—':>12}{'исчез':>10}")

    if regressed:
        print("\nДеградация:")
        for name, change in regressed:
            print(f"  {name}: +{change:.1f}%")

    if missing:
        print("\nПропали из прогона:")
        for name in missing:
            print(f"  {name}")

    return 1 if regressed or missing else 0


if __name__ == "__main__":
    sys.exit(main())
