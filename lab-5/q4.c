#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include "utils.h"

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)
#define NULL_NODE -1
#define IsNullNode(node) (node == NULL_NODE)

#define FINISHED 1
#define TRANSFER 2

#define minimum(a, b) (a < b) ? a : b
#define maximum(a, b) (a > b) ? a : b

static int chunks = 8;

int LinearGather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    void *storage = malloc((size_t)(world * recvcount) * sizeof(int));
    if (rank == root) memcpy((int *)recvbuf + (size_t)(rank * sendcount), sendbuf, (size_t)sendcount * sizeof(int));
    else memcpy((int *)storage, sendbuf, (size_t)sendcount * sizeof(int));

    int base = sendcount / chunks, extra = sendcount % chunks;
    int recvRank = (rank + 1) % world, sendRank = (rank - 1 + world) % world;
    int chunkOffset, chunkSize;

    for (int i = rank; i < world; i++) {
        if (i == root) continue;
        for (int c = 0; c < chunks; c++) {
            chunkSize = base + ((c < extra) ? 1 : 0);
            chunkOffset = (c * base) + (minimum(c, extra));
            if (chunkSize == 0) continue;

            if (rank != root) {
                MPI_Send((int *)storage + (size_t)chunkOffset, chunkSize, sendtype, sendRank, TRANSFER + c, comm);
                if (rank + 1 < world && i < world - 1) {
                    MPI_Recv((int *)storage + (size_t)chunkOffset, chunkSize, recvtype, recvRank, TRANSFER + c, comm, MPI_STATUS_IGNORE);
                }
            } else {
                MPI_Recv((int *)recvbuf + (size_t)(i * sendcount +  chunkOffset), chunkSize, recvtype, recvRank, TRANSFER + c, comm, MPI_STATUS_IGNORE);
            }
        }
    }

    free(storage);
    return MPI_SUCCESS;
}

int BinomialTreeGather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);
    
    int bits = (int)ceil(log2(world));
    int lsb = (rank != root) ? (rank & -rank) : (1 << bits);
    int lsbbits = (int)log2(lsb);

    void *storage = malloc((size_t)(world * recvcount) * sizeof(int));
    memcpy((int *)storage + (size_t)(rank * sendcount), sendbuf, (size_t)sendcount * sizeof(int));
    
    int recvsize = 1, isLast = 0;
    for (int i = 0; i < lsbbits; i++) {
        if (isLast) break;

        int source = rank | (1 << i);
        if (source + recvsize > world) {
            if (world >= source) recvsize = world - source;
            isLast = 1;
        }

        if (recvsize > 0) MPI_Recv((int *)storage + (size_t)(source * sendcount), recvsize * sendcount, recvtype, source, TRANSFER, comm, MPI_STATUS_IGNORE);
        recvsize = isLast ? world - rank : recvsize << 1;
    }

    if (rank != root) {
        int dest = rank & ~lsb;
        MPI_Send((int *)storage + (size_t)(rank * sendcount), recvsize * sendcount, sendtype, dest, TRANSFER, comm);
    } else memcpy(recvbuf, storage, (size_t)(world * recvcount) * sizeof(int));

    free(storage);
    return MPI_SUCCESS;
}

void GetChildren(int rank, int world, int *parent, int *left, int *right) {
    if (parent != NULL) *parent = (int)floor((double)(rank - 1) / 2);

    if (left != NULL) {
        *left = ((rank + 1) << 1) - 1;
        if ((*left) + 1 > world) *left = NULL_NODE;
    }
    
    if (right != NULL) {
        *right = (rank + 1) << 1;
        if ((*right) + 1 > world) *right = NULL_NODE;
    }
}

void GetSubtreeSize(int rank, int world, int *size) {
    if (IsNullNode(rank)) {
        *size = 0;
        return;
    }

    int left, right;
    GetChildren(rank, world, NULL, &left, &right);
    if (IsNullNode(left) && IsNullNode(right)) {
        *size = 1;
        return;
    }

    int leftSize = 0, rightSize = 0;
    if (!IsNullNode(left)) GetSubtreeSize(left, world, &leftSize);
    if (!IsNullNode(right)) GetSubtreeSize(right, world, &rightSize);
    *size = 1 + leftSize + rightSize;
}

void CopyToRecvbuf(void *recvbuf, void *recvpkg, MPI_Datatype type, int pkgsrc, int *index, int world, int *chunkoffsets, int *chunksizes, int chunk, int sendcount) {
    memcpy(
        (int *)recvbuf + (size_t)(pkgsrc * sendcount + chunkoffsets[chunk]),
        (int *)recvpkg + (size_t)((*index) * chunksizes[chunk]),
        (size_t)chunksizes[chunk] * sizeof(int)
    );
    *index = *index + 1;

    int left, right;
    GetChildren(pkgsrc, world, NULL, &left, &right);

    if (IsNullNode(left)) return;
    CopyToRecvbuf(recvbuf, recvpkg, type, left, index, world, chunkoffsets, chunksizes, chunk, sendcount);

    if (IsNullNode(right)) return;
    CopyToRecvbuf(recvbuf, recvpkg, type, right, index, world, chunkoffsets, chunksizes, chunk, sendcount);
}

