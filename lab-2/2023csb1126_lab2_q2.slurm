#!/bin/bash
#SBATCH --job-name=matrix_logs
#SBATCH --partition=full
#SBATCH --nodes=10
#SBATCH --ntasks=10
#SBATCH --ntasks-per-node=1
#SBATCH --time=00:10:00
#SBATCH --output=/mnt/gv0/home/kanwarveer/matrix/lab/q2/runs/%j/matrix_logs.out
#SBATCH --error=/mnt/gv0/home/kanwarveer/matrix/lab/q2/runs/%j/matrix_logs.err

echo "Nodes allocated: $SLURM_NODELIST"
echo "Total tasks: $SLURM_NTASKS"

srun --mpi=pmix /mnt/gv0/home/kanwarveer/matrix/lab/q2/2023csb1126_lab2_q2 100 100 0 50
