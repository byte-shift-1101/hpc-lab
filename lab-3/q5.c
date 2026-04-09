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

long long CalculateSum(int *arr, size_t arrSize) {
    long long sum = 0;
    for (size_t i = 0; i < arrSize; i++) {
        sum += (long long)arr[i];
    }
    return sum;
}

int main(int argC, char **argV) {
    MPI_Init(&argC, &argV);
    
    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    srand((unsigned int)time(NULL) + (unsigned int)rank);

    size_t arrSize = 10;
    if (argC >= 2) arrSize = (size_t)atoi(argV[1]);

    int *arr = (int *)malloc(arrSize * sizeof(int));
    InitializeArray(arr, arrSize);

    long long sum = CalculateSum(arr, arrSize);
    printf("Sum at node %d: %lld\n", rank, sum);

    long long overallSum = 0;
    MPI_Reduce(&sum, &overallSum, 1, MPI_LONG_LONG, MPI_SUM, MANAGER, MPI_COMM_WORLD);

    if (IsManager(rank)) {
        printf("Overall sum: %lld\n", overallSum);
    }

    free(arr);
    MPI_Finalize();
    return 0;
}