int PipelinedGather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
    int rank, world;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);

    int parent, left, right, leftSubtree, rightSubtree;
    GetChildren(rank, world, &parent, &left, &right);
    GetSubtreeSize(left, world, &leftSubtree);
    GetSubtreeSize(right, world, &rightSubtree);

    void *storage = malloc((size_t)(world * recvcount) * sizeof(int));
    void *package = malloc((size_t)((1 + leftSubtree + rightSubtree) * sendcount) * sizeof(int));
    
    int base = sendcount / chunks, extra = sendcount % chunks;
    int chunkOffsets[chunks], chunkSizes[chunks];
    for (int c = 0; c < chunks; c++) {
        chunkSizes[c] = base + ((c < extra) ? 1 : 0);
        chunkOffsets[c] = (c * base) + (minimum(c, extra));
    }
    
    int leftrecvsize, rightrecvsize;
    for (int c = 0; c < chunks; c++) {
        if (chunkSizes[c] == 0) continue;

        leftrecvsize = chunkSizes[c] * leftSubtree, rightrecvsize = chunkSizes[c] * rightSubtree;
        if (!IsNullNode(left)) MPI_Recv(storage, leftrecvsize, recvtype, left, TRANSFER + c, comm, MPI_STATUS_IGNORE);
        if (!IsNullNode(right)) MPI_Recv((int *)storage + (size_t)(leftSubtree * sendcount), rightrecvsize, recvtype, right, TRANSFER + c, comm, MPI_STATUS_IGNORE);

        memset(package, 0, (size_t)((1 + leftSubtree + rightSubtree) * sendcount) * sizeof(int));
        memcpy((int *)package, &((int *)sendbuf)[chunkOffsets[c]], (size_t)chunkSizes[c] * sizeof(int));
        if (!IsNullNode(left)) memcpy((int *)package + (size_t)chunkSizes[c], storage, (size_t)leftrecvsize * sizeof(int));
        if (!IsNullNode(right)) memcpy((int *)package + (size_t)(chunkSizes[c] + leftrecvsize), (int *)storage + (size_t)(leftSubtree * sendcount), (size_t)rightrecvsize * sizeof(int));

        if (rank != root) {
            MPI_Send((int *)package, chunkSizes[c] + leftrecvsize + rightrecvsize, sendtype, parent, TRANSFER + c, comm);
        } else {
            int index = 0;
            CopyToRecvbuf(recvbuf, package, recvtype, rank, &index, world, chunkOffsets, chunkSizes, c, sendcount);
        }
    }

    free(storage);
    free(package);
    return MPI_SUCCESS;
}

double TimeFunction(int (*func)(const void *, int, MPI_Datatype, void *, int, MPI_Datatype, int, MPI_Comm), const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();

    int output = func(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm);

    double elapsed = MPI_Wtime() - start;
    double maxElapsed;
    MPI_Reduce(&elapsed, &maxElapsed, 1, MPI_DOUBLE, MPI_MAX, MANAGER, MPI_COMM_WORLD);

    assert(output == MPI_SUCCESS);
    return maxElapsed;
}

void AssertResult(void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
    int rank, world, datasize;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &world);
    MPI_Type_size(recvtype, &datasize);

    void *checker = malloc((size_t)world * (size_t)recvcount * (size_t)datasize);
    MPI_Gather(sendbuf, sendcount, sendtype, checker, recvcount, recvtype, root, comm);
    if (IsManager(rank) && memcmp(recvbuf, checker, (size_t)world * (size_t)recvcount * (size_t)datasize) != 0) {
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
    int *result = (int *)malloc((size_t)world * count * sizeof(int));
    GetNumbers(numbers, count);

    if (log) {
        PrintArr(numbers, count, rank, "OG");
    }

    double LTime = 0.0, BMTime = 0.0, PTime = 0.0;
    for (int iter = 0; iter < iterations; iter++) {
        LTime += TimeFunction(LinearGather, numbers, (int)count, datatype, result, (int)count, datatype, MANAGER, MPI_COMM_WORLD);
        AssertResult(numbers, (int)count, datatype, result, (int)count, datatype, MANAGER, MPI_COMM_WORLD);

        BMTime += TimeFunction(BinomialTreeGather, numbers, (int)count, datatype, result, (int)count, datatype, MANAGER, MPI_COMM_WORLD);
        AssertResult(numbers, (int)count, datatype, result, (int)count, datatype, MANAGER, MPI_COMM_WORLD);
        
        PTime += TimeFunction(PipelinedGather, numbers, (int)count, datatype, result, (int)count, datatype, MANAGER, MPI_COMM_WORLD);
        AssertResult(numbers, (int)count, datatype, result, (int)count, datatype, MANAGER, MPI_COMM_WORLD);
    }
    LTime /= iterations;
    BMTime /= iterations;
    PTime /= iterations;

    if (IsManager(rank)) {
        printf("%lf,%lf,%lf\n", LTime, BMTime, PTime);
        if (log) {
            PrintArr(result, (size_t)world * count, rank, "RS");
        }
    }

    free(numbers);
    free(result);
    MPI_Finalize();
    return 0;
}