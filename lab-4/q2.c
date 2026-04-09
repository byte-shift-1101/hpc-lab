#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)

#define TRANSFER 1

void GetMessage(char *, size_t);
double TimeFunction(int (*func)(void*, int, MPI_Datatype, int, MPI_Comm), void *, int, MPI_Datatype, int, MPI_Comm);
void AssertResult(void *, void *, int, MPI_Datatype, int, MPI_Comm);

static int chunks = 8;

int PipelineBroadcast(void *message, int messageLen, MPI_Datatype datatype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int chunkSize = messageLen / chunks;
    int nextRank = (rank + 1) % world, prevRank = (rank - 1 + world) % world;
    if (root == rank) {
        for (int i = 0; i < chunks; i++)
            MPI_Send((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, nextRank, TRANSFER + i, comm);
    } else {
        for (int i = 0; i < chunks; i++) {
            MPI_Recv((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, prevRank, TRANSFER + i, comm, MPI_STATUS_IGNORE);
            if (rank + 1 < world)
                MPI_Send((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, nextRank, TRANSFER + i, comm);
        }
    }

    return MPI_SUCCESS;
}

int IPipelineBroadcast(void *message, int messageLen, MPI_Datatype datatype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int chunkSize = messageLen / chunks;
    int nextRank = (rank + 1) % world, prevRank = (rank - 1 + world) % world;
    int isLast = (rank + 1 == world);

    if (root == rank) {
        MPI_Request* sendRequest = (MPI_Request*)malloc((size_t)chunks * sizeof(MPI_Request));
        for (int i = 0; i < chunks; i++)
            MPI_Isend((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, nextRank, TRANSFER + i, comm, &sendRequest[i]);
        MPI_Waitall(chunks, sendRequest, MPI_STATUSES_IGNORE);
        free(sendRequest);
    } else {
        MPI_Request* recvRequest = (MPI_Request*)malloc((size_t)chunks * sizeof(MPI_Request));
        for (int i = 0; i < chunks; i++)
            MPI_Irecv((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, prevRank, TRANSFER + i, comm, &recvRequest[i]);

        int sendCount = 0;
        MPI_Request* sendRequest = isLast ? NULL : (MPI_Request*)malloc((size_t)chunks * sizeof(MPI_Request));

        // Each chunk arrives in order
        for (int i = 0; i < chunks; i++) {
            MPI_Wait(&recvRequest[i], MPI_STATUS_IGNORE);
            if (isLast) continue;

            MPI_Isend((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, nextRank, TRANSFER + i, comm, &sendRequest[sendCount]);
            sendCount++;
        }

        if (sendCount > 0)
            MPI_Waitall(sendCount, sendRequest, MPI_STATUSES_IGNORE);

        free(recvRequest);
        free(sendRequest);
    }

    return MPI_SUCCESS;
}

int main(int argC, char **argV) {
    MPI_Init(&argC, &argV);

    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    // Parse arguments
    int messageLen = 1024;
    int iterations = 100;
    int log = 0;
    if (argC >= 2) messageLen = atoi(argV[1]);
    if (argC >= 3) chunks = atoi(argV[2]);
    if (argC >= 4) iterations = atoi(argV[3]);
    if (argC >= 5) log = atoi(argV[4]);

    char *message = (char *)malloc((size_t)messageLen * sizeof(char));
    char *checker = (char *)malloc((size_t)messageLen * sizeof(char));

    double blockingAvg = 0.0, nonBlockingAvg = 0.0, standardAvg = 0.0;
    for (int iter = 0; iter < iterations; iter++) {
        if (IsManager(rank)) GetMessage(message, (size_t)messageLen);

        blockingAvg += TimeFunction(PipelineBroadcast, message, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);
        AssertResult(message, checker, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);

        nonBlockingAvg += TimeFunction(IPipelineBroadcast, message, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);
        AssertResult(message, checker, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);

        standardAvg += TimeFunction(MPI_Bcast, message, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);
    }

    blockingAvg /= iterations;
    nonBlockingAvg /= iterations;
    standardAvg /= iterations;

    if (IsManager(rank)) {
        printf("%lf,%lf,%lf\n", blockingAvg, nonBlockingAvg, standardAvg);
    }
    if (log) {
        printf("[ Node %d ] Received Message: %s ...\n", rank, message);
    }

    free(message);
    free(checker);
    MPI_Finalize();
    return 0;
}