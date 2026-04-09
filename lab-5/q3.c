#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include "utils.h"

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)
#define NULL_NODE -1
#define IsNullNode(node) (node == NULL_NODE)

#define TRANSFER 1

#define minimum(a, b) (a < b) ? a : b

static int chunks = 8;

int BinomialTreeReduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);
    
    int bits = (int)ceil(log2(world));
    int lsb = (rank != root) ? (rank & -rank) : (1 << bits);
    int lsbbits = (int)log2(lsb);

    void *storage = malloc((size_t)count * sizeof(int));
    void *fakebuf = malloc((size_t)count * sizeof(int));
    memcpy(fakebuf, sendbuf, (size_t)count * sizeof(int));
    
    for (int i = 0; i < lsbbits; i++) {
        int source = rank | (1 << i);
        if (source < world) {
            MPI_Recv(storage, count, datatype, source, TRANSFER, comm, MPI_STATUS_IGNORE);
            PerformOp((int *)fakebuf, (int *)storage, count, op);
        }
    }

    if (rank != root) {
        int dest = rank & ~lsb;
        MPI_Send(fakebuf, count, datatype, dest, TRANSFER, comm);
    } else memcpy(recvbuf, fakebuf, (size_t)count * sizeof(int));

    free(storage);
    free(fakebuf);
    return MPI_SUCCESS;
}

void GetChildren(int rank, int world, int *parent, int *left, int *right) {
    *parent = (int)floor((double)(rank - 1) / 2);

    *left = ((rank + 1) << 1) - 1;
    if ((*left) + 1 > world) *left = NULL_NODE;
    
    *right = (rank + 1) << 1;
    if ((*right) + 1 > world) *right = NULL_NODE;
}

int PipelinedReduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    void *storage = malloc((size_t)count * sizeof(int));
    void *fakebuf = malloc((size_t)count * sizeof(int));
    memcpy(fakebuf, sendbuf, (size_t)count * sizeof(int));

    int parent, leftChild, rightChild;
    GetChildren(rank, world, &parent, &leftChild, &rightChild);

    int base = count / chunks, extra = count % chunks;
    int chunkOffset, chunkSize;
    for (int i = 0; i < chunks; i++) {
        chunkSize = base + ((i < extra) ? 1 : 0);
        chunkOffset = (i * base) + (minimum(i, extra));
        if (!IsNullNode(leftChild)) {
            MPI_Recv((int *)storage + (size_t)chunkOffset, chunkSize, datatype, leftChild, TRANSFER + i, comm, MPI_STATUS_IGNORE);
            PerformOp((int *)fakebuf + (size_t)chunkOffset, (int *)storage + (size_t)chunkOffset, chunkSize, op);
        }
        if (!IsNullNode(rightChild)) {
            MPI_Recv((int *)storage + (size_t)chunkOffset, chunkSize, datatype, rightChild, TRANSFER + i, comm, MPI_STATUS_IGNORE);
            PerformOp((int *)fakebuf + (size_t)chunkOffset, (int *)storage + (size_t)chunkOffset, chunkSize, op);
        }
        if (rank != root) 
            MPI_Send((int *)fakebuf + (size_t)chunkOffset, chunkSize, datatype, parent, TRANSFER + i, comm);
    }

    if (root == rank) memcpy(recvbuf, fakebuf, (size_t)count * sizeof(int));

    free(storage);
    free(fakebuf);
    return MPI_SUCCESS;
}

double TimeFunction(int (*func)(const void *, void *, int, MPI_Datatype, MPI_Op, int, MPI_Comm), const void *buffer, void *result, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
    MPI_Barrier(comm);
    double start = MPI_Wtime();

    int output = func(buffer, result, count, datatype, op, root, comm);
    
    double elapsed = MPI_Wtime() - start;
    double maxElapsed;
    MPI_Reduce(&elapsed, &maxElapsed, 1, MPI_DOUBLE, MPI_MAX, root, comm);

    assert(output == MPI_SUCCESS);
    return maxElapsed;
}

void AssertResult(void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
    int rank, datasize;
    MPI_Comm_rank(comm, &rank);
    MPI_Type_size(datatype, &datasize);

    void *checker = NULL;
    if (rank == root) {
        checker = malloc((size_t)count * (size_t)datasize);
        if (checker == NULL) {
            fprintf(stderr, "Assertion failed: checker allocation failed\n");
            MPI_Abort(comm, EXIT_FAILURE);
        }
    }

    MPI_Reduce(sendbuf, checker, count, datatype, op, root, comm);

    if (rank == root && memcmp(recvbuf, checker, (size_t)count * (size_t)datasize) != 0) {
        fprintf(stderr, "Assertion failed: reduction result mismatch\n");
        MPI_Abort(comm, EXIT_FAILURE);
    }

    free(checker);
}

int main(int argC, char **argV) {
    MPI_Init(&argC, &argV);
    
    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    srand((unsigned int)time(NULL) + (unsigned int)rank);
    
    size_t count = 4;
    int iterations = 100;
    int log = 0;
    if (argC >= 2) count = (size_t)atoi(argV[1]);
    if (argC >= 3) iterations = atoi(argV[2]);
    if (argC >= 4) log = atoi(argV[3]);
    
    MPI_Op op = MPI_MAX;
    MPI_Datatype datatype = MPI_INT;
    int *numbers = (int *)malloc(count * sizeof(int));
    int *result = (int *)malloc(count * sizeof(int));
    GetNumbers(numbers, count);

    if (log) {
        PrintArr(numbers, count, rank, "OG");
    }

    double BMTime = 0.0, PTime = 0.0;
    for (int iter = 0; iter < iterations; iter++) {
        BMTime += TimeFunction(BinomialTreeReduce, numbers, result, (int)count, datatype, op, MANAGER, MPI_COMM_WORLD);
        AssertResult(numbers, result, (int)count, datatype, op, MANAGER, MPI_COMM_WORLD);

        PTime += TimeFunction(PipelinedReduce, numbers, result, (int)count, datatype, op, MANAGER, MPI_COMM_WORLD);
        AssertResult(numbers, result, (int)count, datatype, op, MANAGER, MPI_COMM_WORLD);
    }
    BMTime /= iterations;
    PTime /= iterations;

    if (IsManager(rank)) {
        printf("%lf,%lf\n", BMTime, PTime);
    }
    if (log) {
        PrintArr(result, count, rank, "RS");
    }

    free(numbers);
    free(result);
    MPI_Finalize();
    return 0;
}