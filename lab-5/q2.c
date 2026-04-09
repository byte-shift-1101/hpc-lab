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

void HandoverExtra(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Comm comm) {
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
            MPI_Recv(recvbuf, count, datatype, source, TRANSFER, comm, MPI_STATUS_IGNORE);
        }
    }
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

int RecursiveDoubling(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    assert(sendcount == recvcount);

    void *storage = malloc((size_t)(recvcount * world) * sizeof(int));
    void *package = malloc((size_t)(recvcount * world) * sizeof(int));
    memcpy((int *)recvbuf + (size_t)(rank * sendcount), sendbuf, (size_t)sendcount * sizeof(int));

    int steps = (int)floor(log2((double)world));
    int workers = 1 << steps;

    int extra = world - workers;
    if (rank >= workers) {
        int dest = rank - workers;
        MPI_Send(sendbuf, sendcount, sendtype, dest, TRANSFER, comm);
        MPI_Recv(recvbuf, world * recvcount, recvtype, dest, TRANSFER, comm, MPI_STATUS_IGNORE);
    } else {
        int *received = calloc((size_t)world, sizeof(int));
        received[rank] = 1;

        int source = rank + workers;
        if (source < world) {
            MPI_Recv((int *)recvbuf + (size_t)(source * sendcount), sendcount, recvtype, source, TRANSFER, comm, MPI_STATUS_IGNORE);
            received[source] = 1;
        }

        int sendOffset, sendSize, recvOffset, recvSize, overallOffset = rank;
        for (int _ = 0; _ < steps; _++) {
            int otherRank = rank ^ (1 << _);
            int recvRankCovered = (rank >> _) << _, sendRankCovered = (otherRank >> _) << _;
            int minimumRecvRank = (otherRank >> _) << _;
            recvSize = sendSize = 1 << _;

            sendOffset = overallOffset;
            if (rank < otherRank) recvOffset = overallOffset + (1 << _);
            else recvOffset = overallOffset - (1 << _);

            if (extra > sendRankCovered) recvSize += minimum(extra - sendRankCovered, (1 << _));
            if (extra > recvRankCovered) sendSize += minimum(extra - recvRankCovered, (1 << _));

            memset(package, 0, (size_t)(recvcount * world) * sizeof(int));
            int index = 0;
            for (int i = 0; i < world; i++) {
                if (received[i]) {
                    memcpy((int *)package + (size_t)(index * recvcount), (int *)recvbuf + (size_t)(i * recvcount), (size_t)recvcount * sizeof(int));
                    index++;
                }
            }

            Exchange(
                (int *)package, sendSize * recvcount, otherRank,
                (int *)storage, recvSize * recvcount, otherRank,
                recvtype, comm, rank < otherRank
            );

            memcpy((int *)recvbuf + (size_t)(recvOffset * recvcount), storage, (size_t)((1 << _) * recvcount) * sizeof(int));
            for (int i = 0; i < (1 << _); i++) received[recvOffset + i] = 1;
            if (recvSize > (1 << _)) {
                memcpy((int *)recvbuf + (size_t)((workers + minimumRecvRank) * recvcount), (int *)storage + (size_t)((1 << _) * recvcount), (size_t)((recvSize - (1 << _)) * recvcount) * sizeof(int));
                for (int i = 0; i < recvSize - (1 << _); i++) received[workers + minimumRecvRank + i] = 1;
            }

            if (rank < otherRank) overallOffset = sendOffset;
            else overallOffset = recvOffset;
        }

        if (source < world) {
            MPI_Send(recvbuf, world * recvcount, recvtype, source, TRANSFER, comm);
        }

        free(received);
    }

    free(storage);
    free(package);
    return MPI_SUCCESS;
}

void CyclicDecrement(int *value, int max) { *value = ((*value) - 1 + max) % max; }
void CalculateSizeAndOffset(int *size, int *offset, int index, int base, int extra) {
    *size = base + ((index < extra) ? 1 : 0);
    *offset = (index * base) + (minimum(index, extra));
}

int Ring(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    assert(sendtype == recvtype);
    memcpy((int *)recvbuf + (size_t)(rank * recvcount), sendbuf, (size_t)sendcount * sizeof(int));
    
    int prevRank = (rank - 1 + world) % world, nextRank = (rank + 1) % world;
    int recvIndex = rank, sendIndex = rank + 1;
    for (int _ = 0; _ < world - 1; _++) {
        CyclicDecrement(&recvIndex, world);
        CyclicDecrement(&sendIndex, world);

        Exchange(
            (int *)recvbuf + (size_t)sendIndex, sendcount, nextRank,
            (int *)recvbuf + (size_t)recvIndex, recvcount, prevRank,
            recvtype, comm, IsManager(rank)
        );
    }

    return MPI_SUCCESS;
}

double TimeFunction(int (*func)(const void *, int, MPI_Datatype, void *, int, MPI_Datatype, MPI_Comm), const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();

    int output = func(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
    
    double elapsed = MPI_Wtime() - start;
    double maxElapsed;
    MPI_Reduce(&elapsed, &maxElapsed, 1, MPI_DOUBLE, MPI_MAX, MANAGER, MPI_COMM_WORLD);

    assert(output == MPI_SUCCESS);
    return maxElapsed;
}

void AssertResult(void *sendbuf, int sendcount, void *result, int recvcount, MPI_Datatype datatype, MPI_Comm comm) {
    int world, datasize;
    MPI_Comm_size(comm, &world);
    MPI_Type_size(datatype, &datasize);
    
    void *checker = malloc((size_t)(world * recvcount * datasize));
    MPI_Allgather(sendbuf, sendcount, datatype, checker, recvcount, datatype, comm);
    if (memcmp(result, checker, (size_t)(world * recvcount * datasize)) != 0) {
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
    
    MPI_Datatype datatype = MPI_INT;
    int *numbers = (int *)malloc(count * sizeof(int));
    int *result = (int *)malloc(count * (size_t)world * sizeof(int));
    GetNumbers(numbers, count);

    if (log) {
        PrintArr(numbers, count, rank, "OG");
    }

    double RDTime = 0.0, RingTime = 0.0;
    for (int iter = 0; iter < iterations; iter++) {
        RDTime += TimeFunction(RecursiveDoubling, numbers, (int)count, datatype, result, (int)count, datatype, MPI_COMM_WORLD);
        AssertResult(numbers, (int)count, result, (int)count, datatype, MPI_COMM_WORLD);

        RingTime += TimeFunction(Ring, numbers, (int)count, datatype, result, (int)count, datatype, MPI_COMM_WORLD);
        AssertResult(numbers, (int)count, result, (int)count, datatype, MPI_COMM_WORLD);
    }
    RDTime /= iterations;
    RingTime /= iterations;

    if (IsManager(rank)) {
        printf("%lf,%lf\n", RDTime, RingTime);
    }
    if (log) {
        PrintArr(result, count * (size_t)world, rank, "RS");
    }

    free(numbers);
    free(result);
    MPI_Finalize();
    return 0;
}