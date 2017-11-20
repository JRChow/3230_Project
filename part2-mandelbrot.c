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

#include "Mandel.h"
#include "draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <sys/resource.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

// -----------------------------------------------------------------------------

// Structure of a task.
typedef struct task {
  int start_row;   // Start at which row.
  int num_of_rows; // How many rows.
} TASK;

// -----------------------------------------------------------------------------

// The pixels of the graph.
float *pixels;

// The task pool.
TASK **taskPool;
int    buffCount;  // Third arg (number of buffers in the pool).

int fillInd   = 0; // Pointer for filling the task pool.
int useInd    = 0; // Pointer for using the task pool.
int taskCount = 0; // How many tasks are actually in the pool?

// Lock for the task pool.
pthread_mutex_t poolLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  empty    = PTHREAD_COND_INITIALIZER; // FIXME CV for empty.
pthread_cond_t  fill     = PTHREAD_COND_INITIALIZER; // FIXME CV for fill.

int canFinish = 0;

// -----------------------------------------------------------------------------

// A wrapper function for creating threads.
void Pthread_create(pthread_t            *thread,
                    const pthread_attr_t *attr,
                    void *(*start_routine)(void *),
                    void                 *arg) {
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

// Process a task.
float* processTask(TASK *tsk) {
  assert(tsk != NULL);
  assert(tsk->start_row >= 0);
  assert(tsk->start_row < IMAGE_HEIGHT);
  assert(tsk->start_row + tsk->num_of_rows <= IMAGE_HEIGHT);

  float *result = (float *)malloc(sizeof(float) * tsk->num_of_rows * IMAGE_WIDTH);

  // Process task row by row.
  for (int y = tsk->start_row; y < (tsk->start_row + tsk->num_of_rows); y++) {
    for (int x = 0; x < IMAGE_WIDTH; x++) {
      int index = (y - tsk->start_row) * IMAGE_WIDTH + x; // Index into result.
      result[index] = Mandelbrot(x, y);
    }
  }

  return result;
}

// Create a task depending on nextTaskRow. nextTaskRow is updated.
TASK* createTask(int *nextTaskRow, int rowPerTask) {
  assert(*nextTaskRow >= 0);
  assert(*nextTaskRow < IMAGE_HEIGHT);

  TASK *tskBuf = (TASK *)malloc(sizeof(*tskBuf));
  tskBuf->start_row   = *nextTaskRow;
  tskBuf->num_of_rows = MIN(rowPerTask, IMAGE_HEIGHT - *nextTaskRow);

  *nextTaskRow += tskBuf->num_of_rows; // Update the next task row.

  return tskBuf;
}

// Write result to pixels.
void writeResult(float *result, int start_row, int num_of_rows) {
  assert(pixels != NULL);
  assert(result != NULL);

  int base = start_row * IMAGE_WIDTH;

  for (int i = 0; i < num_of_rows * IMAGE_WIDTH; i++) {
    pixels[base + i] = result[i];
  }
}

// Read input arguments.
void readArgs(int   argc,
              char *args[],
              int  *workerCount,
              int  *rowPerTask,
              int  *buffCount)
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
  if (sscanf(args[3], "%i", buffCount) != 1) {
    fprintf(stderr, "Buffer count is NOT an integer!\n");
    exit(1);
  }

  // Assert input args assumptions.
  assert(*workerCount >= 1);
  assert(*workerCount <= 16);
  assert(*rowPerTask >= 1);
  assert(*rowPerTask <= 50);
  assert(*buffCount >= 1);
  assert(*buffCount <= 10);
}

// Returns 1 if still have tasks. 0 if no task left.
int hasTask(int nextTaskRow) {
  return nextTaskRow < IMAGE_HEIGHT;
}

// Put newTask into the task pool.
void putTask(TASK *newTask) {
  assert(newTask != NULL);
  taskPool[fillInd] = newTask;
  fillInd           = (fillInd + 1) % buffCount;
  taskCount++;
}

// Get task from the task pool.
TASK* getTask() {
  assert(taskCount > 0);
  TASK *temp = taskPool[useInd];

  useInd = (useInd + 1) % buffCount;
  taskCount--;
  return temp;
}

// The work of a worker.
void* work(void *arg) {
  // **************************** Consumer ****************************

  while (1) {                              // TODO: While not terminated
    Pthread_mutex_lock(&poolLock);         // ### Lock the pool ###.

    while (taskCount == 0) {               // While task pool is empty
      if (canFinish) {
        Pthread_mutex_unlock(&poolLock);       // Unlock before death.
        return 0;
      }
      pthread_cond_wait(&fill, &poolLock); // Wait until it becomes filled.
    }
    TASK *task = getTask();                // Get task from the pool.
    Pthread_cond_signal(&empty);           // A new buffer is available.
    Pthread_mutex_unlock(&poolLock);       // ### Unlock the pool ###
    float *result = processTask(task);     // Process task.
    // TODO: display computation time.
    writeResult(result, task->start_row, task->num_of_rows);
  }
}

// Main function
int main(int argc, char *args[])
{
  // Record the process start time and end time.
  // struct timespec proc_start_time, proc_end_time;

  // Get process start time.
  // clock_gettime(CLOCK_MONOTONIC, &proc_start_time);

  // First arg (number of worker processes to be created).
  int workerCount;

  // Second arg (number of rows in a task).
  int rowPerTask;

  // The start row of the next task.
  int nextTaskRow = 0;

  // Read input arguments.
  readArgs(argc, args, &workerCount, &rowPerTask, &buffCount);

  // Create the task pool.
  taskPool = malloc(buffCount * sizeof(TASK));

  // Store the 2D image as a linear array of pixels (in row-major format).
  pixels = (float *)malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);

  if (pixels == NULL) {
    printf("Out of memory!!\n");
    exit(1);
  }

  // An array of worker threads.
  pthread_t workers[workerCount];

  // Create worker threads.
  for (int i = 0; i < workerCount; i++) {
    Pthread_create(&(workers[i]), NULL, &work, NULL);
  }

  // ---------------------------------------------------------------------
  // ******************************* Producer *******************************

  while (hasTask(nextTaskRow)) {
    Pthread_mutex_lock(&poolLock); // # Lock the pool.

    // While task pool is full
    while (taskCount == buffCount) {
      Pthread_cond_wait(&empty, &poolLock); // Wait until it's not full
    }

    // Create and put a task in the next unused buffer.
    putTask(createTask(&nextTaskRow, rowPerTask));

    // Signal any waiting worker that a new task has arrived.
    Pthread_cond_signal(&fill);

    Pthread_mutex_unlock(&poolLock); // # Unlock the pool.
  }

  // Inform all workers that no more tasks will be assigned.
  // And the workers should terminate after finishing all pending tasks.
  Pthread_mutex_lock(&poolLock); // # Lock the pool.
  canFinish = 1;
  Pthread_mutex_unlock(&poolLock); // # Unlock the pool.

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

  // Draw the image by using the SDL2 library
  DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 5000);

  //
  // free(pixels); // Free the pixels.
  //
  return 0;
}
