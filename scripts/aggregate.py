#!/usr/bin/env python3
"""
Aggregation script for CG initialization study results.
Produces medians with min-max and Dolan-Moré performance profiles.
"""

import json
import os
import sys
import glob
import numpy as np
from collections import defaultdict
from typing import Dict, List, Tuple, Any


def load_results(results_dir: str = "results") -> List[Dict[str, Any]]:
    """Load all result JSON files."""
    results = []
    for filepath in glob.glob(os.path.join(results_dir, "*.json")):
        with open(filepath, 'r') as f:
            data = json.load(f)
            data["_filepath"] = filepath
            results.append(data)
    return results


def extract_metrics(results: List[Dict[str, Any]]) -> Dict[str, List[float]]:
    """Extract key metrics grouped by instance class and method."""
    metrics = defaultdict(list)

    for r in results:
        instance = r.get("instance", "")
        method = r.get("method", "")
        endpoint = r.get("endpoint", {})
        metrics_obj = r.get("metrics", {})

        status = endpoint.get("status", "unknown")
        if status not in ["feasible", "certified_infeasible"]:
            continue

        key = f"{instance}|{method}"
        metrics[key].append({
            "iterations": endpoint.get("iterations_to_endpoint", 0),
            "master_time": metrics_obj.get("total_master_time_sec", 0),
            "total_time": metrics_obj.get("total_master_time_sec", 0) + metrics_obj.get("total_pricing_time_sec", 0),
            "simplex_iters": metrics_obj.get("total_simplex_iterations", 0),
            "gap": endpoint.get("gap", 0),
        })

    return metrics


def compute_statistics(metrics: Dict[str, List[float]]) -> Dict[str, Dict[str, float]]:
    """Compute median, min, max for each metric."""
    stats = {}

    for key, values in metrics.items():
        if not values:
            continue

        iterations = [v["iterations"] for v in values]
        master_time = [v["master_time"] for v in values]
        total_time = [v["total_time"] for v in values]
        simplex_iters = [v["simplex_iters"] for v in values]
        gap = [v["gap"] for v in values]

        stats[key] = {
            "iterations_median": np.median(iterations),
            "iterations_min": np.min(iterations),
            "iterations_max": np.max(iterations),
            "master_time_median": np.median(master_time),
            "master_time_min": np.min(master_time),
            "master_time_max": np.max(master_time),
            "total_time_median": np.median(total_time),
            "total_time_min": np.min(total_time),
            "total_time_max": np.max(total_time),
            "simplex_iters_median": np.median(simplex_iters),
            "simplex_iters_min": np.min(simplex_iters),
            "simplex_iters_max": np.max(simplex_iters),
            "gap_median": np.median(gap),
            "gap_min": np.min(gap),
            "gap_max": np.max(gap),
            "count": len(values),
        }

    return stats


def dolan_more_profile(results: List[Dict[str, Any]], metric: str = "total_time") -> Dict[str, List[Tuple[float, float]]]:
    """Compute Dolan-Moré performance profiles."""
    instance_methods = defaultdict(dict)

    for r in results:
        instance = r.get("instance", "")
        method = r.get("method", "")
        endpoint = r.get("endpoint", {})
        metrics = r.get("metrics", {})

        status = endpoint.get("status", "unknown")
        if status != "feasible":
            continue

        if metric == "total_time":
            value = metrics.get("total_master_time_sec", 0) + metrics.get("total_pricing_time_sec", 0)
        elif metric == "iterations":
            value = endpoint.get("iterations_to_endpoint", 0)
        elif metric == "simplex_iters":
            value = metrics.get("total_simplex_iterations", 0)
        else:
            value = 0

        if value > 0:
            instance_methods[instance][method] = value

    methods = set()
    for methods_dict in instance_methods.values():
        methods.update(methods_dict.keys())
    methods = sorted(methods)

    profiles = {method: [] for method in methods}

    for instance, method_values in instance_methods.items():
        if len(method_values) < len(methods):
            continue

        best = min(method_values.values())
        for method in methods:
            if method in method_values:
                ratio = method_values[method] / best
                profiles[method].append(ratio)

    profile_curves = {}
    for method, ratios in profiles.items():
        if not ratios:
            profile_curves[method] = [(1.0, 0.0)]
            continue

        ratios = sorted(ratios)
        n = len(ratios)
        curve = []
        for i, r in enumerate(ratios):
            curve.append((r, (i + 1) / n))
        profile_curves[method] = curve

    return profile_curves


def print_statistics(stats: Dict[str, Dict[str, float]]) -> None:
    """Print formatted statistics table."""
    print("\n" + "=" * 120)
    print(f"{'Instance | Method':<50} {'Iters':>10} {'Master Time (s)':>18} {'Total Time (s)':>18} {'Simplex':>12} {'Gap':>10}")
    print("-" * 120)

    for key in sorted(stats.keys()):
        s = stats[key]
        print(f"{key:<50} "
              f"{s['iterations_median']:>6.1f} [{s['iterations_min']:.0f}-{s['iterations_max']:.0f}] "
              f"{s['master_time_median']:>8.3f} [{s['master_time_min']:.3f}-{s['master_time_max']:.3f}] "
              f"{s['total_time_median']:>8.3f} [{s['total_time_min']:.3f}-{s['total_time_max']:.3f}] "
              f"{s['simplex_iters_median']:>8.1f} [{s['simplex_iters_min']:.0f}-{s['simplex_iters_max']:.0f}] "
              f"{s['gap_median']*100:>6.2f}% [{s['gap_min']*100:.2f}-{s['gap_max']*100:.2f}]")

    print("=" * 120)


def print_dolan_more(profiles: Dict[str, List[Tuple[float, float]]], title: str) -> None:
    """Print Dolan-Moré performance profile."""
    print(f"\n{title}")
    print("-" * 60)

    tau_values = [1.0, 1.5, 2.0, 3.0, 5.0, 10.0]
    print(f"{'Method':<15}", end="")
    for tau in tau_values:
        print(f"  tau={tau:<4}", end="")
    print()

    for method, curve in sorted(profiles.items()):
        print(f"{method:<15}", end="")
        for tau in tau_values:
            prob = 0.0
            for r, p in curve:
                if r <= tau:
                    prob = p
            print(f"  {prob*100:>6.1f}%", end="")
        print()


def main():
    results = load_results()
    if not results:
        print("No results found in results/")
        return

    print(f"Loaded {len(results)} result files")

    metrics = extract_metrics(results)
    stats = compute_statistics(metrics)
    print_statistics(stats)

    time_profiles = dolan_more_profile(results, "total_time")
    print_dolan_more(time_profiles, "Dolan-Moré Profile: Total Time to Feasibility")

    iter_profiles = dolan_more_profile(results, "iterations")
    print_dolan_more(iter_profiles, "Dolan-Moré Profile: Iterations to Feasibility")

    simplex_profiles = dolan_more_profile(results, "simplex_iters")
    print_dolan_more(simplex_profiles, "Dolan-Moré Profile: Simplex Iterations to Feasibility")

    output = {
        "statistics": stats,
        "profiles": {
            "time": {k: dict(v) for k, v in time_profiles.items()},
            "iterations": {k: dict(v) for k, v in iter_profiles.items()},
            "simplex": {k: dict(v) for k, v in simplex_profiles.items()},
        }
    }

    with open("results/aggregate.json", "w") as f:
        json.dump(output, f, indent=2)
    print("\nAggregated results written to results/aggregate.json")


if __name__ == "__main__":
    main()