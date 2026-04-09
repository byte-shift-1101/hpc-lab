#!/bin/bash
#SBATCH --job-name=two_tree_bench
#SBATCH --partition=full
#SBATCH --nodes=10
#SBATCH --ntasks=40
#SBATCH --ntasks-per-node=4
#SBATCH --time=00:30:00
#SBATCH --output=/mnt/gv0/home/ruhaan/mini_project/logs/bench_%j.log
#SBATCH --error=/mnt/gv0/home/ruhaan/mini_project/logs/bench_%j.err

BINARY="/mnt/gv0/home/ruhaan/mini_project/bin/two-tree"
RUN_DIR="/mnt/gv0/home/ruhaan/mini_project/results"
mkdir -p "$RUN_DIR"

# --- Sweep message size for each processes per node ---
echo "=== Sweeping message size for each processes per node ==="
> "$RUN_DIR/two_tree_results.csv"
echo "message_size,nodes,num_processes,time" >> "$RUN_DIR/two_tree_results.csv"

for PPN in 1 2 4; do
    for NODES in 6 8 10; do
        NTASKS=$((NODES * PPN))
        # echo "Running with $PPN processes per node (Total: $NTASKS processes)"
        for MSG in 1024 4096 8192 16384 65536 262144 1048576 4194304; do
            RESULT=$(srun --mpi=pmix --nodes=$NODES --ntasks="$NTASKS" --ntasks-per-node="$PPN" "$BINARY" "$MSG" | head -1)
            echo "$MSG,$NODES,$NTASKS,$RESULT" >> "$RUN_DIR/two_tree_results.csv"
        done
    done
done

echo "Done."
