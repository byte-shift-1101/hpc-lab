#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

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

#define min(a,b) ((a)<(b)?(a):(b))

static void *xmalloc(size_t n) {
  void *p = malloc(n);
  if (!p) {
    fprintf(stderr, "malloc failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  return p;
}

/* Small printf helper with rank + time prefix */
static void logF(int enabled, double t0, int rank, const char *fmt, ...) {
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

int encryptRowtag(int rowidx, int numrows, int rows) {
  int rowtag = rowidx + 1;
  int bits = (int)ceil(log2((double)rows));
  rowtag = rowtag << bits;
  rowtag = rowtag | numrows;
  return rowtag;
}

void decryptRowtag(int rowtag, int *rowidx, int *numrows, int rows) {
  int bits = (int)ceil(log2((double)rows));
  *numrows = rowtag & ((1 << bits) - 1);
  *rowidx = (rowtag >> bits) - 1;
}

void SendRows(double *a, double *buffer, int numrows, int sendSize, int *numsent, int rows, int cols, double t0, int myid, int dest, int log_level) {
  // int rowtag = (*numsent) + 1;     /* 1..rows */
  int rowidx = *numsent;      /* 0-based */
  int rowtag = encryptRowtag(rowidx, numrows, rows);

  for (int j = 0; j < numrows * cols; j++)
    buffer[j] = a[(size_t)rowidx * cols + j];

  if (log_level >= 1) logF(1, t0, myid, "SEND row=%d(+%d) -> rank=%d", rowidx + 1, numrows - 1, dest);

  MPI_Send(buffer, numrows * cols, MPI_DOUBLE, dest, rowtag, MPI_COMM_WORLD);
  (*numsent) += numrows;
}

void PrintVector(double *v, int n) {
  fprintf(stdout, "[ ");

  for (int i = 0; i < n; i++) {
    fprintf(stdout, " %.2f ", v[i]);
  }

  fprintf(stdout, "] \n");
  fflush(stdout);
}

void PrintMatrix(double *m, int rows, int cols) {
  for (int i = 0; i < rows; i++) {
    fprintf(stdout, "[ ");
    for (int j = 0; j < cols; j++) {
      fprintf(stdout, " %.2f ", m[(size_t)i * cols + j]);
    }
    fprintf(stdout, "] \n");
  }

  fflush(stdout);
}


int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int myid, numprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

  const int manager = 0;

  int rows = 100, cols = 100;
  int log_level = 1; /* default: manager logs only */
  int sendSize = 2;

  /* Usage:
     ./mv_sched_log rows cols [log_level] [sendSize]
     log_level: 0..3
  */
  if (argc >= 2) { rows = cols = atoi(argv[1]); }
  if (argc >= 3) { rows = atoi(argv[1]); cols = atoi(argv[2]); }
  if (argc >= 4) { log_level = atoi(argv[3]); }
  if (argc >= 5) { sendSize = atoi(argv[4]); }
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
  double *buffer = (double*)xmalloc((size_t)sendSize * (size_t)cols * sizeof(double));
  double *ans = (double*)xmalloc((size_t)sendSize * sizeof(double));

  double *a = NULL;
  double *c = NULL;

  if (myid == manager) {
    a = (double*)xmalloc((size_t)rows * (size_t)cols * sizeof(double));
    c = (double*)xmalloc((size_t)rows * sizeof(double));

    for (int j = 0; j < cols; j++) b[j] = 1.0;
    for (int i = 0; i < rows; i++) {
      for (int j = 0; j < cols; j++) {
        a[(size_t)i * cols + j] = (double)(i + 1);
      }
    }

    // PrintMatrix(a, rows, cols);
    // PrintVector(b, cols);
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

  int totalsends = (int)ceil((double)rows / sendSize);
  if (myid == manager) {
    int numsent = 0;

    /* Send initial work to as many workers as possible */
    int initial = min(numprocs - 1, totalsends);

    for (int dest = 1; dest <= initial; dest++) {
      SendRows(a, buffer, min(sendSize, rows - numsent), sendSize, &numsent, rows, cols, t0, myid, dest, log_level);
    }

    /* Receive each result; send next row to the sender, else terminate */
    int rowsReceived = 0;
    while (rowsReceived < rows) {
      MPI_Status status;

      MPI_Recv(ans, sendSize, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG,
               MPI_COMM_WORLD, &status);

      int sender = status.MPI_SOURCE;
      int rowtag = status.MPI_TAG;       /* row number */
      int rowidx, numrows;
      decryptRowtag(rowtag, &rowidx, &numrows, rows);

      if (log_level >= 1) logF(1, t0, myid, "RECV result row=%d(+%d) <- rank=%d (ans=%.2f, %.2f)",
                               rowidx + 1, numrows - 1, sender, ans[0], ans[1]);

      if (rowidx >= 0 && rowidx < rows) {
        for (int i = 0; i < numrows; i++) {
          c[rowidx + i] = ans[i];
        }
        rowsReceived += numrows;
      }

      if (numsent < rows) {
        SendRows(a, buffer, min(sendSize, rows - numsent), sendSize, &numsent, rows, cols, t0, myid, sender, log_level);
      } else {
        if (log_level >= 1) logF(1, t0, myid, "TERM -> rank=%d", sender);
        MPI_Send(NULL, 0, MPI_DOUBLE, sender, 0, MPI_COMM_WORLD);
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
    if (myid <= totalsends) {
      while (1) {
        MPI_Status status;

        MPI_Recv(buffer, sendSize * cols, MPI_DOUBLE, manager, MPI_ANY_TAG,
                 MPI_COMM_WORLD, &status);

        int rowtag = status.MPI_TAG;
        if (rowtag == 0) {
          if (log_level >= 2) logF(1, t0, myid, "RECV TERM (tag=0) -> exiting");
          break;
        }

        int rowidx, numrows;
        decryptRowtag(rowtag, &rowidx, &numrows, rows);

        if (log_level >= 2) logF(1, t0, myid, "RECV row=%d(+%d) from manager", rowidx + 1, numrows - 1);

        // PrintMatrix(buffer, numrows, cols);

        /* Dot product with b */
        for (int i = 0; i < numrows; i++){
          ans[i] = 0.0;
          for (int j = 0; j < cols; j++) {
            ans[i] += buffer[i * cols + j] * b[j];
          }
        }

        if (log_level >= 3) logF(1, t0, myid, "DONE row=%d compute (sending result=(%.2f, %.2f))", rowidx + 1, ans[0], ans[1]);

        MPI_Send(ans, numrows, MPI_DOUBLE, manager, rowtag, MPI_COMM_WORLD);

        if (log_level >= 2) logF(1, t0, myid, "SEND result row=%d -> manager", rowidx + 1);
      }
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }

  free(b);
  free(buffer);

  MPI_Finalize();
  return 0;
}
