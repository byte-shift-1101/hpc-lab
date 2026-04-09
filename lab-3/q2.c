#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)

void InitializeArray(int *arr, size_t arrSize) {
    for (size_t i = 0; i < arrSize; i++) {
        arr[i] = ((int)i) + 1;
    }
}

void PrintArray(int *arr, size_t arrSize, int rank) {
    fprintf(stdout, "Node %d: ", rank);
    for (size_t i = 0; i < arrSize; i++)
        fprintf(stdout, "%d ", arr[i]);

    fprintf(stdout, "\n");
    fflush(stdout);
}

int main(int argC, char **argV) {
    MPI_Init(&argC, &argV);

    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    // Using arguments
    size_t arrSize = 10;
    if (argC >= 2) arrSize = (size_t)atoi(argV[1]);

    // Allocating array
    int *arr = (int *)malloc(arrSize * sizeof(int));

    // Start Time
    MPI_Barrier(MPI_COMM_WORLD);
    double startTime = MPI_Wtime();
    
    if (IsManager(rank)) {
        // Initializing array
        InitializeArray(arr, arrSize);

        // Broadcasting array
        for (int destRank = 0; destRank < world; destRank++) {
            if (!IsManager(destRank)) MPI_Send(arr, (int)arrSize, MPI_INT, destRank, 0, MPI_COMM_WORLD);
        }
    } else {
        MPI_Recv(arr, (int)arrSize, MPI_INT, MANAGER, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    // End Time
    double endTime = MPI_Wtime();
    
    // Print the array
    if (!IsManager(rank)) PrintArray(arr, arrSize, rank);
    MPI_Barrier(MPI_COMM_WORLD);
    if (IsManager(rank)) printf("Time taken: %lf seconds\n", endTime - startTime);
    
    free(arr);
    MPI_Finalize();
    return 0;
}