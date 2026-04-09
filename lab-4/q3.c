#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)
#define NULL_NODE -1
#define IsNullNode(rank) (rank == NULL_NODE)

#define TRANSFER 1

void GetMessage(char *, size_t);
double TimeFunction(int (*func)(void*, int, MPI_Datatype, int, MPI_Comm), void *, int, MPI_Datatype, int, MPI_Comm);
void AssertResult(void *, void *, int, MPI_Datatype, int, MPI_Comm);

static int chunks = 8;

void GetChildren(int rank, int world, int *parent, int *left, int *right) {
    *parent = (int)floor((double)(rank - 1) / 2);

    *left = ((rank + 1) << 1) - 1;
    if ((*left) + 1 > world) *left = NULL_NODE;
    
    *right = (rank + 1) << 1;
    if ((*right) + 1 > world) *right = NULL_NODE;
}

int BinaryTreeBroadcast(void *message, int messageLen, MPI_Datatype datatype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int parent, leftChild, rightChild;
    GetChildren(rank, world, &parent, &leftChild, &rightChild);

    int chunkSize = messageLen / chunks;
    for (int i = 0; i < chunks; i++) {
        if (rank != root)
            MPI_Recv((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, parent, TRANSFER + i, comm, MPI_STATUS_IGNORE);
        if (!IsNullNode(leftChild))
            MPI_Send((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, leftChild, TRANSFER + i, comm);
        if (!IsNullNode(rightChild))
            MPI_Send((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, rightChild, TRANSFER + i, comm);
    }

    return MPI_SUCCESS;
}

int IBinaryTreeBroadcast(void *message, int messageLen, MPI_Datatype datatype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int parent, leftChild, rightChild;
    GetChildren(rank, world, &parent, &leftChild, &rightChild);
    
    int chunkSize = messageLen / chunks;
    MPI_Request* recvRequest = (MPI_Request*)malloc((size_t)chunks * sizeof(MPI_Request));
    MPI_Request* leftSendRequest = (MPI_Request*)malloc((size_t)chunks * sizeof(MPI_Request));
    MPI_Request* rightSendRequest = (MPI_Request*)malloc((size_t)chunks * sizeof(MPI_Request));

    if (rank != root) {
        for (int i = 0; i < chunks; i++)
            MPI_Irecv((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, parent, TRANSFER + i, comm, &recvRequest[i]);
    }

    for (int i = 0; i < chunks; i++) {
        if (rank != root)
            MPI_Wait(&recvRequest[i], MPI_STATUS_IGNORE);
        if (!IsNullNode(leftChild))
            MPI_Isend((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, leftChild, TRANSFER + i, comm, &leftSendRequest[i]);
        if (!IsNullNode(rightChild))
            MPI_Isend((char *)message + (size_t)(i * chunkSize), chunkSize, datatype, rightChild, TRANSFER + i, comm, &rightSendRequest[i]);
    }

    if (!IsNullNode(leftChild))
        MPI_Waitall(chunks, leftSendRequest, MPI_STATUSES_IGNORE);
    if (!IsNullNode(rightChild))
        MPI_Waitall(chunks, rightSendRequest, MPI_STATUSES_IGNORE);

    free(recvRequest);
    free(leftSendRequest);
    free(rightSendRequest);
    return MPI_SUCCESS;
}

int main(int argC, char **argV) {
    MPI_Init(&argC, &argV);

    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    // Parsing Arguments
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

        blockingAvg += TimeFunction(BinaryTreeBroadcast, message, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);
        AssertResult(message, checker, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);

        nonBlockingAvg += TimeFunction(IBinaryTreeBroadcast, message, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);
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