/**********************************************************************
* Filename:part2-mandelbrot.c
* Student name: Zhou Jingran
* Student no.: 3035232468
* Date: Nov 1, 2017
* version: 1.1
* Development platform: Ubuntu 16.04
* Compilation: gcc part2-mandelbrot.c -o 2-mandel -l SDL2 -l m -pthread
**********************************************************************/
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
#include <pthread.h>

// -----------------------------------------------------------------------------

// A wrapper function for creating threads.
void Pthread_create(pthread_t            *thread,
                    const pthread_attr_t *attr,
                    void *(*start_routine)(
                      void *),
                    void *arg) {
  int rc = pthread_create(thread, attr, start_routine, arg);

  assert(rc == 0);
}

// A wrapper function for joining threads.
void Pthread_join(pthread_t thread, void **retval) {
  int rc = pthread_join(thread, retval);

  assert(rc == 0);
}

// A wrapper function for locking the mutex lock.
void Pthread_mutex_lock(pthread_mutex_t *mutex) {
  int rc = pthread_mutex_lock(mutex);

  assert(rc == 0);
}

// A wrapper function for unlocking the mutex lock.
void Pthread_mutex_unlock(pthread_mutex_t *mutex) {
  int rc = pthread_mutex_unlock(mutex);

  assert(rc == 0);
}

// A wrapper for waiting a conditional variable.
void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  int rc = pthread_cond_wait(cond, mutex);

  assert(rc == 0);
}

// A wrapper for signaling on condition.
void Pthread_cond_signal(pthread_cond_t *cond) {
  int rc = pthread_cond_signal(cond);

  assert(rc == 0);
}

// -----------------------------------------------------------------------------

// Structure of a task.
typedef struct task {
  int start_row;   // Start at which row.
  int num_of_rows; // How many rows.
} TASK;

