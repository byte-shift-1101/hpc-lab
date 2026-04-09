#!/usr/bin/env python3
"""
Generate one SLURM file per question (q1..q5) that sweeps message size,
node count, and processes-per-node. Output is appended directly to
length.txt, nodes.txt, and processes.txt in runs/<question>/.

Usage:
  python generate.py q1
  python generate.py q5 --length 3 19 --nodes 2 10 --processes 1 4
"""

import argparse
import os
import stat

# ---- Config ----
PARTITION = "full"
TIME_LIMIT = "00:30:00"
MOUNT_PREFIX = "/mnt/gv0"
HPC_BASE_PATH = "/home/kanwarveer/lab-5"

DEFAULT_MSG_SIZE = 16384
DEFAULT_NODES = 10
DEFAULT_PPN = 1

MAX_NODES = 10
MAX_PPN = 4

DEFAULT_LENGTH_RANGE = (3, 19)
DEFAULT_NODES_RANGE = (2, 10)
DEFAULT_PROCS_RANGE = (1, 4)
VALID_QUESTIONS = {"q1", "q2", "q3", "q4", "q5"}


def write_slurm(path, content):
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.write(content)
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)


def generate(question, length_vals, node_vals, proc_vals):
    hpc = f"{MOUNT_PREFIX}{HPC_BASE_PATH}"
    binary = f"{hpc}/bin/{question}"
    run_dir = f"{hpc}/runs/{question}"

    lengths = " ".join(str(v) for v in length_vals)
    nodes = " ".join(str(v) for v in node_vals)
    procs = " ".join(str(v) for v in proc_vals)

    return f"""\
#!/bin/bash
#SBATCH --job-name={question}_bench
#SBATCH --partition={PARTITION}
#SBATCH --nodes={MAX_NODES}
#SBATCH --ntasks={MAX_NODES * MAX_PPN}
#SBATCH --ntasks-per-node={MAX_PPN}
#SBATCH --time={TIME_LIMIT}
#SBATCH --output={run_dir}/bench_%j.log
#SBATCH --error={run_dir}/bench_%j.err

BINARY=\"{binary}\"
RUN_DIR=\"{run_dir}\"
mkdir -p \"$RUN_DIR\"

# --- Sweep message size ---
echo \"=== Sweeping message size ===\"
> \"$RUN_DIR/length.txt\"
for MSG in {lengths}; do
    srun --mpi=pmix --nodes={DEFAULT_NODES} --ntasks={DEFAULT_NODES * DEFAULT_PPN} --ntasks-per-node={DEFAULT_PPN} \"$BINARY\" \"$MSG\" >> \"$RUN_DIR/length.txt\"
done

# --- Sweep node count ---
echo \"=== Sweeping node count ===\"
> \"$RUN_DIR/nodes.txt\"
for N in {nodes}; do
    NTASKS=$((N * {DEFAULT_PPN}))
    srun --mpi=pmix --nodes=\"$N\" --ntasks=\"$NTASKS\" --ntasks-per-node={DEFAULT_PPN} \"$BINARY\" {DEFAULT_MSG_SIZE} >> \"$RUN_DIR/nodes.txt\"
done

# --- Sweep processes per node ---
echo \"=== Sweeping processes per node ===\"
> \"$RUN_DIR/processes.txt\"
for PPN in {procs}; do
    NTASKS=$(({DEFAULT_NODES} * PPN))
    srun --mpi=pmix --nodes={DEFAULT_NODES} --ntasks=\"$NTASKS\" --ntasks-per-node=\"$PPN\" \"$BINARY\" {DEFAULT_MSG_SIZE} >> \"$RUN_DIR/processes.txt\"
done

echo \"Done.\"
"""


def main():
    default_output = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "runs"
    )

    parser = argparse.ArgumentParser(description="Generate SLURM benchmark file for lab-5")
    parser.add_argument("question", help="q1, q2, q3, q4, q5")
    parser.add_argument("--output-dir", default=default_output)
    parser.add_argument("--length", nargs=2, type=int, metavar=("LOW_EXP", "HIGH_EXP"))
    parser.add_argument("--nodes", nargs=2, type=int, metavar=("LOW", "HIGH"))
    parser.add_argument("--processes", nargs=2, type=int, metavar=("LOW", "HIGH"))
    args = parser.parse_args()

    question = args.question.lower()
    if question not in VALID_QUESTIONS:
        raise SystemExit("question must be one of: q1, q2, q3, q4, q5")

    low, high = args.length if args.length else DEFAULT_LENGTH_RANGE
    length_vals = [2 ** e for e in range(low, high + 1)]

    low, high = args.nodes if args.nodes else DEFAULT_NODES_RANGE
    node_vals = list(range(low, high + 1))

    low, high = args.processes if args.processes else DEFAULT_PROCS_RANGE
    proc_vals = list(range(low, high + 1))

    out_dir = os.path.join(args.output_dir, question)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "bench.slurm")
    write_slurm(path, generate(question, length_vals, node_vals, proc_vals))

    print(f"Generated {path}")


if __name__ == "__main__":
    main()
