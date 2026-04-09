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

int RecursiveHalvingDoubling(const void *sendbuf, void *recvbuf, const int recvcounts[], MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int *offsets = (int *)malloc((size_t)(world + 1) * sizeof(int));
    offsets[0] = 0;
    for (int i = 0; i < world; i++) offsets[i + 1] = offsets[i] + recvcounts[i];
    int totalcount = offsets[world];

    int *package = (int *)malloc((size_t)totalcount * sizeof(int));
    int *storage = (int *)malloc((size_t)totalcount * sizeof(int));
    memcpy(package, sendbuf, (size_t)totalcount * sizeof(int));

    int pof2 = 1;
    while ((pof2 << 1) <= world) pof2 <<= 1;
    int rem = world - pof2;

    int newrank = -1;
    if (rank < 2 * rem) {
        if ((rank & 1) == 0) {
            MPI_Send(package, totalcount, datatype, rank + 1, TRANSFER, comm);
            MPI_Recv(recvbuf, recvcounts[rank], datatype, rank + 1, TRANSFER + 1, comm, MPI_STATUS_IGNORE);

            free(storage);
            free(package);
            free(offsets);
            return MPI_SUCCESS;
        }

        MPI_Recv(storage, totalcount, datatype, rank - 1, TRANSFER, comm, MPI_STATUS_IGNORE);
        PerformOp(package, storage, totalcount, op);
        newrank = rank / 2;
    } else {
        newrank = rank - rem;
    }

    int active = pof2;
    int *blockCounts = (int *)malloc((size_t)active * sizeof(int));
    int *blockOffsets = (int *)malloc((size_t)(active + 1) * sizeof(int));
    blockOffsets[0] = 0;
    for (int i = 0; i < active; i++) {
        if (i < rem) blockCounts[i] = recvcounts[2 * i] + recvcounts[(2 * i) + 1];
        else blockCounts[i] = recvcounts[i + rem];
        blockOffsets[i + 1] = blockOffsets[i] + blockCounts[i];
    }

    int curLo = 0, curHi = blockOffsets[active];
    int steps = (int)log2((double)active);
    for (int step = steps - 1; step >= 0; step--) {
        int groupSize = 1 << (step + 1);
        int halfGroup = 1 << step;
        int groupBase = (newrank / groupSize) * groupSize;
        int split = groupBase + halfGroup;
        int splitOffset = blockOffsets[split];
        int partnerNew = newrank ^ (1 << step);
        int partnerRank = (partnerNew < rem) ? (2 * partnerNew + 1) : (partnerNew + rem);

        int sendLo, sendHi, recvLo, recvHi;
        if (newrank < split) {
            sendLo = splitOffset;
            sendHi = curHi;
            recvLo = curLo;
            recvHi = splitOffset;
            curHi = splitOffset;
        } else {
            sendLo = curLo;
            sendHi = splitOffset;
            recvLo = splitOffset;
            recvHi = curHi;
            curLo = splitOffset;
        }

        int sendCount = sendHi - sendLo;
        int recvCount = recvHi - recvLo;
        MPI_Sendrecv(
            package + sendLo, sendCount, datatype, partnerRank, TRANSFER,
            storage + recvLo, recvCount, datatype, partnerRank, TRANSFER,
            comm, MPI_STATUS_IGNORE
        );

        PerformOp(package + recvLo, storage + recvLo, recvCount, op);
    }

    if (rank < 2 * rem && (rank & 1)) {
        int pair = rank / 2;
        MPI_Send(package + offsets[2 * pair], recvcounts[2 * pair], datatype, rank - 1, TRANSFER + 1, comm);
        memcpy(recvbuf, package + offsets[rank], (size_t)recvcounts[rank] * sizeof(int));
    } else {
        memcpy(recvbuf, package + offsets[rank], (size_t)recvcounts[rank] * sizeof(int));
    }

    free(blockOffsets);
    free(blockCounts);

    free(package);
    free(storage);
    free(offsets);
    return MPI_SUCCESS;
}

void CyclicDecrement(int *value, int max) { *value = ((*value) - 1 + max) % max; }
void CalculateSizeAndOffset(int *size, int *offset, int index, int base, int extra) {
    *size = base + ((index < extra) ? 1 : 0);
    *offset = (index * base) + (minimum(index, extra));
}

