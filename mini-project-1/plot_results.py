#!/usr/bin/env python3
"""Plot two-tree broadcast timings from actual, SimGrid, and LogGOP data.

Creates one plot per configuration showing:
- Actual Time (from actual-results CSV files)
- SimGrid Time (from simgrid-results.txt)
- LogGOP Time (from loggop-results.csv, converted ns -> s)
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter

DataPoints = List[Tuple[int, float]]
ConfigKey = Tuple[int, int]


def parse_actual_results(actual_dir: Path) -> Dict[ConfigKey, DataPoints]:
    result: Dict[ConfigKey, DataPoints] = {}

    for csv_path in sorted(actual_dir.glob("two_tree_results_*n_*ppn.csv")):
        match = re.search(r"two_tree_results_(\d+)n_(\d+)ppn\.csv$", csv_path.name)
        if not match:
            continue
        nodes = int(match.group(1))
        ppn = int(match.group(2))

        points: DataPoints = []
        with csv_path.open("r", newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                msg_size = int(row["message_size"])
                # User requirement specifies this is already in seconds.
                time_seconds = float(row["time_us"])
                points.append((msg_size, time_seconds))

        result[(nodes, ppn)] = sorted(points, key=lambda x: x[0])

    return result


def parse_simgrid_results(path: Path) -> Dict[ConfigKey, DataPoints]:
    result: Dict[ConfigKey, DataPoints] = {}

    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            parts = line.split()
            if len(parts) != 3:
                continue

            hostfile, msg_size_s, time_s = parts
            host_match = re.search(r"hostfiles/(\d+)_(\d+)\.txt$", hostfile)
            if not host_match:
                continue

            nodes = int(host_match.group(1))
            ppn = int(host_match.group(2))
            msg_size = int(msg_size_s)
            time_seconds = float(time_s)

            result.setdefault((nodes, ppn), []).append((msg_size, time_seconds))

    for config in result:
        result[config].sort(key=lambda x: x[0])

    return result


def parse_loggop_results(path: Path) -> Dict[ConfigKey, DataPoints]:
    result: Dict[ConfigKey, DataPoints] = {}

    with path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            label = row["file"].strip()
            time_ns = float(row["max_host_time"])

            # Flexible match to tolerate minor label typos/prefixes.
            match = re.search(r"(\d+)n_(\d+)ppn_msg(\d+)$", label)
            if not match:
                continue

            nodes = int(match.group(1))
            ppn = int(match.group(2))
            msg_size = int(match.group(3))
            time_seconds = time_ns / 1e9
            result.setdefault((nodes, ppn), []).append((msg_size, time_seconds))

    for config in result:
        result[config].sort(key=lambda x: x[0])

    return result


def plot_for_config(
    nodes: int,
    ppn: int,
    actual: DataPoints,
    simgrid: DataPoints,
    loggop: DataPoints,
    output_dir: Path,
) -> None:
    plt.style.use("seaborn-v0_8-whitegrid")
    fig, ax = plt.subplots(figsize=(9.5, 5.8), dpi=140)

    colors = {
        "actual": "#2a9d8f",
        "simgrid": "#457b9d",
        "loggop": "#e76f51",
    }

    ax.plot(
        [x for x, _ in actual],
        [y for _, y in actual],
        marker="o",
        linewidth=2.2,
        markersize=5,
        color=colors["actual"],
        label="Actual Time",
    )
    ax.plot(
        [x for x, _ in simgrid],
        [y for _, y in simgrid],
        marker="s",
        linewidth=2.0,
        markersize=4.8,
        color=colors["simgrid"],
        label="SimGrid Time",
    )
    ax.plot(
        [x for x, _ in loggop],
        [y for _, y in loggop],
        marker="^",
        linewidth=2.0,
        markersize=5,
        color=colors["loggop"],
        label="LogGOP Time",
    )

    ax.set_xscale("log", base=2)
    ax.set_yscale("log")

    ax.set_title(f"Two-Tree Broadcast Timing Comparison ({nodes}n_{ppn}ppn)", fontsize=12.5, pad=10)
    ax.set_xlabel("Message Size (bytes)", fontsize=10.5)
    ax.set_ylabel("Time (seconds)", fontsize=10.5)
    ax.legend(frameon=True)

    # Put x ticks exactly at measured message sizes.
    tick_sizes = sorted({x for x, _ in actual} | {x for x, _ in simgrid} | {x for x, _ in loggop})
    ax.set_xticks(tick_sizes)
    ax.get_xaxis().set_major_formatter(ScalarFormatter())
    ax.ticklabel_format(style="plain", axis="x")

    fig.tight_layout()
    out_path = output_dir / f"timing_comparison_{nodes}n_{ppn}ppn.png"
    fig.savefig(out_path)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot CDAC actual vs SimGrid vs LogGOP timings.")
    parser.add_argument(
        "--dir",
        type=Path,
        default=Path("cdac"),
        help="Path to data directory (contains actual-results or actual_results, simgrid-results.txt, loggop-results.csv)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Directory to write generated plot images (defaults to <dir>/plots)",
    )
    args = parser.parse_args()

    cdac_dir = args.dir
    actual_dir = cdac_dir / "actual-results"
    if not actual_dir.exists():
        actual_dir = cdac_dir / "actual_results"
    simgrid_path = cdac_dir / "simgrid-results.txt"
    loggop_path = cdac_dir / "loggop-results.csv"

    actual_data = parse_actual_results(actual_dir)
    simgrid_data = parse_simgrid_results(simgrid_path)
    loggop_data = parse_loggop_results(loggop_path)

    output_dir = args.output_dir or (cdac_dir / "plots")
    output_dir.mkdir(parents=True, exist_ok=True)

    configs = sorted(set(actual_data) & set(simgrid_data) & set(loggop_data))
    if not configs:
        raise RuntimeError("No overlapping configurations found across all three data sources.")

    for nodes, ppn in configs:
        plot_for_config(
            nodes=nodes,
            ppn=ppn,
            actual=actual_data[(nodes, ppn)],
            simgrid=simgrid_data[(nodes, ppn)],
            loggop=loggop_data[(nodes, ppn)],
            output_dir=output_dir,
        )

    print(f"Generated {len(configs)} plots in {output_dir}")


if __name__ == "__main__":
    main()
