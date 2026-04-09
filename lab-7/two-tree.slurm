#!/bin/bash
#SBATCH --job-name=two_tree_benchmark
#SBATCH --nodes=16
#SBATCH --ntasks=16
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:10:00

PROJECT_ROOT="$(pwd)"

# Results directory
RESULTS_DIR="$PROJECT_ROOT/results"
mkdir -p "$RESULTS_DIR"

# Message sizes in bytes: 1KB, 4KB, 8KB, 16KB, 64KB, 256KB, 1MB, 4MB
MESSAGE_SIZES=(1024 4096 8192 16384 65536 262144 1048576 4194304)

# Node and process configurations to test
NODES_ARRAY=(6 8 10 12 14 16)
PROCS_PER_NODE_ARRAY=(1)

echo "========== Two-Tree Benchmark (Multi-Configuration) =========="
echo "Project root: $PROJECT_ROOT"
echo "Nodes allocated: $SLURM_NODELIST"
echo "Testing: 3 node counts × 3 process configurations = 9 total runs"
echo "Starting benchmark runs..."

# Loop through each node configuration
for num_nodes in "${NODES_ARRAY[@]}"; do
    
    # Loop through each process per node configuration
    for procs_per_node in "${PROCS_PER_NODE_ARRAY[@]}"; do
        
        total_procs=$((num_nodes * procs_per_node))
        
        # Generate CSV filename based on configuration
        OUTPUT_CSV="$RESULTS_DIR/two_tree_results_${num_nodes}n_${procs_per_node}ppn.csv"
        
        # Write CSV header
        echo "message_size,num_processes,time_us" > "$OUTPUT_CSV"
        
        echo ""
        echo "=========================================="
        echo "Configuration: $num_nodes nodes, $procs_per_node process(es) per node"
        echo "Total processes: $total_procs"
        echo "Output: $OUTPUT_CSV"
        echo "=========================================="
        
        # Loop through each message size
        for msg_size in "${MESSAGE_SIZES[@]}"; do
            echo "Testing message size: $msg_size bytes with $total_procs processes"
            
            # Run the benchmark and capture output
            output=$(srun --mpi=pmix --nodes=$num_nodes --ntasks=$total_procs --ntasks-per-node=$procs_per_node \
                     "$PROJECT_ROOT/two-tree-broadcast" $msg_size 2>&1)
            
            # Try to extract timing (more flexible patterns)
            result=$(echo "$output" | grep -oE "[0-9]+\.[0-9]+" | head -1)
            
            # Write to CSV
            if [ -n "$result" ]; then
                echo "$msg_size,$total_procs,$result" >> "$OUTPUT_CSV"
                echo "  Result: $result us"
            else
                echo "  Warning: No result obtained for message size $msg_size"
                echo "  Debug output: $output" >&2
            fi
        done
        
        echo "Completed: $OUTPUT_CSV"
    done
done

echo ""
echo "=========================================="
echo "Benchmark completed!"
echo "Results saved to: $RESULTS_DIR"
echo "Generated files:"
ls -lh "$RESULTS_DIR"/two_tree_results_*n_*ppn.csv
echo "=========================================="
echo "Benchmark completed!"
echo "Results saved to: $OUTPUT_CSV"
echo "=========================================="
