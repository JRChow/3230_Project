/************************************************************
 * Filename:part1b-mandelbrot.c
 * Student name: ZHOU Jingran
 * Student no.: 3035232468
 * Date: Nov 1, 2017
 * version: 1.1
 * Development platform: Ubuntu 16.04
 * Compilation: gcc part1b-mandelbrot.c -o 1b-mandel -l SDL2 -l m
 **************************************************************/
#define MIN(a, b) (((a) < (b)) ? (a) : (b)) // User-defined min function

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "Mandel.h"
#include "draw.h"
#include <assert.h>
#include <sys/resource.h>
#include <signal.h>
#include <string.h>

// Global file descriptors for the handler.
int task_fd[2];
int data_fd[2];

// Find index of array given value.
int find_index(pid_t a[], int num_elements, pid_t value)
{
  int i;

  for (i = 0; i < num_elements; i++)
  {
    if (a[i] == value)
    {
      return i; /* it was found */
    }
  }
  return -1;    /* if it was not found */
}

// Structure of a task.
typedef struct task {
  int start_row;   // Start at which row.
  int num_of_rows; // How many rows.
} TASK;

// The result message of one row of pixels.
typedef struct message {
  int   row_index;             // Which row.
  pid_t child_pid;             // Child ID.
  float row_data[IMAGE_WIDTH]; // Actual pixel values.
} MSG;

// Computes data and return the message.
MSG* computeRow(int row_index)
{
  assert(row_index >= 0);
  assert(row_index < IMAGE_HEIGHT);

  MSG *msg = (MSG *)malloc(sizeof *msg); // Allocate memory.

  msg->row_index = row_index;            // Choose which row to compute.

  // Compute one row of data.
  for (int i = 0; i < IMAGE_WIDTH; i++) {
    msg->row_data[i] = Mandelbrot(i, row_index);
  }

  msg->child_pid = -1;

  return msg;
}

// Process a task.
void processTask(TASK *tsk) {
  assert(tsk != NULL);
  assert(tsk->start_row >= 0);
  assert(tsk->start_row < IMAGE_HEIGHT);
  assert(tsk->start_row + tsk->num_of_rows <= IMAGE_HEIGHT);

  // Process task row by row.
  for (int y = tsk->start_row; y < (tsk->start_row + tsk->num_of_rows); y++) {
    MSG *curMsg = computeRow(y);

    if (y == (tsk->start_row + tsk->num_of_rows - 1)) curMsg->child_pid = getpid();
    write(data_fd[1], curMsg, sizeof(MSG));
    free(curMsg); // Free the buffer.
  }
}

