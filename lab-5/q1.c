#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include "utils.h"

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)

#define TRANSFER 1

#define minimum(a, b) (a < b) ? a : b

void Exchange(const void *sendbuf, int sendCount, int sendRank, void *recvbuf, int recvCount, int recvRank, MPI_Datatype datatype, MPI_Comm comm, int differentiator) {
    if (differentiator) {
        MPI_Recv(recvbuf, recvCount, datatype, recvRank, TRANSFER, comm, MPI_STATUS_IGNORE);
        MPI_Send(sendbuf, sendCount, datatype, sendRank, TRANSFER, comm);
    } else {
        MPI_Send(sendbuf, sendCount, datatype, sendRank, TRANSFER, comm);
        MPI_Recv(recvbuf, recvCount, datatype, recvRank, TRANSFER, comm, MPI_STATUS_IGNORE);
    }
}

int HandoverExtra(const void *sendbuf, void *storage, int count, MPI_Datatype datatype, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int workers = 1 << (int)floor(log2((double)world));
    if (rank >= workers) {
        int dest = rank - workers;
        MPI_Send(sendbuf, count, datatype, dest, TRANSFER, comm);
    } else {
        int source = rank + workers;
        if (source < world) {
            MPI_Recv(storage, count, datatype, source, TRANSFER, comm, MPI_STATUS_IGNORE);
            return 1;
        }
    }
    return 0;
}

void ShareResults(void *recvbuf, int count, MPI_Datatype datatype, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int workers = 1 << (int)floor(log2((double)world));
    if (rank >= workers) {
        int source = rank - workers;
        MPI_Recv(recvbuf, count, datatype, source, TRANSFER, comm, MPI_STATUS_IGNORE);
    } else {
        int dest = rank + workers;
        if (dest < world) MPI_Send(recvbuf, count, datatype, dest, TRANSFER, comm);
    }
}

int RecursiveDoubling(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    memcpy(recvbuf, sendbuf, (size_t)count * sizeof(int));
    void *storage = malloc((size_t)count * sizeof(int));

    int received = HandoverExtra(recvbuf, storage, count, datatype, comm);
    if (received) PerformOp((int *)recvbuf, (int *)storage, count, op);
    
    int steps = (int)floor(log2(world));
    if (rank < (1 << steps)) {
        for (int _ = 0; _ < steps; _++) {
            int otherRank = rank ^ (1 << _);

            Exchange(
                recvbuf, count, otherRank,
                storage, count, otherRank,
                datatype, comm, rank < otherRank
            );

            PerformOp((int *)recvbuf, (int *)storage, count, op);
        }
    }

    ShareResults(recvbuf, count, datatype, comm);
    free(storage);
    return MPI_SUCCESS;
}

int RecursiveHalvingDoubling(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    memcpy(recvbuf, sendbuf, (size_t)count * sizeof(int));
    int *storage = (int *)malloc((size_t)count * sizeof(int));

    int received = HandoverExtra(recvbuf, storage, count, datatype, comm);
    if (received) PerformOp((int *)recvbuf, (int *)storage, count, op);
    
    int steps = (int)floor(log2((double)world));
    
    if (rank >= (1 << steps)) {
        ShareResults(recvbuf, count, datatype, comm);
        free(storage);
        return MPI_SUCCESS;
    }
    
    int transferSize = count;
    int *backtrackSizes = (int *)malloc((size_t)steps * sizeof(int));
    int sendSize, sendOffset, recvSize, recvOffset, overallOffset = 0;
    for (int _ = steps - 1; _ >= 0; _--) {
        int otherRank = rank ^ (1 << _);

        if (rank < otherRank) {
            recvSize = transferSize / 2;
            sendSize = transferSize - recvSize;
        } else {
            sendSize = transferSize / 2;
            recvSize = transferSize - sendSize;
        }
        recvOffset = overallOffset + ((rank >> _) & 1) * sendSize, sendOffset = overallOffset + ((otherRank >> _) & 1) * recvSize;
        
        Exchange(
            (int *)recvbuf + (size_t)sendOffset, sendSize, otherRank,
            (int *)storage + (size_t)recvOffset, recvSize, otherRank,
            datatype, comm, rank < otherRank
        );

        PerformOp((int *)recvbuf + (size_t)recvOffset, (int *)storage + (size_t)recvOffset, (size_t)recvSize, op);
        backtrackSizes[_] = sendSize;
        transferSize = recvSize;
        overallOffset = recvOffset;
    }
    
    for (int _ = 0; _ < steps; _++) {
        int otherRank = rank ^ (1 << _);
        
        sendOffset = overallOffset;
        if (rank < otherRank) {
            recvOffset = overallOffset + transferSize;
        } else {
            recvOffset = overallOffset - backtrackSizes[_];
        }

        Exchange(
            (int *)recvbuf + (size_t)sendOffset, transferSize, otherRank,
            (int *)recvbuf + (size_t)recvOffset, backtrackSizes[_], otherRank,
            datatype, comm, rank < otherRank
        );
        
        transferSize += backtrackSizes[_];
        if (rank < otherRank) overallOffset = sendOffset;
        else overallOffset = recvOffset;
    }
    
    ShareResults(recvbuf, count, datatype, comm);
    free(storage);
    free(backtrackSizes);
    return MPI_SUCCESS;
}

