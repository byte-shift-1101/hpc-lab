#include <mpi.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)

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

void AssertResult(void *message, void *checker, int messageLen, MPI_Datatype datatype, int root, MPI_Comm comm) {
    int rank;
    MPI_Comm_rank(comm, &rank);
    if (IsManager(rank)) {
        MPI_Bcast(message, messageLen, datatype, root, comm);
    } else {
        MPI_Bcast(checker, messageLen, datatype, root, comm);
        if (memcmp(message, checker, (size_t)messageLen) != 0) {
            fprintf(stderr, "Assertion failed: buffers are not equal\n");
            exit(EXIT_FAILURE);
        }
    }
}