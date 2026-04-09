#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)

#define REQUEST 1
#define REPLY 2

static int server;
int IsServer(int rank) { return rank == server; }

void GenerateNumbers(int *nums, size_t size) {
    for (size_t i = 0; i < size; i++) {
        nums[i] = rand() % RAND_MAX;
    }
}

typedef struct Point {
    double x, y;
} Point;

int IsPointIn(Point point) { return (point.x * point.x + point.y * point.y <= 1); }

int main(int argC, char **argV) {
    srand((unsigned int)time(NULL));
    MPI_Init(&argC, &argV);

    int worldRank, numProcs;
    MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs);

    // Creating new communicator
    MPI_Group worldGroup;
    MPI_Comm_group(MPI_COMM_WORLD, &worldGroup);

    server = numProcs - 1;
    int serverRanks[1];
    serverRanks[0] = server;

    MPI_Group workersGroup;
    MPI_Comm workers;
    MPI_Group_excl(worldGroup, 1, serverRanks, &workersGroup);
    MPI_Comm_create(MPI_COMM_WORLD, workersGroup, &workers);
    MPI_Group_free(&workersGroup);
    MPI_Group_free(&worldGroup);

    // Broadcasting configurations
    int chunkSize, numIterations;
    if (IsManager(worldRank)) {
        chunkSize = 1000;
        numIterations = 10000;
        if (argC >= 2) chunkSize = atoi(argV[1]);
        if (argC >= 3) numIterations = atoi(argV[2]);
    }

    MPI_Bcast(&chunkSize, 1, MPI_INT, MANAGER, MPI_COMM_WORLD);
    if (!IsServer(worldRank)) {
        MPI_Bcast(&numIterations, 1, MPI_INT, MANAGER, workers);
    }
    
    // Logic
    int *randomNumbers = (int *)malloc((size_t)chunkSize * sizeof(int));
    if (IsServer(worldRank)) {
        int request, source;
        MPI_Status status;
        while (1) {
            MPI_Recv(&request, 1, MPI_INT, MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &status);
            if (request == 0) break;

            source = status.MPI_SOURCE;

            GenerateNumbers(randomNumbers, (size_t)chunkSize);
            MPI_Send(randomNumbers, chunkSize, MPI_INT, source, REPLY, MPI_COMM_WORLD);
        }
    } else {
        int response = 1, currentIterations = 0, overallIterations = 0;
        int in = 0, out = 0;
        Point point;
        while (1) {
            MPI_Send(&response, 1, MPI_INT, server, REQUEST, MPI_COMM_WORLD);
            MPI_Recv(randomNumbers, chunkSize, MPI_INT, server, REPLY, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            for (size_t i = 0; i < (size_t)chunkSize; i += 2) {
                point.x = (double)randomNumbers[i] / RAND_MAX;
                point.y = (double)randomNumbers[i + 1] / RAND_MAX ;
                
                if (IsPointIn(point)) in++;
                else out++;
            }
            currentIterations++;

            MPI_Allreduce(&currentIterations, &overallIterations, 1, MPI_INT, MPI_SUM, workers);
            if (numIterations <= overallIterations) {
                response = 0;
                if (IsManager(worldRank)) {
                    MPI_Send(&response, 1, MPI_INT, server, REQUEST, MPI_COMM_WORLD);
                }

                break;
            }
        }

        int overallIn = 0, overallOut = 0;
        MPI_Allreduce(&in, &overallIn, 1, MPI_INT, MPI_SUM, workers);
        MPI_Allreduce(&out, &overallOut, 1, MPI_INT, MPI_SUM, workers);

        double estimatedPi = 4 * (double)overallIn / (overallIn + overallOut);
        printf("[ Node %d ] Estimated Pi: %lf\n", worldRank, estimatedPi);
        MPI_Comm_free(&workers);
    }
    
    free(randomNumbers);
    MPI_Finalize();
    return 0;
}