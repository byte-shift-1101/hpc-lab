#!/bin/bash
#SBATCH --job-name=q1_bench
#SBATCH --partition=full
#SBATCH --nodes=10
#SBATCH --ntasks=40
#SBATCH --ntasks-per-node=4
#SBATCH --time=00:30:00

#SBATCH --output=/mnt/gv0/home/kanwarveer/lab-4/runs/q1/bench_%j.log
#SBATCH --error=/mnt/gv0/home/kanwarveer/lab-4/runs/q1/bench_%j.err

BINARY="/mnt/gv0/home/kanwarveer/lab-4/bin/q1"
RUN_DIR="/mnt/gv0/home/kanwarveer/lab-4/runs/q1"
mkdir -p "$RUN_DIR"

# --- Sweep message size ---
echo "=== Sweeping message size ==="
> "$RUN_DIR/length.txt"
for MSG in 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288; do
    srun --mpi=pmix --nodes=10 --ntasks=10 --ntasks-per-node=1 "$BINARY" "$MSG" >> "$RUN_DIR/length.txt"
done

# --- Sweep node count ---
echo "=== Sweeping node count ==="
> "$RUN_DIR/nodes.txt"
for N in 2 3 4 5 6 7 8 9 10; do
    NTASKS=$((N * 1))
    srun --mpi=pmix --nodes="$N" --ntasks="$NTASKS" --ntasks-per-node=1 "$BINARY" 16384 >> "$RUN_DIR/nodes.txt"
done

# --- Sweep processes per node ---
echo "=== Sweeping processes per node ==="
> "$RUN_DIR/processes.txt"
for PPN in 1 2 3 4; do
    NTASKS=$((10 * PPN))
    srun --mpi=pmix --nodes=10 --ntasks="$NTASKS" --ntasks-per-node="$PPN" "$BINARY" 16384 >> "$RUN_DIR/processes.txt"
done

echo "Done."
