#!/usr/bin/env python3

import argparse
import json
import math
import re
import sys
from bisect import bisect_right
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


TIMESTAMP_PATTERN = re.compile(r"^\[([^]]+)]")
METRIC_PATTERN = re.compile(r"([A-Za-z0-9_]+)=(-?[0-9]+(?:\.[0-9]+)?)")
ROOM_TICK_PATTERN = re.compile(r"\[RoomTickResponseTimes] worker=(\d+) samples_us=([0-9,]+)")

WORKLOAD_METRICS = (
    "rrs_net_connections_current",
    "rrs_static_entities_current",
    "rrs_dynamic_entities_current",
    "rrs_process_cpu_percent",
    "rrs_net_bytes_out_per_sec",
    "rrs_net_send_calls_per_sec",
)

DEFAULT_THRESHOLDS_US = (1_000, 5_000, 10_000, 15_000, 20_000, 25_000, 30_000, 33_333, 40_000)
COMPARISON_LATENCY_METRICS = (
    "mean_us",
    "p50_us",
    "p95_us",
    "p99_us",
    "p99_9_us",
    "max_us",
    "deadline_miss_percent",
)


@dataclass
class MetricsWindow:
    timestamp: str
    metrics: dict[str, float]
    samples_by_worker: dict[int, list[int]] = field(default_factory=dict)


def parse_number(value: str) -> float:
    return float(value)


def parse_log(path: Path) -> list[MetricsWindow]:
    windows: list[MetricsWindow] = []
    current_window: MetricsWindow | None = None

    with path.open("r", encoding="utf-8", errors="replace") as log_file:
        for line in log_file:
            if "[Metrics]" in line:
                if current_window is not None:
                    windows.append(current_window)

                timestamp_match = TIMESTAMP_PATTERN.search(line)
                timestamp = timestamp_match.group(1) if timestamp_match else ""
                metrics_text = line.split("[Metrics]", maxsplit=1)[1]
                metrics = {
                    name: parse_number(value)
                    for name, value in METRIC_PATTERN.findall(metrics_text)
                }
                current_window = MetricsWindow(timestamp=timestamp, metrics=metrics)
                continue

            if current_window is None or "[RoomTickResponseTimes]" not in line:
                continue

            samples_match = ROOM_TICK_PATTERN.search(line)
            if samples_match is None:
                continue

            worker_id = int(samples_match.group(1))
            samples = [int(value) for value in samples_match.group(2).split(",")]
            current_window.samples_by_worker.setdefault(worker_id, []).extend(samples)

    if current_window is not None:
        windows.append(current_window)

    return windows


def matches_filters(window: MetricsWindow, args: argparse.Namespace) -> bool:
    connections = window.metrics.get("rrs_net_connections_current")
    static_entities = window.metrics.get("rrs_static_entities_current")
    dynamic_entities = window.metrics.get("rrs_dynamic_entities_current")

    if args.connections is not None and connections != args.connections:
        return False
    if args.min_static_entities is not None and (
        static_entities is None or static_entities < args.min_static_entities
    ):
        return False
    if args.max_static_entities is not None and (
        static_entities is None or static_entities > args.max_static_entities
    ):
        return False
    if args.min_dynamic_entities is not None and (
        dynamic_entities is None or dynamic_entities < args.min_dynamic_entities
    ):
        return False
    if args.max_dynamic_entities is not None and (
        dynamic_entities is None or dynamic_entities > args.max_dynamic_entities
    ):
        return False
    return True


def select_windows(windows: list[MetricsWindow], args: argparse.Namespace) -> list[MetricsWindow]:
    selected = [window for window in windows if matches_filters(window, args)]
    if args.skip_windows > 0:
        selected = selected[args.skip_windows:]
    if args.last_windows is not None:
        selected = selected[-args.last_windows:]
    return selected


def nearest_rank(ordered_samples: list[int], percentile: float) -> int:
    rank = max(1, math.ceil(percentile * len(ordered_samples)))
    return ordered_samples[rank - 1]