void CyclicDecrement(int *value, int max) { *value = ((*value) - 1 + max) % max; }
void CalculateSizeAndOffset(int *size, int *offset, int index, int base, int extra) {
    *size = base + ((index < extra) ? 1 : 0);
    *offset = (index * base) + (minimum(index, extra));
}

int Ring(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    memcpy(recvbuf, sendbuf, (size_t)count * sizeof(int));
    void *storage = malloc((size_t)count * sizeof(int));
    int baseChunkSize = (int)(count / world), extraChunks = count - baseChunkSize * world;

    int prevRank = (rank - 1 + world) % world, nextRank = (rank + 1) % world;
    int recvIndex = rank, recvSize, recvOffset, sendIndex = rank + 1, sendSize, sendOffset;
    for (int _ = 0; _ < world - 1; _++) {
        CyclicDecrement(&recvIndex, world);
        CyclicDecrement(&sendIndex, world);

        CalculateSizeAndOffset(&recvSize, &recvOffset, recvIndex, baseChunkSize, extraChunks);
        CalculateSizeAndOffset(&sendSize, &sendOffset, sendIndex, baseChunkSize, extraChunks);

        Exchange(
            (int *)recvbuf + (size_t)sendOffset, sendSize, nextRank,
            (int *)storage + (size_t)recvOffset, recvSize, prevRank,
            datatype, comm, IsManager(rank)
        );
        
        PerformOp((int *)recvbuf + (size_t)recvOffset, (int *)storage + (size_t)recvOffset, recvSize, op);
    }
    
    for (int _ = 0; _ < world - 1; _++) {
        CyclicDecrement(&recvIndex, world);
        CyclicDecrement(&sendIndex, world);

        CalculateSizeAndOffset(&recvSize, &recvOffset, recvIndex, baseChunkSize, extraChunks);
        CalculateSizeAndOffset(&sendSize, &sendOffset, sendIndex, baseChunkSize, extraChunks);

        Exchange(
            (int *)recvbuf + (size_t)sendOffset, sendSize, nextRank,
            (int *)recvbuf + (size_t)recvOffset, recvSize, prevRank,
            datatype, comm, IsManager(rank)
        );
    }

    free(storage);
    return MPI_SUCCESS;
}

double TimeFunction(int (*func)(const void *, void *, int, MPI_Datatype, MPI_Op, MPI_Comm), const void *buffer, void *result, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();

    int output = func(buffer, result, count, datatype, op, comm);
    
    double elapsed = MPI_Wtime() - start;
    double maxElapsed;
    MPI_Reduce(&elapsed, &maxElapsed, 1, MPI_DOUBLE, MPI_MAX, MANAGER, MPI_COMM_WORLD);

    assert(output == MPI_SUCCESS);
    return maxElapsed;
}

void AssertResult(void *buffer, void *result, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    int datasize;
    MPI_Type_size(datatype, &datasize);

    void *checker = malloc((size_t)(count * datasize));
    MPI_Allreduce(buffer, checker, count, datatype, op, comm);
    if (memcmp(result, checker, (size_t)(count * datasize)) != 0) {
        fprintf(stderr, "Assertion failed: buffers are not equal\n");
        exit(EXIT_FAILURE);
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

    double RDTime = 0.0, RHDTime = 0.0, RingTime = 0.0;
    for (int iter = 0; iter < iterations; iter++) {
        RDTime += TimeFunction(RecursiveDoubling, numbers, result, (int)count, datatype, op, MPI_COMM_WORLD);
        AssertResult(numbers, result, (int)count, datatype, op, MPI_COMM_WORLD);

        RHDTime += TimeFunction(RecursiveHalvingDoubling, numbers, result, (int)count, datatype, op, MPI_COMM_WORLD);
        AssertResult(numbers, result, (int)count, datatype, op, MPI_COMM_WORLD);

        RingTime += TimeFunction(Ring, numbers, result, (int)count, datatype, op, MPI_COMM_WORLD);
        AssertResult(numbers, result, (int)count, datatype, op, MPI_COMM_WORLD);
    }
    RDTime /= iterations;
    RHDTime /= iterations;
    RingTime /= iterations;

    if (IsManager(rank)) {
        printf("%lf,%lf,%lf\n", RDTime, RHDTime, RingTime);
    }
    if (log) {
        PrintArr(result, count, rank, "RS");
    }

    free(numbers);
    free(result);
    MPI_Finalize();
    return 0;
}