// Signal handler for SIGUSR1.
// This means that a new task has been placed in the Task pipe.
void USRhandler() {
  // Get a task from the task pipe to work on.
  TASK *tskBuf = (TASK *)malloc(sizeof(TASK));

  read(task_fd[0], tskBuf, sizeof(TASK));

  // Record the computation start time and end time.
  struct timespec start_time, end_time;

  // Get computation start time.
  clock_gettime(CLOCK_MONOTONIC, &start_time);

  fprintf(stderr, "Child (%d): Start computation...\n", getpid());

  // Process the task.
  processTask(tskBuf);

  // Get computation start time.
  clock_gettime(CLOCK_MONOTONIC, &end_time);

  // Display computation time.
  double computeTime =
    (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0 +
    (end_time.tv_sec - start_time.tv_sec) * 1000.0;
  fprintf(stderr,
          "Child (%d): ...completed. Elapsed time= %f ms\n", getpid(),
          computeTime);
  free(tskBuf);
}

// Signal handler for SIGINT
void INThandler() {
  // "Time to die."
  fprintf(stderr, "Process %d interruped by INT. Bye.\n", getpid());
  close(data_fd[1]); // No need to write data.
  close(task_fd[0]); // No need to read task.
  exit(0);
}

// Create a task depending on nextTaskRow. nextTaskRow is updated.
TASK* createTask(int *nextTaskRow, int rowPerTask) {
  assert(*nextTaskRow >= 0);
  assert(*nextTaskRow < IMAGE_HEIGHT);

  TASK *tskBuf = (TASK *)malloc(sizeof(*tskBuf));
  tskBuf->start_row   = *nextTaskRow;
  tskBuf->num_of_rows = MIN(rowPerTask, IMAGE_HEIGHT - *nextTaskRow);

  *nextTaskRow += tskBuf->num_of_rows;

  return tskBuf;
}

// Main function
int main(int argc, char *args[])
{
  // Record the process start time and end time.
  struct timespec proc_start_time, proc_end_time;

  // Get process start time.
  clock_gettime(CLOCK_MONOTONIC, &proc_start_time);

  // First input argument (number of worker processes to be created).
  int workerCount = 0;

  // Second input argument (number of rows in a task).
  int rowPerTask;

  // The start row of the next task.
  int nextTaskRow = 0;

  // Validate the number of arguments
  if (argc != 3)
  {
    fprintf(stderr,
            "Usage: ./part1b-mandelbrot [numOfChildProc] [numRowPerTask]\n");
    exit(1);
  }

  // Validate and obtain the number of worker processes.
  assert(args != NULL);

  if (sscanf(args[1], "%i", &workerCount) != 1) {
    fprintf(stderr, "Child count is NOT an integer!\n");
    exit(1);
  }

  // Validate and obtain the number of rows in a task.
  if (sscanf(args[2], "%i", &rowPerTask) != 1) {
    fprintf(stderr, "Row per task is NOT an integer!\n");
    exit(1);
  }

  // Assert input args assumptions.
  assert(workerCount >= 1);
  assert(workerCount <= 10);
  assert(rowPerTask >= 1);
  assert(rowPerTask <= 50);

  // Store the 2D image as a linear array of pixels (in row-major format).
  float *pixels = (float *)malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);

  if (pixels == NULL) {
    printf("Out of memory!!\n");
    exit(1);
  }

  // Install handlers.
  signal(SIGUSR1, USRhandler);
  signal(SIGINT,  INThandler);

  // Create the Task pipe.
  if (pipe(task_fd) == -1) {
    fprintf(stderr, "Can't create task pipe!\n");
    exit(1);
  }

  // Create the Data pipe.
  if (pipe(data_fd) == -1) {
    fprintf(stderr, "Can't create data pipe!\n");
    exit(1);
  }

  pid_t cPid[workerCount];      // Array of pid's of worker process
  int   taskCount[workerCount]; // Array recording each worker's task
  // achieved.

  // Create workers.
  for (int w = 0; w < workerCount; w++) {
    taskCount[w] = 0;

    if ((cPid[w] = fork()) < 0) { // fork() error.
      perror("fork() error");
      exit(1);
    } else if (cPid[w] == 0) {
      // ******************************* Worker *******************************
      fprintf(stderr, "Child (%d): Start up. Wait for task.\n", getpid());
      close(task_fd[1]); // Worker won't WRITE to task pipe.
      close(data_fd[0]); // Worker won't READ from data pipe.

      // Wait for SIGUSR1 or SIGINT
      while (1) pause();
    }
  }

  // ################################## Boss ##################################

  fprintf(stderr, "Start collecting image lines.\n");

  close(task_fd[0]); // Boss won't read task.
  close(data_fd[1]); // Boss won't write data.

  // Distribute a task to each worker. Send a SIGUSR1 to them.
  for (int w = 0; w < workerCount; w++) {
    TASK *newTask = createTask(&nextTaskRow, rowPerTask);

    if (write(task_fd[1], newTask, sizeof(TASK)) > 0) kill(cPid[w], SIGUSR1);
    free(newTask);
  }

  int msgCount = 0; // How many messages are read and stored.

  MSG *msgBuf = (MSG *)malloc(sizeof(*msgBuf));

  // While not all results are returned.
  while (msgCount < IMAGE_HEIGHT) {
    // Read a message
    read(data_fd[0], msgBuf, sizeof(MSG));

    msgCount++;

    // Store the data to pixels
    memcpy(&pixels[msgBuf->row_index * IMAGE_WIDTH],
           msgBuf->row_data,
           IMAGE_WIDTH * sizeof(float));

    if (msgBuf->child_pid != -1) { // Last row. This worker is idle.
      // Increase the work accomplished by this worker.
      taskCount[find_index(cPid, workerCount, msgBuf->child_pid)] += 1;

      if (nextTaskRow < IMAGE_HEIGHT) { // If still unassigned task.
        // Put the task in Task pipe.
        TASK *newTask = createTask(&nextTaskRow, rowPerTask);
        int   nbyte   = write(task_fd[1], newTask, sizeof(TASK));

        // Inform the idle worker.
        if (nbyte > 0) kill(msgBuf->child_pid, SIGUSR1);
        free(newTask);
      }
    }
  } // While loop

        free(msgBuf);

  close(task_fd[1]); // Boss finishes writing task.
  close(data_fd[0]); // Boss finishes reading data.

  // Terminate all workers via SIGINT to all.
  for (int w = 0; w < workerCount; w++) kill(cPid[w], SIGINT);

  // Parent waits for all children to terminate.
  while (wait(NULL) > 0) {}

  for (int i = 0; i < workerCount; i++) {
    fprintf(stderr,
            "Child %d terminated and completed %d tasks.\n",
            cPid[i],
            taskCount[i]);
  }

    fprintf(stderr, "All children completed.\n");

  // ---------------------------------------------------------------------

  struct rusage workerren_usage, self_usage;
  getrusage(RUSAGE_CHILDREN, &workerren_usage);
  getrusage(RUSAGE_SELF,     &self_usage);
  fprintf(
    stderr,
    "Total time spent by all children in user mode = %f ms\n",
    workerren_usage.ru_utime.tv_usec / 1000000.0 +
    workerren_usage.ru_utime.tv_sec * 1000.0);
  fprintf(
    stderr,
    "Total time spent by all children in system mode = %f ms\n",
    workerren_usage.ru_stime.tv_usec / 1000000.0 +
    workerren_usage.ru_stime.tv_sec * 1000.0);
  fprintf(
    stderr,
    "Total time spent by the parent in user mode = %f ms\n",
    self_usage.ru_utime.tv_usec / 1000000.0 + self_usage.ru_utime.tv_sec *
    1000.0);
  fprintf(
    stderr,
    "Total time spent by the parent in system mode = %f ms\n",
    self_usage.ru_stime.tv_usec / 1000000.0 + self_usage.ru_stime.tv_sec *
    1000.0);

  // Get process end time.
  clock_gettime(CLOCK_MONOTONIC,
                &proc_end_time);

  // Calculate and display the total elapsed time.
  double elapsedTime =
    (proc_end_time.tv_nsec - proc_start_time.tv_nsec) / 1000000.0 +
    (proc_end_time.tv_sec - proc_start_time.tv_sec) * 1000.0;
  fprintf(stderr,
          "Total elapsed time measured by parent process = %f ms\n",
          elapsedTime);

  printf("Draw the image\n");

  // Draw the image by using the SDL2 library
  DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 5000);

  free(pixels); // Free the pixels.

  return 0;
}