int Ring(const void *sendbuf, void *recvbuf, const int recvcounts[], MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int totalcount = 0, *offsets = (int *)malloc((size_t)world * sizeof(int));
    for (int i = 0; i < world; i++) {
        offsets[i] = totalcount;
        totalcount += recvcounts[i];
    }

    int maxcount = 0;
    for (int i = 0; i < world; i++) {
        if (recvcounts[i] > maxcount) maxcount = recvcounts[i];
    }

    int *package = (int *)malloc((size_t)totalcount * sizeof(int));
    int *storage = (int *)malloc((size_t)maxcount * sizeof(int));
    memcpy(package, sendbuf, (size_t)totalcount * sizeof(int));
    
    int prevRank = (rank - 1 + world) % world, nextRank = (rank + 1) % world;
    for (int step = 0; step < world - 1; step++) {
        int sendChunk = (rank - step - 1 + world) % world;
        int recvChunk = (rank - step - 2 + world) % world;

        MPI_Sendrecv(
            package + offsets[sendChunk], recvcounts[sendChunk], datatype, nextRank, TRANSFER,
            storage, recvcounts[recvChunk], datatype, prevRank, TRANSFER,
            comm, MPI_STATUS_IGNORE
        );

        PerformOp(package + offsets[recvChunk], storage, recvcounts[recvChunk], op);
    }

    memcpy(recvbuf, package + offsets[rank], (size_t)recvcounts[rank] * sizeof(int));

    free(storage);
    free(package);
    free(offsets);
    return MPI_SUCCESS;
}

double TimeFunction(int (*func)(const void *, void *, const int[], MPI_Datatype, MPI_Op, MPI_Comm), const void *sendbuf, void *recvbuf, const int recvcounts[], MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();

    int output = func(sendbuf, recvbuf, recvcounts, datatype, op, comm);

    double elapsed = MPI_Wtime() - start;
    double maxElapsed;
    MPI_Reduce(&elapsed, &maxElapsed, 1, MPI_DOUBLE, MPI_MAX, MANAGER, MPI_COMM_WORLD);

    assert(output == MPI_SUCCESS);
    return maxElapsed;
}

void AssertResult(void *buffer, void *result, const int recvcounts[], MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    int rank, datasize;
    MPI_Type_size(datatype, &datasize);
    MPI_Comm_rank(comm, &rank);

    void *checker = malloc((size_t)(recvcounts[rank] * datasize));
    MPI_Reduce_scatter(buffer, checker, recvcounts, datatype, op, comm);
    if (memcmp(result, checker, (size_t)(recvcounts[rank] * datasize)) != 0) {
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
    MPI_Op op = MPI_MAX;
    int base = (int)count / world, extra = (int)count % world;
    int *recvcounts = (int *)malloc((size_t)world * sizeof(int));
    for (int i = 0; i < world; i++) recvcounts[i] = base + ((i < extra) ? 1 : 0);

    int *numbers = (int *)malloc(count * sizeof(int));
    int *result = (int *)malloc((size_t)recvcounts[rank] * sizeof(int));
    GetNumbers(numbers, count);

    if (log) {
        PrintArr(numbers, count, rank, "OG");
    }

    double RHDTime = 0.0, RingTime = 0.0;
    for (int iter = 0; iter < iterations; iter++) {
        RHDTime += TimeFunction(RecursiveHalvingDoubling, numbers, result, recvcounts, datatype, op, MPI_COMM_WORLD);
        AssertResult(numbers, result, recvcounts, datatype, op, MPI_COMM_WORLD);

        RingTime += TimeFunction(Ring, numbers, result, recvcounts, datatype, op, MPI_COMM_WORLD);
        AssertResult(numbers, result, recvcounts, datatype, op, MPI_COMM_WORLD);
    }
    RHDTime /= iterations;
    RingTime /= iterations;

    if (IsManager(rank)) {
        printf("%lf,%lf\n", RHDTime, RingTime);
    }
    if (log) {
        PrintArr(result, (size_t)recvcounts[rank], rank, "RS");
    }

    free(numbers);
    free(result);
    MPI_Finalize();
    return 0;
}