def calculate_latency_stats(
    samples: list[int],
    deadline_us: int,
    thresholds_us: tuple[int, ...],
) -> dict[str, Any]:
    ordered_samples = sorted(samples)
    deadline_miss_count = len(ordered_samples) - bisect_right(ordered_samples, deadline_us)
    return {
        "sample_count": len(samples),
        "mean_us": round(sum(samples) / len(samples), 2),
        "p50_us": nearest_rank(ordered_samples, 0.50),
        "p95_us": nearest_rank(ordered_samples, 0.95),
        "p99_us": nearest_rank(ordered_samples, 0.99),
        "p99_9_us": nearest_rank(ordered_samples, 0.999),
        "max_us": ordered_samples[-1],
        "deadline_miss_count": deadline_miss_count,
        "deadline_miss_percent": round(deadline_miss_count / len(samples) * 100.0, 6),
        "threshold_exceedance": [
            {
                "threshold_us": threshold_us,
                "count": len(ordered_samples) - bisect_right(ordered_samples, threshold_us),
                "percent": round(
                    (len(ordered_samples) - bisect_right(ordered_samples, threshold_us))
                    / len(ordered_samples)
                    * 100.0,
                    6,
                ),
            }
            for threshold_us in thresholds_us
        ],
    }


def collect_window_samples(window: MetricsWindow) -> list[int]:
    return [
        sample
        for worker_id in sorted(window.samples_by_worker)
        for sample in window.samples_by_worker[worker_id]
    ]


def summarize_window_latency(
    windows: list[MetricsWindow],
    deadline_us: int,
) -> dict[str, Any]:
    details = []
    for window in windows:
        samples = collect_window_samples(window)
        if not samples:
            continue

        ordered_samples = sorted(samples)
        deadline_miss_count = len(ordered_samples) - bisect_right(ordered_samples, deadline_us)
        details.append({
            "timestamp": window.timestamp,
            "sample_count": len(ordered_samples),
            "p99_us": nearest_rank(ordered_samples, 0.99),
            "max_us": ordered_samples[-1],
            "deadline_miss_count": deadline_miss_count,
            "deadline_miss_percent": round(deadline_miss_count / len(ordered_samples) * 100.0, 6),
        })

    ordered_p99_values = sorted(detail["p99_us"] for detail in details)
    worst_p99_window = max(details, key=lambda detail: detail["p99_us"])
    worst_deadline_miss_window = max(
        details,
        key=lambda detail: (detail["deadline_miss_percent"], detail["max_us"]),
    )
    deadline_miss_windows = sum(detail["deadline_miss_count"] > 0 for detail in details)
    p99_over_deadline_windows = sum(detail["p99_us"] > deadline_us for detail in details)
    return {
        "sampled_window_count": len(details),
        "p99_us": {
            "median": nearest_rank(ordered_p99_values, 0.50),
            "p95": nearest_rank(ordered_p99_values, 0.95),
            "max": worst_p99_window["p99_us"],
            "worst_timestamp": worst_p99_window["timestamp"],
        },
        "deadline_miss_windows": {
            "count": deadline_miss_windows,
            "percent": round(deadline_miss_windows / len(details) * 100.0, 6),
        },
        "p99_over_deadline_windows": {
            "count": p99_over_deadline_windows,
            "percent": round(p99_over_deadline_windows / len(details) * 100.0, 6),
        },
        "worst_deadline_miss_window": worst_deadline_miss_window,
        "windows": details,
    }


def summarize_workload(windows: list[MetricsWindow]) -> dict[str, dict[str, float]]:
    summary: dict[str, dict[str, float]] = {}
    for metric_name in WORKLOAD_METRICS:
        values = [window.metrics[metric_name] for window in windows if metric_name in window.metrics]
        if not values:
            continue
        summary[metric_name] = {
            "min": min(values),
            "max": max(values),
            "mean": round(sum(values) / len(values), 2),
        }
    return summary


def describe_selection(args: argparse.Namespace) -> dict[str, int | None]:
    return {
        "connections": args.connections,
        "min_static_entities": args.min_static_entities,
        "max_static_entities": args.max_static_entities,
        "min_dynamic_entities": args.min_dynamic_entities,
        "max_dynamic_entities": args.max_dynamic_entities,
        "skip_windows": args.skip_windows,
        "last_windows": args.last_windows,
    }


