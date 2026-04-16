#!/usr/bin/env python3
"""Export combined two-tree broadcast timings to a single CSV file.

The script is intentionally shared across both data layouts:
- cdac/actual-results
- pi/actual_results

It reads actual results, SimGrid results, and LogGOP results, aligns them by
configuration and message size, and writes a single CSV with the timing values
in seconds.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path
from typing import Dict, List, Tuple


DataPoints = List[Tuple[int, float]]
ConfigKey = Tuple[int, int]
CombinedRow = Tuple[int, int, int, float, float, float]


def parse_actual_results(actual_dir: Path) -> Dict[ConfigKey, DataPoints]:
    result: Dict[ConfigKey, DataPoints] = {}

    for csv_path in sorted(actual_dir.glob("two_tree_results_*n_*ppn.csv")):
        match = re.search(r"two_tree_results_(\d+)n_(\d+)ppn\.csv$", csv_path.name)
        if not match:
            continue

        nodes = int(match.group(1))
        ppn = int(match.group(2))
        points: DataPoints = []

        with csv_path.open("r", newline="", encoding="utf-8") as file_handle:
            reader = csv.DictReader(file_handle)
            for row in reader:
                message_size = int(row["message_size"])
                # The source column is named time_us, but the project data is
                # already treated as seconds in the plotting workflow.
                time_seconds = float(row["time_us"])
                points.append((message_size, time_seconds))

        result[(nodes, ppn)] = sorted(points, key=lambda item: item[0])

    return result


def parse_simgrid_results(path: Path) -> Dict[ConfigKey, DataPoints]:
    result: Dict[ConfigKey, DataPoints] = {}

    with path.open("r", encoding="utf-8") as file_handle:
        for line in file_handle:
            line = line.strip()
            if not line:
                continue

            parts = line.split()
            if len(parts) != 3:
                continue

            hostfile, message_size_text, time_text = parts
            match = re.search(r"hostfiles/(\d+)_(\d+)\.txt$", hostfile)
            if not match:
                continue

            nodes = int(match.group(1))
            ppn = int(match.group(2))
            message_size = int(message_size_text)
            time_seconds = float(time_text)
            result.setdefault((nodes, ppn), []).append((message_size, time_seconds))

    for config in result:
        result[config].sort(key=lambda item: item[0])

    return result


def parse_loggop_results(path: Path) -> Dict[ConfigKey, DataPoints]:
    result: Dict[ConfigKey, DataPoints] = {}

    with path.open("r", newline="", encoding="utf-8") as file_handle:
        reader = csv.DictReader(file_handle)
        for row in reader:
            label = row["file"].strip()
            match = re.search(r"(\d+)n_(\d+)ppn_msg(\d+)$", label)
            if not match:
                continue

            nodes = int(match.group(1))
            ppn = int(match.group(2))
            message_size = int(match.group(3))
            time_seconds = float(row["max_host_time"]) / 1e9
            result.setdefault((nodes, ppn), []).append((message_size, time_seconds))

    for config in result:
        result[config].sort(key=lambda item: item[0])

    return result


def find_actual_dir(data_dir: Path) -> Path:
    candidates = [data_dir / "actual-results", data_dir / "actual_results"]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        f"Could not find actual results directory in {data_dir}. Expected actual-results or actual_results."
    )


def build_combined_rows(
    actual_data: Dict[ConfigKey, DataPoints],
    simgrid_data: Dict[ConfigKey, DataPoints],
    loggop_data: Dict[ConfigKey, DataPoints],
) -> List[CombinedRow]:
    rows: List[CombinedRow] = []
    configs = sorted(set(actual_data) & set(simgrid_data) & set(loggop_data))

    for config in configs:
        actual_map = {message_size: time_seconds for message_size, time_seconds in actual_data[config]}
        simgrid_map = {message_size: time_seconds for message_size, time_seconds in simgrid_data[config]}
        loggop_map = {message_size: time_seconds for message_size, time_seconds in loggop_data[config]}

        message_sizes = sorted(set(actual_map) & set(simgrid_map) & set(loggop_map))
        nodes, ppn = config

        for message_size in message_sizes:
            rows.append(
                (
                    nodes,
                    ppn,
                    message_size,
                    actual_map[message_size],
                    simgrid_map[message_size],
                    loggop_map[message_size],
                )
            )

    return rows


def write_combined_csv(rows: List[CombinedRow], output_path: Path) -> None:
    with output_path.open("w", newline="", encoding="utf-8") as file_handle:
        writer = csv.writer(file_handle)
        writer.writerow(
            [
                "nodes",
                "processes_per_node",
                "message_size",
                "actual_time_seconds",
                "simgrid_time_seconds",
                "loggop_time_seconds",
            ]
        )
        for row in rows:
            writer.writerow(row)


def main() -> None:
    parser = argparse.ArgumentParser(description="Export combined timings from actual, SimGrid, and LogGOP data.")
    parser.add_argument(
        "--dir",
        type=Path,
        default=Path("cdac"),
        help="Path to a data directory such as cdac or pi.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output CSV path. Defaults to <dir>/combined_timings.csv.",
    )
    args = parser.parse_args()

    data_dir = args.dir
    actual_dir = find_actual_dir(data_dir)
    simgrid_path = data_dir / "simgrid-results.txt"
    loggop_path = data_dir / "loggop-results.csv"

    actual_data = parse_actual_results(actual_dir)
    simgrid_data = parse_simgrid_results(simgrid_path)
    loggop_data = parse_loggop_results(loggop_path)

    rows = build_combined_rows(actual_data, simgrid_data, loggop_data)
    if not rows:
        raise RuntimeError("No overlapping rows found across the three data sources.")

    output_path = args.output or (data_dir / "combined_timings.csv")
    write_combined_csv(rows, output_path)

    print(f"Wrote {len(rows)} rows to {output_path}")


if __name__ == "__main__":
    main()