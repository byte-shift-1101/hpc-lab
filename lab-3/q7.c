#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)
#define RANGE 100

void InitializeArray(int *arr, size_t arrSize) {
    for (size_t i = 0; i < arrSize; i++) {
        arr[i] = rand() % RANGE;
    }
}

void PrintArray(int *arr, size_t size, int rank) {
    fprintf(stdout, "Node %d: [ ", rank);
    for (size_t i = 0; i < size; i++) {
        fprintf(stdout, "%d ", arr[i]);
    }
    fprintf(stdout, "]\n");
    fflush(stdout);
}

int main(int argC, char **argV) {
    srand((unsigned int)time(NULL));
    MPI_Init(&argC, &argV);

    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    size_t elementsPerNode = 2;
    if (argC >= 2) elementsPerNode = (size_t)atoi(argV[1]);

    size_t arrSize = (size_t)world * elementsPerNode;
    int *arr = NULL, *myElements = (int *)malloc(elementsPerNode * sizeof(int));

    if (IsManager(rank)) {
        arr = (int *)malloc(arrSize * sizeof(int));
        InitializeArray(arr, arrSize);
        printf("[Main Array] ");
        PrintArray(arr, arrSize, MANAGER);
    }

    MPI_Scatter(arr, (int)elementsPerNode, MPI_INT, myElements, (int)elementsPerNode, MPI_INT, MANAGER, MPI_COMM_WORLD);

    PrintArray(myElements, elementsPerNode, rank);

    free(myElements);
    free(arr);
    MPI_Finalize();
    return 0;
}