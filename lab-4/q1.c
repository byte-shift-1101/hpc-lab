#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)

#define TRANSFER 1

void GetMessage(char *, size_t);
double TimeFunction(int (*func)(void*, int, MPI_Datatype, int, MPI_Comm), void *, int, MPI_Datatype, int, MPI_Comm);
void AssertResult(void *, void *, int, MPI_Datatype, int, MPI_Comm);

int BinomialTreeBroadcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    if (root != rank) {
        MPI_Recv(buffer, count, datatype, MPI_ANY_SOURCE, TRANSFER, comm, MPI_STATUS_IGNORE);
    }

    int bits = (int)ceil(log2(world));
    int lsb = (rank != 0) ? (rank & -rank) : (1 << bits);

    while ((lsb = (lsb >> 1))) {
        int destRank = rank | lsb;
        if (destRank < world) {
            MPI_Send(buffer, count, datatype, destRank, TRANSFER, comm);
        }
    }

    return MPI_SUCCESS;
}

int IBinomialTreeBroadcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int bits = (int)ceil(log2(world));
    int lsb = (rank != 0) ? (rank & -rank) : (1 << bits);
    int sends = (int)log2(lsb);

    if (root != rank) {
        MPI_Request recvRequest;
        MPI_Irecv(buffer, count, datatype, MPI_ANY_SOURCE, TRANSFER, comm, &recvRequest);
        MPI_Wait(&recvRequest, MPI_STATUS_IGNORE);
    }

    if (sends == 0) return MPI_SUCCESS;

    int i = 0;
    MPI_Request* sendRequest = (MPI_Request*)malloc((size_t)sends * sizeof(MPI_Request));
    while ((lsb = (lsb >> 1))) {
        int destRank = rank | lsb;
        if (destRank < world) {
            MPI_Isend(buffer, count, datatype, destRank, TRANSFER, comm, &sendRequest[i]);
            i++;
        }
    }
    
    for (int j = 0; j < i; j++) {
        MPI_Wait(&sendRequest[j], MPI_STATUS_IGNORE);
    }

    free(sendRequest);
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
    if (argC >= 3) iterations = atoi(argV[2]);
    if (argC >= 4) log = atoi(argV[3]);

    char *message = (char *)malloc((size_t)messageLen * sizeof(char));
    char *checker = (char *)malloc((size_t)messageLen * sizeof(char));

    double blockingAvg = 0.0, nonBlockingAvg = 0.0, standardAvg = 0.0;
    for (int iter = 0; iter < iterations; iter++) {
        if (IsManager(rank)) GetMessage(message, (size_t)messageLen);

        blockingAvg += TimeFunction(BinomialTreeBroadcast, message, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);
        AssertResult(message, checker, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);

        nonBlockingAvg += TimeFunction(IBinomialTreeBroadcast, message, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);
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