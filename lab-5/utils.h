#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

#define MIN_GEN 10
#define MAX_GEN 100

static inline void GetNumbers(int *number, size_t numAmount) {
    for (size_t i = 0; i < numAmount; i++) {
        number[i] = MIN_GEN + rand() % (MAX_GEN - MIN_GEN);
    }
}

static inline void GetOp(int *operation) {
    int chosenOp = -1;
    printf("Enter number for the operation:\n1. Sum\n2. Max\n3. Min\n");

    if (scanf("%d", &chosenOp) != 1) {
        printf("Couldn't take input\n");
        return;
    }

    switch (chosenOp) {
        case 1:
        case 2:
        case 3:
            *operation = chosenOp;
            break;
        default:
            printf("Invalid input!\n");
            break;
    }
}

static inline void PerformOp(int *result, int *buffer, int count, MPI_Op op) {
    for (size_t i = 0; i < (size_t)count; i++) {
        if (op == MPI_SUM) {
            result[i] += buffer[i];
        } else if (op == MPI_MAX) {
            if (buffer[i] > result[i]) result[i] = buffer[i];
        } else if (op == MPI_MIN) {
            if (buffer[i] < result[i]) result[i] = buffer[i];
        }
    }
}

static inline void PrintArr(int *arr, size_t count, int rank, char *label) {
    fprintf(stdout, "[ Node %d ] (%s) ", rank, label);
    for (size_t i = 0; i < count; i++) fprintf(stdout, "%d ", arr[i]);
    fprintf(stdout, "\n");
    fflush(stdout);
}

#endif