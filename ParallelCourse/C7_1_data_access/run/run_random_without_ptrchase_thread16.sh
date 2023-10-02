#!/bin/bash
#SBATCH --partition=a800
#SBATCH --job-name=random16
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --nodelist=g07
#SBATCH --exclusive

source ./env.sh

./build.sh

export LATENCY_OUTPUT_FILENAME_PREFIX="cycle_random_without_ptrchase_thread16"
export OMP_NUM_THREADS=16

numactl -N 0 -m 0 ./bin/mem_random_without_ptrchase_omp
