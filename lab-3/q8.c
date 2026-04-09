#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MANAGER 0
#define IsManager(rank) (rank == MANAGER)
#define MAX_TEMP 50.0

#define min(a, b) ((a < b) ? a : b)
#define max(a, b) ((a > b) ? a : b)

void AssignTemperatures(double *temp, size_t size) {
    for (size_t i = 0; i < size; i++) {
        temp[i] = ((double)rand() / RAND_MAX) * MAX_TEMP;
    }
}

void PrintGatheredInfo(int *counts, double *temps, size_t size, int rank) {
    fprintf(stdout, "[ Node %d ]\n", rank);
    
    fprintf(stdout, "Counts: [");
    for (size_t i = 0; i < size; i++) {
        fprintf(stdout, "%d ", counts[i]);
    }
    fprintf(stdout, "]\n");

    fprintf(stdout, "Temperatures: [");
    for (size_t i = 0; i < size; i++) {
        fprintf(stdout, "%lf ", temps[i]);
    }
    fprintf(stdout, "]\n");
    
    fflush(stdout);
}

int main(int argC, char **argV) {
    srand((unsigned int)time(NULL));
    MPI_Init(&argC ,&argV);

    int rank, world;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world);

    size_t elementsPerProc = 6, totalElements;
    if (argC >= 2) elementsPerProc = (size_t)atoi(argV[1]);

    // Phase 1: Config Distribution
    int windowSize;
    double alertThreshold;    
    if (IsManager(rank)) {
        windowSize = min(3, (int)elementsPerProc);
        alertThreshold = 40.0;
    }
    
    MPI_Request windowSizeReq, alertThresholdReq;
    MPI_Ibcast(&windowSize, 1, MPI_INT, MANAGER, MPI_COMM_WORLD, &windowSizeReq);
    MPI_Ibcast(&alertThreshold, 1, MPI_DOUBLE, MANAGER, MPI_COMM_WORLD, &alertThresholdReq);
    
    // Phase 2: Data Scattering
    double *temperatures = NULL, *allotedTemperatures = (double *)malloc(elementsPerProc * sizeof(double));
    if (IsManager(rank)) {
        totalElements = elementsPerProc * (size_t)world;
        temperatures = (double *)malloc(totalElements * sizeof(double));
        AssignTemperatures(temperatures, totalElements);
    }

    MPI_Request temperaturesReq;
    MPI_Iscatter(temperatures, (int)elementsPerProc, MPI_DOUBLE, allotedTemperatures, (int)elementsPerProc, MPI_DOUBLE, MANAGER, MPI_COMM_WORLD, &temperaturesReq);

    // Phase 3: Local Computation
    MPI_Wait(&windowSizeReq, MPI_STATUS_IGNORE);
    MPI_Wait(&temperaturesReq, MPI_STATUS_IGNORE);
    
    size_t windowIndex;
    double windowTotalTemp = 0.0;
    for (windowIndex = 0; windowIndex < (size_t)windowSize - 1; windowIndex++) {
        windowTotalTemp += allotedTemperatures[windowIndex];
    }

    MPI_Wait(&alertThresholdReq, MPI_STATUS_IGNORE);
    
    int alertCount = 0;
    double maxAvgTemp = -__DBL_MAX__;
    for (windowIndex++; windowIndex < elementsPerProc; windowIndex++) {
        windowTotalTemp += allotedTemperatures[windowIndex];

        double avgTemp = (double)windowTotalTemp / windowSize;
        if (avgTemp >= alertThreshold) alertCount++;
        maxAvgTemp = max(maxAvgTemp, avgTemp);
        
        windowTotalTemp -= allotedTemperatures[windowIndex - (size_t)windowSize + 1];
    }

    // Phase 4: Result Collection
    int *alertCounts = NULL;
    double *maxAvgTemps = NULL;
    if (IsManager(rank)) {
        alertCounts = (int *)malloc((size_t)world * sizeof(int));
        maxAvgTemps = (double *)malloc((size_t)world * sizeof(double));
    }

    MPI_Request countsReq, avgTempsReq;
    MPI_Igather(&alertCount, 1, MPI_INT, alertCounts, 1, MPI_INT, MANAGER, MPI_COMM_WORLD, &countsReq);
    MPI_Igather(&maxAvgTemp, 1, MPI_DOUBLE, maxAvgTemps, 1, MPI_DOUBLE, MANAGER, MPI_COMM_WORLD, &avgTempsReq);

    // Phase 5: Data Aggregation
    int totalAlerts = 0;
    double globalMaxAvgTemp = 0.0;

    if (IsManager(rank)) {
        MPI_Wait(&countsReq, MPI_STATUS_IGNORE);
        MPI_Wait(&avgTempsReq, MPI_STATUS_IGNORE);
        PrintGatheredInfo(alertCounts, maxAvgTemps, (size_t)world, rank);
    }

    MPI_Request totalAlertReq, globalMaxAvgTempReq;
    MPI_Iallreduce(&alertCount, &totalAlerts, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD, &totalAlertReq);
    MPI_Iallreduce(&maxAvgTemp, &globalMaxAvgTemp, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD, &globalMaxAvgTempReq);

    // Print Results
    MPI_Wait(&totalAlertReq, MPI_STATUS_IGNORE);
    MPI_Wait(&globalMaxAvgTempReq, MPI_STATUS_IGNORE);

    printf("[ Node %d ] Total Alerts: %d, Global Max Average Temperature: %lf\n", rank, totalAlerts, globalMaxAvgTemp);

    free(temperatures);
    free(allotedTemperatures);
    free(alertCounts);
    free(maxAvgTemps);
    MPI_Finalize();
    return 0;
}