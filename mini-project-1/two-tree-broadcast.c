#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)
#define NULL_NODE -1
#define IsNullNode(rank) (rank == NULL_NODE)

#define SIGNAL_0 0
#define SIGNAL_1 1

static size_t rollingCount = 0;
void GetMessage(char *message, size_t len) {
    for (size_t i = 0; i < len - 1; i++) message[i] = 'a' + (char)((i + rollingCount) % 26);
    message[len - 1] = '\0';
    rollingCount++;
}

double TimeFunction(int (*func)(void*, int, MPI_Datatype, int, MPI_Comm), void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();

    int result = func(buffer, count, datatype, root, comm);
    
    double elapsed = MPI_Wtime() - start;
    double maxElapsed;
    MPI_Reduce(&elapsed, &maxElapsed, 1, MPI_DOUBLE, MPI_MAX, MANAGER, MPI_COMM_WORLD);

    assert(result == MPI_SUCCESS);
    return maxElapsed;
}

void GetParent(int, int, int *);
void GetLeftChild(int, int, int *);
void GetRightChild(int, int, int *);

void GetParent(int rank, int world, int *parent) {
    int bits = (int)ceil(log2((double)world));
    int lsb = rank & -rank;
    int bitMask = (1 << bits) - 1;

    *parent = -1;
    if (IsManager(rank)) return;

    *parent = ((rank & ~lsb) | (lsb << 1)) & bitMask;
    while ((*parent) >= world) {
        GetParent(*parent, world, parent);
    }
}

void GetLeftChild(int rank, int world, int *left) {
    int bits = (int)ceil(log2((double)world));
    int lsb = rank & -rank;
    
    *left = -1;
    if (lsb == 1) {
        return;
    } else if (IsManager(rank)) {
        *left = 1 << (bits - 1);
        return;
    }
    
    *left = (rank & ~lsb) | (lsb >> 1);
    while ((*left) <= 0) {
        GetRightChild(*left, world, left);
    }
}

void GetRightChild(int rank, int world, int *right) {
    int lsb = rank & -rank;
    
    *right = -1;
    if (lsb == 1 || IsManager(rank)) return;
    
    *right = rank | (lsb >> 1);
    while ((*right) >= world) {
        GetLeftChild(*right, world, right);
    }
}

void GetFirstTree(int rank, int world, int *parent, int *left, int *right) {
    GetParent(rank, world, parent);
    GetLeftChild(rank, world, left);
    GetRightChild(rank, world, right);
}

int MirrorRank(int rank, int world) { return IsNullNode(rank) ? NULL_NODE : (world - rank) % world; }
void GetSecondTree_Mirror(int rank, int world, int *parent, int *left, int *right) {
    assert(world % 2 != 0);

    int newRank = MirrorRank(rank, world);
    GetFirstTree(newRank, world, parent, left, right);

    *parent = MirrorRank(*parent, world);
    *left = MirrorRank(*left, world);
    *right = MirrorRank(*right, world);
}

int DecrementRank(int rank) { return IsNullNode(rank) ? NULL_NODE : rank - 1; }
void GetSecondTree_Shift(int rank, int world, int *parent, int *left, int *right) {
    assert(world % 2 == 0);
    
    int newRank = IsManager(rank) ? rank : rank + 1;
    GetFirstTree(newRank, world + 1, parent, left, right);

    *parent = (IsManager(*parent)) ? *parent : DecrementRank(*parent);
    *left = DecrementRank(*left);
    *right = DecrementRank(*right);
}

void GetTree(int rank, int world, int *parent, int *left, int *right, int isSecondTree) {
    if (isSecondTree) {
        if (world % 2 == 0) GetSecondTree_Shift(rank, world, parent, left, right);
        else GetSecondTree_Mirror(rank, world, parent, left, right);
    } else {
        GetFirstTree(rank, world, parent, left, right);
    }
}

int AssignColors(int rank) {
    int lsb = rank & -rank;
    int colorIndexer = rank >> ((int)log2(lsb) + 1);
    
    int leftColor = 0;
    while (colorIndexer) {
        leftColor ^= colorIndexer & 1;
        colorIndexer = colorIndexer >> 1;
    }
    return leftColor;
}

