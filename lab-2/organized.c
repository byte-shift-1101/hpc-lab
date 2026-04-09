#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define MANAGER 0
int rows = 100, cols = 100;
int log_level = 1;

// Utility Functions
static void *xmalloc(size_t n) {
  void *p = malloc(n);
  if (!p) {
    fprintf(stderr, "malloc failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  return p;
}

static void logf(int enabled, double t0, int rank, const char *fmt, ...) {
  if (!enabled) return;
  double now = MPI_Wtime();
  fprintf(stdout, "[t=%9.6f] rank=%d ", now - t0, rank);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

void clamp(int *value, int min, int max) {
  *value = (*value > max) ? max : ((*value < min) ? min : *value);
}

// Vector Assignment
void InitializeVector(double **vec, size_t size) {
  *vec = (double*)xmalloc(size * sizeof(double));
}

void InitializeMatrix(double **matrix, size_t size_x, size_t size_y) {
  *matrix = (double*)xmalloc(size_x * size_y * sizeof(double));
}

void AssignValues(double *a, double *b, size_t size_x, size_t size_y) {
  for (size_t j = 0; j < size_y; j++) b[j] = 1.0;
  for (size_t i = 0; i < size_x; i++)
    for (size_t j = 0; j < size_y; j++)
      a[i * size_y + j] = (double)(i + 1);
}

// Computation
void DivideRowsForProcs(int *numRowsForProcs, int size_x, int numProcs) {
  int extra = size_x % (numProcs - 1);
  int base = size_x / (numProcs - 1);
  for (int i = 0; i < numProcs - 1; i++) {
    numRowsForProcs[i] = base;
    if (extra > 0) {
      numRowsForProcs[i]++;
      extra--;
    }
  }
}

double DotProduct(double *a, double *b, int size) {
  double ans = 0.0;
  for (int i = 0; i < size; i++)
    ans += a[i] * b[i];
  return ans;
}

// Validation and Error Handling
int IsManager(int myRank) { return myRank == MANAGER; }

void UsageError(int argC, char **argV) {
  if (argC == 0) return;
  fprintf(stderr, "Usage: %s rows cols [log_level]\n", argV[0]);
  MPI_Abort(MPI_COMM_WORLD, 1);
}

void ParseArgs(int argC, char **argV) {
  if (argC <= 2 || argC >= 5) UsageError(argC, argV);

  if (argC >= 3) {
    rows = atoi(argV[1]);
    cols = atoi(argV[2]);
  }
  if (argC >= 4) log_level = atoi(argV[3]);
  clamp(&log_level, 0, 3);

  if (rows <= 0 || cols <= 0) UsageError(argC, argV);
}

// Helper functions for manager and workers
void ShareResources(double *b) {
  MPI_Bcast(b, cols, MPI_DOUBLE, MANAGER, MPI_COMM_WORLD);
}

void RunManager(double *a, double *c, int numProcs, double startTime) {
  fprintf(stdout, "--- mv_sched_log: rows=%d cols=%d procs=%d log_level=%d ---\n", rows, cols, numProcs, log_level);
  fflush(stdout);

  // Calculate number of rows each worker has to process
  int *numRowsForProcs = (int *)xmalloc((size_t)(numProcs - 1) * sizeof(int));
  DivideRowsForProcs(numRowsForProcs, rows, numProcs);

  // Send rows to workers
  int rowIdx = 0;
  for (int destRank = 1; destRank < numProcs; destRank++) {
    int rowCount = numRowsForProcs[destRank - 1];
    int tag = rowIdx;

    if (log_level >= 1) logf(1, startTime, MANAGER, "SEND row=%d -> rank=%d", rowIdx, destRank);

    MPI_Send(&rowCount, 1, MPI_INT, destRank, 0, MPI_COMM_WORLD);
    MPI_Send(a + rowIdx * cols, rowCount * cols, MPI_DOUBLE, destRank, tag, MPI_COMM_WORLD);
    rowIdx += rowCount;
  }

  // Receive Answers
  double answer;
  MPI_Status status;
  int source, tag, receivedRowIdx;
  for (int i = 0; i < rows; i++) {
    MPI_Recv(&answer, 1, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    source = status.MPI_SOURCE;
    tag = status.MPI_TAG;
    receivedRowIdx = tag;

    if (log_level >= 1) logf(1, startTime, MANAGER, "RECV result row=%d <- rank=%d (ans=%.2f)", tag, source, answer);

    c[receivedRowIdx] = answer;
  }
}

void RunWorker(double *b, int myRank, double startTime) {
  // Get number of rows to process
  int rowCount;
  MPI_Recv(&rowCount, 1, MPI_INT, MANAGER, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  
  // Receive rows
  MPI_Status status;
  double *rowsToProcess;
  InitializeMatrix(&rowsToProcess, (size_t)rowCount, (size_t)cols);
  MPI_Recv(rowsToProcess, rowCount * cols, MPI_DOUBLE, MANAGER, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

  int initialRowIdx = status.MPI_TAG;
  if (log_level >= 2) logf(1, startTime, myRank, "RECV row=%d from manager", initialRowIdx);
  
  // Compute and Send result for each row
  for (int rowIdx = 0; rowIdx < rowCount; rowIdx++) {
    double answer = DotProduct(rowsToProcess + rowIdx * cols, b, cols);
    int currentRowIdx = initialRowIdx + rowIdx;

    if (log_level >= 3) logf(1, startTime, myRank, "DONE row=%d compute (sending result)", currentRowIdx);

    MPI_Send(&answer, 1, MPI_DOUBLE, MANAGER, currentRowIdx, MPI_COMM_WORLD);

    if (log_level >= 2) logf(1, startTime, myRank, "SEND result row=%d -> manager", currentRowIdx);
  }
}

// Main Function
int main(int argC, char **argV) {
  MPI_Init(&argC, &argV);
  
  int myRank, numProcs;
  MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
  MPI_Comm_size(MPI_COMM_WORLD, &numProcs);

  if (IsManager(myRank)) ParseArgs(argC, argV);
  
  // Initialize and Assign values to Matrix and Vector
  double *a, *b, *c;
  InitializeVector(&b, (size_t)cols);
  
  if (IsManager(myRank)) {
    InitializeMatrix(&a, (size_t)rows, (size_t)cols);
    InitializeVector(&c, (size_t)rows);
    AssignValues(a, b, (size_t)rows, (size_t)cols);
  }
  
  // Share "common vector" with all workers
  ShareResources(b);

  // Start Computation
  double t0 = MPI_Wtime();
  if (IsManager(myRank)) {
    RunManager(a, c, numProcs, t0);
  } else {
    RunWorker(b, myRank, t0);
  }

  // End Computation
  MPI_Barrier(MPI_COMM_WORLD);
  double t1 = MPI_Wtime();

  // Free Resources
  free(b);

  if (IsManager(myRank)) {
    free(a);
    free(c);
  }

  if (IsManager(myRank)) printf("Manager: rows=%d cols=%d procs=%d time=%.6f seconds\n", rows, cols, numProcs, (t1 - t0));

  MPI_Finalize();
  return 0;
}
