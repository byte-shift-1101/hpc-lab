#!/bin/bash
#SBATCH --job-name=matrix_logs
#SBATCH --partition=full
#SBATCH --nodes=10
#SBATCH --ntasks=10
#SBATCH --ntasks-per-node=1
#SBATCH --time=00:10:00
#SBATCH --output=/mnt/gv0/home/kanwarveer/matrix/lab/q1/runs/%j/matrix_logs.out
#SBATCH --error=/mnt/gv0/home/kanwarveer/matrix/lab/q1/runs/%j/matrix_logs.err

echo "Nodes allocated: $SLURM_NODELIST"
echo "Total tasks: $SLURM_NTASKS"

srun --mpi=pmix /mnt/gv0/home/kanwarveer/matrix/lab/q1/2023csb1126_lab2_q1 10000 10000 0
