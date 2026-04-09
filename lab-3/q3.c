#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)

void PrintArray(int *arr, size_t size) {
    fprintf(stdout, "Array: [ ");
    for (size_t i = 0; i < size; i++) {
        fprintf(stdout, "%d ", arr[i]);
    }
    fprintf(stdout, "]\n");
    fflush(stdout);
}

int main(int argC, char **argV) {
    MPI_Init(&argC, &argV);

    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    int generatedInt = rank, receiveCount = world;
    int *gatheredInts = NULL;

    if (IsManager(rank)) gatheredInts = (int *)malloc((size_t)receiveCount * sizeof(int));

    MPI_Gather(&generatedInt, 1, MPI_INT, gatheredInts, 1, MPI_INT, MANAGER, MPI_COMM_WORLD);

    if (IsManager(rank)) PrintArray(gatheredInts, (size_t)receiveCount);

    free(gatheredInts);
    MPI_Finalize();
    return 0;
}