int ITwoTreeBroadcast(void *message, int messageLen, MPI_Datatype datatype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);
    
    int parentT1, leftChildT1, rightChildT1, parentT2, leftChildT2, rightChildT2;
    GetTree(rank, world, &parentT1, &leftChildT1, &rightChildT1, 0);
    GetTree(rank, world, &parentT2, &leftChildT2, &rightChildT2, 1);
    if (leftChildT1 == root) leftChildT1 = NULL_NODE;
    if (rightChildT1 == root) rightChildT1 = NULL_NODE;
    if (leftChildT2 == root) leftChildT2 = NULL_NODE;
    if (rightChildT2 == root) rightChildT2 = NULL_NODE;
    
    int isLeafT1 = IsNullNode(leftChildT1) && IsNullNode(rightChildT1);
    int leftColorT1 = -1, leftColorT2 = -1;
    if (!isLeafT1) {
        if (!IsNullNode(leftChildT1)) leftColorT1 = AssignColors(leftChildT1);
        else leftColorT1 = 1 - AssignColors(rightChildT1);
    } else {
        if (!IsNullNode(leftChildT2)) leftColorT2 = 1 - AssignColors(leftChildT2);
        else if (!IsNullNode(rightChildT2)) leftColorT2 = AssignColors(rightChildT2);
    }
    if (rank == root) leftColorT2 = 1 - leftColorT1;
    
    // remove buffers and use message directly for receiving and sending
    char *bufferT1 = malloc((size_t)messageLen / 2 * sizeof(char));
    char *bufferT2 = malloc((size_t)messageLen / 2 * sizeof(char));
    MPI_Request leftSendRequest, rightSendRequest;
    int signal;
    if (rank != root) {
        MPI_Request recvRequestT1, recvRequestT2;
        MPI_Irecv(bufferT1, messageLen / 2, datatype, parentT1, MPI_ANY_TAG, comm, &recvRequestT1);
        MPI_Irecv(bufferT2, messageLen / 2, datatype, parentT2, MPI_ANY_TAG, comm, &recvRequestT2);

        MPI_Status recvStatus;
        if (!isLeafT1) {
            MPI_Wait(&recvRequestT1, &recvStatus);
            memcpy((char *)message, bufferT1, (size_t)messageLen / 2 * sizeof(char));
        } else {
            MPI_Wait(&recvRequestT2, &recvStatus);
            memcpy((char *)message + (size_t)messageLen / 2, bufferT2, (size_t)messageLen / 2 * sizeof(char));
        }
        signal = (recvStatus.MPI_TAG) - 1;
        signal = 1 - signal;

        if (!isLeafT1) {
            if (leftColorT1 == signal) {
                if (!IsNullNode(leftChildT1)) MPI_Isend(message, messageLen / 2, datatype, leftChildT1, signal + 1, comm, &leftSendRequest);
                if (!IsNullNode(rightChildT1)) MPI_Isend(message, messageLen / 2, datatype, rightChildT1, signal + 1, comm, &rightSendRequest);
            } else if (leftColorT1 == 1 - signal) {
                if (!IsNullNode(rightChildT1)) MPI_Isend(message, messageLen / 2, datatype, rightChildT1, signal + 1, comm, &rightSendRequest);
                if (!IsNullNode(leftChildT1)) MPI_Isend(message, messageLen / 2, datatype, leftChildT1, signal + 1, comm, &leftSendRequest);
            }
            MPI_Wait(&recvRequestT2, MPI_STATUS_IGNORE);
            memcpy((char *)message + (size_t)messageLen / 2, bufferT2, (size_t)messageLen / 2 * sizeof(char));
        } else {
            if (leftColorT2 == signal) {
                if (!IsNullNode(leftChildT2)) MPI_Isend((char *)message + (size_t)messageLen / 2, messageLen / 2, datatype, leftChildT2, signal + 1, comm, &leftSendRequest);
                if (!IsNullNode(rightChildT2)) MPI_Isend((char *)message + (size_t)messageLen / 2, messageLen / 2, datatype, rightChildT2, signal + 1, comm, &rightSendRequest);
            } else if (leftColorT2 == 1 - signal) {
                if (!IsNullNode(rightChildT2)) MPI_Isend((char *)message + (size_t)messageLen / 2, messageLen / 2, datatype, rightChildT2, signal + 1, comm, &rightSendRequest);
                if (!IsNullNode(leftChildT2)) MPI_Isend((char *)message + (size_t)messageLen / 2, messageLen / 2, datatype, leftChildT2, signal + 1, comm, &leftSendRequest);
            }
            MPI_Wait(&recvRequestT1, MPI_STATUS_IGNORE);
            memcpy((char *)message, bufferT1, (size_t)messageLen / 2 * sizeof(char));
        }

        if (!IsNullNode(leftChildT1) || !IsNullNode(leftChildT2)) MPI_Wait(&leftSendRequest, MPI_STATUS_IGNORE);
        if (!IsNullNode(rightChildT1) || !IsNullNode(rightChildT2)) MPI_Wait(&rightSendRequest, MPI_STATUS_IGNORE);
    } else {
        if (!IsNullNode(leftChildT1)) MPI_Isend(message, messageLen / 2, datatype, leftChildT1, SIGNAL_0 + 1, comm, &leftSendRequest);
        if (!IsNullNode(leftChildT2)) MPI_Isend((char *)message + (size_t)messageLen / 2, messageLen / 2, datatype, leftChildT2, SIGNAL_1 + 1, comm, &rightSendRequest);

        if (!IsNullNode(leftChildT1)) MPI_Wait(&leftSendRequest, MPI_STATUS_IGNORE);
        if (!IsNullNode(leftChildT2)) MPI_Wait(&rightSendRequest, MPI_STATUS_IGNORE);
    }
        
    free(bufferT1);
    free(bufferT2);
    return MPI_SUCCESS;
}

int main(int argC, char **argV) {
    MPI_Init(&argC, &argV);

    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    // Parsing Arguments
    int messageLen = 131072;
    int iterations = 1;
    if (argC >= 2) messageLen = atoi(argV[1]);
    if (argC >= 3) iterations = atoi(argV[2]);

    char *message = (char *)malloc((size_t)messageLen * sizeof(char));
    double nonBlockingAvg = 0.0;
    for (int iter = 0; iter < iterations; iter++) {
        if (IsManager(rank)) GetMessage(message, (size_t)messageLen);
        nonBlockingAvg += TimeFunction(ITwoTreeBroadcast, message, messageLen, MPI_CHAR, MANAGER, MPI_COMM_WORLD);
    }
    nonBlockingAvg /= iterations;

    if (IsManager(rank)) printf("%lf\n", nonBlockingAvg);
    
    free(message);
    MPI_Finalize();
    return 0;
}