// Process a task.
void processTask(TASK *tsk) {
  assert(tsk != NULL);
  assert(tsk->start_row >= 0);
  assert(tsk->start_row < IMAGE_HEIGHT);
  assert(tsk->start_row + tsk->num_of_rows <= IMAGE_HEIGHT);

  // Process task row by row.
  // TODO
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

// Read input arguments.
void readArgs(int   argc,
              char *args[],
              int  *workerCount,
              int  *rowPerTask,
              int  *bufCount)
{
  // Validate the number of arguments
  if (argc != 4)
  {
    fprintf(
      stderr,
      "Usage: ./part2-mandelbrot [number of workers] [number of rows in a task] [number of buffers]\n");
    exit(1);
  }

  // Validate and obtain the number of worker processes.
  assert(args != NULL);

  if (sscanf(args[1], "%i", workerCount) != 1) {
    fprintf(stderr, "Child count is NOT an integer!\n");
    exit(1);
  }

  // Validate and obtain the number of rows in a task.
  if (sscanf(args[2], "%i", rowPerTask) != 1) {
    fprintf(stderr, "Row per task is NOT an integer!\n");
    exit(1);
  }

  // Validate and obtain the number of rows in a task.
  if (sscanf(args[3], "%i", bufCount) != 1) {
    fprintf(stderr, "Buffer count is NOT an integer!\n");
    exit(1);
  }

  // Assert input args assumptions.
  assert(*workerCount >= 1);
  assert(*workerCount <= 16);
  assert(*rowPerTask >= 1);
  assert(*rowPerTask <= 50);
  assert(*bufCount >= 1);
  assert(*bufCount <= 10);
}

// Returns 1 if still have tasks. 0 if no task left.
int hasTask(int nextTaskRow) {
  return nextTaskRow < IMAGE_HEIGHT;
}

// Put newTask into the task pool.
void putTask(TASK *newTask,
             TASK *taskPool[],
             int   bufCount,
             int  *taskCount,
             int  *fillInd) {
  assert(newTask != NULL);
  taskPool[*fillInd] = newTask;
  *fillInd           = (*fillInd + 1) % bufCount;
  (*taskCount)++;
}

// Get task from the task pool.
TASK* getTask(TASK *taskPool[], int bufCount, int *taskCount, int *useInd) {
  TASK *temp = taskPool[*useInd];

  *useInd = (*useInd + 1) % bufCount;
  (*taskCount)--;
  return temp;
}

// The work of a worker.
void* work(void *arg) {
  fprintf(stderr, "working!\n");

  // while (1) {                            // TODO: While not terminated
  Pthread_mutex_lock(&poolLock);

  while (taskCount == 0) {            // While task pool is empty
    pthread_cond_wait(&fill, &mutex); // Wait until it becomes filled.
  }
  TASK *task = getTask(taskPool, bufCount, &taskCount, &useInd);
  Pthread_cond_signal(&empty);
  Pthread_mutex_unlock(&poolLock);

  // }
}

// Main function
int main(int argc, char *args[])
{
  // Record the process start time and end time.
  // struct timespec proc_start_time, proc_end_time;

  // Get process start time.
  // clock_gettime(CLOCK_MONOTONIC, &proc_start_time);

  // First input argument (number of worker processes to be created).
  int workerCount = 0;

  // Second input argument (number of rows in a task).
  int rowPerTask;

  // Third input argument (number of buffers).
  int bufCount;

  // The start row of the next task.
  int nextTaskRow = 0;

  // Read input arguments.
  readArgs(argc, args, &workerCount, &rowPerTask, &bufCount);

  // Store the 2D image as a linear array of pixels (in row-major format).
  float *pixels = (float *)malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);

  if (pixels == NULL) {
    printf("Out of memory!!\n");
    exit(1);
  }

  // An array of worker threads.
  pthread_t workers[workerCount];

  // Create task pool.
  TASK *taskPool[bufCount];

  int fillInd   = 0; // Pointer for filling the task pool.
  int useInd    = 0; // Pointer for using the task pool.
  int taskCount = 0; // How many tasks are there in the pool?

  // Lock for the task pool.
  pthread_mutex_t poolLock = PTHREAD_MUTEX_INITIALIZER;

  // TODO: An array of return values;

  pthread_cond_t empty, fill; // CV for empty and full.

  // Create worker threads.
  for (int i = 0; i < workerCount; i++) {
    Pthread_create(&(workers[i]), NULL, &work, NULL);
  }

  // ---------------------------------------------------------------------
  // Producer

  while (hasTask(nextTaskRow)) {
    Pthread_mutex_lock(&poolLock); // Lock the pool.

    // While task pool is full
    while (taskCount == bufCount) {
      Pthread_cond_wait(&empty, &poolLock); // Wait until it's not full
    }

    // Create and put a task in the next unused buffer.
    putTask(createTask(&nextTaskRow, rowPerTask),
            taskPool,
            bufCount,
            &taskCount,
            &fillInd);

    // Signal any waiting worker that a new task has arrived.
    Pthread_cond_signal(&fill);

    // Unlock the pool.
    Pthread_mutex_unlock(&poolLock);
  }

  // Inform all workers that no more tasks will be assigned.
  // And the workers should terminate after finishing all pending tasks.
  // TODO

  // ---------------------------------------------------------------------

  // Wait for all worker threads
  for (int i = 0; i < workerCount; i++) {
    Pthread_join(workers[i], NULL); // TODO: change NULL to a ret val struct.
  }

  // ---------------------------------------------------------------------

  // struct rusage workerren_usage, self_usage;
  //
  // // getrusage(RUSAGE_CHILDREN, &workerren_usage);
  // getrusage(RUSAGE_SELF, &self_usage);
  // fprintf(
  //   stderr,
  //   "Total time spent by all children in user mode = %f ms\n",
  //   workerren_usage.ru_utime.tv_usec / 1000000.0 +
  //   workerren_usage.ru_utime.tv_sec * 1000.0);
  // fprintf(
  //   stderr,
  //   "Total time spent by all children in system mode = %f ms\n",
  //   workerren_usage.ru_stime.tv_usec / 1000000.0 +
  //   workerren_usage.ru_stime.tv_sec * 1000.0);
  // fprintf(
  //   stderr,
  //   "Total time spent by the parent in user mode = %f ms\n",
  //   self_usage.ru_utime.tv_usec / 1000000.0 + self_usage.ru_utime.tv_sec *
  //   1000.0);
  // fprintf(
  //   stderr,
  //   "Total time spent by the parent in system mode = %f ms\n",
  //   self_usage.ru_stime.tv_usec / 1000000.0 + self_usage.ru_stime.tv_sec *
  //   1000.0);
  //
  // // Get process end time.
  // clock_gettime(CLOCK_MONOTONIC,
  //               &proc_end_time);
  //
  // // Calculate and display the total elapsed time.
  // double elapsedTime =
  //   (proc_end_time.tv_nsec - proc_start_time.tv_nsec) / 1000000.0 +
  //   (proc_end_time.tv_sec - proc_start_time.tv_sec) * 1000.0;
  // fprintf(stderr,
  //         "Total elapsed time measured by parent process = %f ms\n",
  //         elapsedTime);
  //
  // printf("Draw the image\n");
  //
  // // Draw the image by using the SDL2 library
  // DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 5000);
  //
  // free(pixels); // Free the pixels.
  //
  return 0;
}