def analyze_file(path: Path, args: argparse.Namespace) -> dict[str, Any]:
    windows = parse_log(path)
    selected_windows = select_windows(windows, args)
    thresholds_us = tuple(sorted(set((*DEFAULT_THRESHOLDS_US, args.deadline_us))))

    samples_by_worker: dict[int, list[int]] = {}
    for window in selected_windows:
        for worker_id, samples in window.samples_by_worker.items():
            samples_by_worker.setdefault(worker_id, []).extend(samples)

    aggregate_samples = [
        sample
        for worker_id in sorted(samples_by_worker)
        for sample in samples_by_worker[worker_id]
    ]
    if not aggregate_samples:
        raise ValueError("no room tick samples matched the selected workload windows")

    return {
        "path": str(path),
        "selection": describe_selection(args),
        "windows": {
            "parsed": len(windows),
            "selected": len(selected_windows),
            "first_selected_timestamp": selected_windows[0].timestamp,
            "last_selected_timestamp": selected_windows[-1].timestamp,
        },
        "workload": summarize_workload(selected_windows),
        "latency_us": {
            "aggregate": calculate_latency_stats(aggregate_samples, args.deadline_us, thresholds_us),
            "workers": {
                str(worker_id): calculate_latency_stats(samples, args.deadline_us, thresholds_us)
                for worker_id, samples in sorted(samples_by_worker.items())
            },
        },
        "window_latency_us": summarize_window_latency(selected_windows, args.deadline_us),
    }


def compare_value(baseline: float, candidate: float) -> dict[str, float | None]:
    return {
        "baseline": baseline,
        "candidate": candidate,
        "absolute_delta": round(candidate - baseline, 6),
        "relative_delta_percent": (
            round((candidate - baseline) / baseline * 100.0, 6)
            if baseline != 0
            else None
        ),
    }


def build_comparison(baseline: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    baseline_latency = baseline["latency_us"]["aggregate"]
    candidate_latency = candidate["latency_us"]["aggregate"]
    workload_comparison = {}
    for metric_name in WORKLOAD_METRICS:
        if metric_name not in baseline["workload"] or metric_name not in candidate["workload"]:
            continue
        workload_comparison[metric_name] = compare_value(
            baseline["workload"][metric_name]["mean"],
            candidate["workload"][metric_name]["mean"],
        )

    return {
        "baseline_path": baseline["path"],
        "candidate_path": candidate["path"],
        "latency_us": {
            metric_name: compare_value(baseline_latency[metric_name], candidate_latency[metric_name])
            for metric_name in COMPARISON_LATENCY_METRICS
        },
        "workload_mean": workload_comparison,
    }


def print_text_report(report: dict[str, Any], deadline_us: int) -> None:
    print(f"File: {report['path']}")
    active_filters = [
        f"{name}={value}"
        for name, value in report["selection"].items()
        if value is not None and value != 0
    ]
    print(f"Selection: {', '.join(active_filters) if active_filters else 'all metrics windows'}")
    windows = report["windows"]
    print(
        "Windows: "
        f"selected={windows['selected']} parsed={windows['parsed']} "
        f"range={windows['first_selected_timestamp']} .. {windows['last_selected_timestamp']}"
    )

    workload = report["workload"]
    if workload:
        print("Workload:")
        for metric_name, values in workload.items():
            print(
                f"  {metric_name}: mean={values['mean']:.2f} "
                f"min={values['min']:.2f} max={values['max']:.2f}"
            )

    print(f"Latency (nearest-rank percentiles, deadline>{deadline_us}us):")
    print(
        "  scope        samples    mean_us  p50_us  p95_us  p99_us  "
        "p99.9_us  max_us  misses  miss_percent"
    )

    latency = report["latency_us"]
    rows = [("aggregate", latency["aggregate"])]
    rows.extend((f"worker={worker_id}", stats) for worker_id, stats in latency["workers"].items())
    for scope, stats in rows:
        print(
            f"  {scope:<12} {stats['sample_count']:>8} {stats['mean_us']:>10.2f} "
            f"{stats['p50_us']:>7} {stats['p95_us']:>7} {stats['p99_us']:>7} "
            f"{stats['p99_9_us']:>9} {stats['max_us']:>7} "
            f"{stats['deadline_miss_count']:>7} {stats['deadline_miss_percent']:>13.6f}%"
        )

    print("Threshold exceedance (aggregate):")
    print("  threshold_us      count       percent")
    for threshold in latency["aggregate"]["threshold_exceedance"]:
        print(
            f"  {threshold['threshold_us']:>12} {threshold['count']:>10} "
            f"{threshold['percent']:>12.6f}%"
        )

    window_latency = report["window_latency_us"]
    window_p99 = window_latency["p99_us"]
    deadline_miss_windows = window_latency["deadline_miss_windows"]
    p99_over_deadline_windows = window_latency["p99_over_deadline_windows"]
    worst_miss = window_latency["worst_deadline_miss_window"]
    print("Window stability:")
    print(
        f"  sampled={window_latency['sampled_window_count']} "
        f"window_p99_median_us={window_p99['median']} "
        f"window_p99_p95_us={window_p99['p95']} "
        f"window_p99_max_us={window_p99['max']} "
        f"worst_p99_at={window_p99['worst_timestamp']}"
    )
    print(
        f"  windows_with_miss={deadline_miss_windows['count']} "
        f"({deadline_miss_windows['percent']:.6f}%) "
        f"windows_p99_over_deadline={p99_over_deadline_windows['count']} "
        f"({p99_over_deadline_windows['percent']:.6f}%)"
    )
    print(
        f"  worst_miss_percent={worst_miss['deadline_miss_percent']:.6f}% "
        f"worst_miss_at={worst_miss['timestamp']} "
        f"worst_window_max_us={worst_miss['max_us']}"
    )


def print_comparison(comparison: dict[str, Any]) -> None:
    print(
        f"Comparison: {comparison['candidate_path']} relative to "
        f"{comparison['baseline_path']}"
    )
    print("  metric                              baseline       candidate        delta    relative")

    rows = list(comparison["latency_us"].items())
    cpu_comparison = comparison["workload_mean"].get("rrs_process_cpu_percent")
    if cpu_comparison is not None:
        rows.append(("rrs_process_cpu_percent_mean", cpu_comparison))

    for metric_name, values in rows:
        relative = values["relative_delta_percent"]
        relative_text = f"{relative:+.2f}%" if relative is not None else "n/a"
        print(
            f"  {metric_name:<34} {values['baseline']:>12.3f} "
            f"{values['candidate']:>15.3f} {values['absolute_delta']:>12.3f} "
            f"{relative_text:>11}"
        )


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Analyze raw RoomTickResponseTimes samples from one or more rrs server logs."
    )
    parser.add_argument("logs", nargs="+", type=Path, help="server log files to analyze")
    parser.add_argument("--connections", type=int, help="select windows with this exact connection count")
    parser.add_argument("--min-static-entities", type=int, help="minimum static entity count")
    parser.add_argument("--max-static-entities", type=int, help="maximum static entity count")
    parser.add_argument("--min-dynamic-entities", type=int, help="minimum dynamic entity count")
    parser.add_argument("--max-dynamic-entities", type=int, help="maximum dynamic entity count")
    parser.add_argument(
        "--skip-windows",
        type=int,
        default=0,
        help="skip this many windows after workload filtering",
    )
    parser.add_argument(
        "--last-windows",
        type=int,
        help="keep only the last N windows after filtering and skipping",
    )
    parser.add_argument(
        "--deadline-us",
        type=int,
        default=33_333,
        help="deadline used for miss statistics (default: 33333)",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="output format for humans or automated analysis (default: text)",
    )
    return parser


