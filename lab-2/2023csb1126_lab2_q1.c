#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/*
  Self-scheduling matrix-vector multiply (Using MPI style) + logging:
  - manager rank 0 sends one row at a time to workers
  - tag carries row number (1..rows)
  - tag 0 reserved for termination
  - termination message length is 0
  - b is broadcast to everyone
  - timing measured with MPI_Wtime()

  Logging:
  - timestamps are relative to a common t0 (after barrier)
  - log_level:
      0 = no logs
      1 = manager events only
      2 = manager + worker recv/send events
      3 = verbose (includes compute done events)
*/

static void *xmalloc(size_t n) {
  void *p = malloc(n);
  if (!p) {
    fprintf(stderr, "malloc failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  return p;
}

/* Small printf helper with rank + time prefix */
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

void AssignRows(int *numrows, int *cumulativeRows, int numProcs, int rows) {
  int extra = rows % (numProcs - 1);
  int base = rows / (numProcs - 1);

  cumulativeRows[0] = 0;
  for (int i = 0; i < numProcs - 1; i++) {
    numrows[i] = base;
    if (extra) {
      numrows[i]++;
      extra--;
    }

    cumulativeRows[i + 1] = cumulativeRows[i] + numrows[i];
  }
}

void SendRow(double *a, double *buffer, int rowidx, int *numsent, int cols, double t0, int myid, int dest, int log_level) {
  int rowtag = rowidx + 1;     /* 1..rows */

  for (int j = 0; j < cols; j++)
    buffer[j] = a[(size_t)rowidx * cols + j];

  if (log_level >= 1) logf(1, t0, myid, "SEND row=%d -> rank=%d", rowtag, dest);

  MPI_Send(buffer, cols, MPI_DOUBLE, dest, rowtag, MPI_COMM_WORLD);
  (*numsent)++;
}

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int myid, numprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

  const int manager = 0;

  int rows = 100, cols = 100;
  int log_level = 1; /* default: manager logs only */

  /* Usage:
     ./mv_sched_log rows cols [log_level]
     log_level: 0..3
  */
  if (argc >= 3) { rows = atoi(argv[1]); cols = atoi(argv[2]); }
  if (argc >= 4) { log_level = atoi(argv[3]); }
  if (log_level < 0) log_level = 0;
  if (log_level > 3) log_level = 3;

  if (rows <= 0 || cols <= 0) {
    if (myid == manager) fprintf(stderr, "Usage: %s rows cols [log_level]\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  if (numprocs < 2) {
    if (myid == manager) fprintf(stderr, "Run with at least 2 processes.\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  double *b = (double*)xmalloc((size_t)cols * sizeof(double));
  double *buffer = (double*)xmalloc((size_t)cols * sizeof(double));

  double *a = NULL;
  double *c = NULL;

  if (myid == manager) {
    a = (double*)xmalloc((size_t)rows * (size_t)cols * sizeof(double));
    c = (double*)xmalloc((size_t)rows * sizeof(double));

    for (int j = 0; j < cols; j++) b[j] = 1.0;
    for (int i = 0; i < rows; i++)
      for (int j = 0; j < cols; j++)
        a[(size_t)i * cols + j] = (double)(i + 1);
  }

  /* Broadcast b to all ranks */
  MPI_Bcast(b, cols, MPI_DOUBLE, manager, MPI_COMM_WORLD);

  /* Common synchronized start time for logging + timing */
  MPI_Barrier(MPI_COMM_WORLD);
  double t0 = MPI_Wtime();

  /* Optional: print one header from manager */
  if (myid == manager && log_level >= 1) {
    fprintf(stdout, "=== mv_sched_log: rows=%d cols=%d procs=%d log_level=%d ===\n",
            rows, cols, numprocs, log_level);
    fflush(stdout);
  }

  if (myid == manager) {
    int numsent = 0;

    /* Send initial work to as many workers as possible */
    int initial = (numprocs - 1 < rows) ? (numprocs - 1) : rows;

    int *rowsAssigned = (int*)malloc((numprocs - 1) * sizeof(int));
    int *rowsProcessed = (int*)calloc(numprocs - 1, sizeof(int));
    int *cumulativeRows = (int*)malloc(numprocs * sizeof(int));
    AssignRows(rowsAssigned, cumulativeRows, numprocs, rows);

    for (int dest = 1; dest <= numprocs - 1; dest++)
      MPI_Send(&rowsAssigned[dest - 1], 1, MPI_INT, dest, 0, MPI_COMM_WORLD);

    for (int dest = 1; dest <= initial; dest++)
      SendRow(a, buffer, rowsProcessed[dest - 1] + cumulativeRows[dest - 1], &numsent, cols, t0, myid, dest, log_level);

    /* Receive each result; send next row to the sender, else terminate */
    for (int done = 0; done < rows; done++) {
      double ans;
      MPI_Status status;

      MPI_Recv(&ans, 1, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

      int sender = status.MPI_SOURCE;
      int rowtag = status.MPI_TAG;       /* row number */
      int rowidx = rowtag - 1;

      if (log_level >= 1) logf(1, t0, myid, "RECV result row=%d <- rank=%d (ans=%.2f)",
                               rowtag, sender, ans);

      if (rowtag >= 1 && rowtag <= rows)
        c[rowidx] = ans;

      rowsProcessed[sender - 1]++;
      if (rowsProcessed[sender - 1] < rowsAssigned[sender - 1]) {
        SendRow(a, buffer, rowsProcessed[sender - 1] + cumulativeRows[sender - 1], &numsent, cols, t0, myid, sender, log_level);
      }
    }

    /* Ensure all workers reach the end (mainly for clean logging) */
    MPI_Barrier(MPI_COMM_WORLD);
    double t1 = MPI_Wtime();

    /* Print timing */
    printf("Manager: rows=%d cols=%d procs=%d time=%.6f seconds\n",
           rows, cols, numprocs, (t1 - t0));

    if (rows <= 10) {
      for (int i = 0; i < rows; i++)
        printf("c[%d] = %.2f\n", i, c[i]);
    }

    free(a);
    free(c);

  } else {
    int rowsAssigned;
    MPI_Recv(&rowsAssigned, 1, MPI_INT, manager, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (log_level >= 2) logf(1, t0, myid, "RECV assigned_rows=%d from manager", rowsAssigned);

    if (myid <= rows) {
      for (int i = 0; i < rowsAssigned; i++) {
        MPI_Status status;

        MPI_Recv(buffer, cols, MPI_DOUBLE, manager, MPI_ANY_TAG, MPI_COMM_WORLD, &status);       
        
        int rowtag = status.MPI_TAG;
        if (log_level >= 2) logf(1, t0, myid, "RECV row=%d from manager", rowtag);
        
        /* Dot product with b */
        double ans = 0.0;
        for (int j = 0; j < cols; j++)
          ans += buffer[j] * b[j];
        
        if (log_level >= 3) logf(1, t0, myid, "DONE row=%d compute (sending result)", rowtag);

        MPI_Send(&ans, 1, MPI_DOUBLE, manager, rowtag, MPI_COMM_WORLD);

        if (log_level >= 2) logf(1, t0, myid, "SEND result row=%d -> manager", rowtag);
      }
    }
    if (log_level >= 2) logf(1, t0, myid, "RECV TERM (tag=0) -> exiting");
    
    MPI_Barrier(MPI_COMM_WORLD);
  }

  free(b);
  free(buffer);

  MPI_Finalize();
  return 0;
}