def validate_arguments(args: argparse.Namespace, parser: argparse.ArgumentParser) -> None:
    if args.skip_windows < 0:
        parser.error("--skip-windows must not be negative")
    if args.last_windows is not None and args.last_windows <= 0:
        parser.error("--last-windows must be positive")
    if args.deadline_us <= 0:
        parser.error("--deadline-us must be positive")
    if (
        args.min_static_entities is not None
        and args.max_static_entities is not None
        and args.min_static_entities > args.max_static_entities
    ):
        parser.error("--min-static-entities must not exceed --max-static-entities")
    if (
        args.min_dynamic_entities is not None
        and args.max_dynamic_entities is not None
        and args.min_dynamic_entities > args.max_dynamic_entities
    ):
        parser.error("--min-dynamic-entities must not exceed --max-dynamic-entities")


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()
    validate_arguments(args, parser)

    reports = []
    for path in args.logs:
        try:
            reports.append(analyze_file(path, args))
        except (OSError, ValueError) as error:
            print(f"error: {path}: {error}", file=sys.stderr)
            return 1

    comparisons = [
        build_comparison(reports[0], candidate)
        for candidate in reports[1:]
    ]

    if args.format == "json":
        json.dump(
            {
                "schema_version": 2,
                "percentile_method": "nearest_rank",
                "deadline_us": args.deadline_us,
                "reports": reports,
                "comparisons": comparisons,
            },
            sys.stdout,
            ensure_ascii=False,
            indent=2,
        )
        print()
        return 0

    for report_index, report in enumerate(reports):
        if report_index > 0:
            print()
        print_text_report(report, args.deadline_us)

    for comparison in comparisons:
        print()
        print_comparison(comparison)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
