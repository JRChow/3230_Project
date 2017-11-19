/************************************************************
 * Filename: part1a-mandelbrot.c
 * Student name: ZHOU Jingran
 * Student no.: 3035232468
 * Date: Nov 1, 2017
 * version: 1.1
 * Development platform: Ubuntu 16.04
 * Compilation: gcc part1a-mandelbrot.c -o 1a-mandel -l SDL2 -l m
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

// The result message of one row of pixels.
typedef struct message {
  int   row_index;             // Which row.
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

  return msg;
}

// Compute a block of rows and write to the pipe.
void childCompute(int childInd, int childCount, int fd[])
{
  assert(childCount > 0);
  assert(childCount <= IMAGE_HEIGHT);
  assert(childInd >= 0);
  assert(childInd < childCount);
  assert(fd != NULL);

  // Helper variable.
  int scale = IMAGE_HEIGHT / childCount;

  // End on which row (exclusive).
  int endRowInd = MIN((childInd + 1) * scale, IMAGE_HEIGHT);

  // Store the start and end times of the child.
  struct timespec child_start_time, child_end_time;

  printf("Child (%d): Start computation...\n", getpid());

  // Get child start time.
  clock_gettime(CLOCK_MONOTONIC, &child_start_time);

  // Calculate all rows and write each row to pipe.
  for (int curRowInd = childInd * scale; curRowInd < endRowInd; curRowInd++) {
    MSG *curMsg = computeRow(curRowInd);
    write(fd[1], curMsg, sizeof(MSG));
    free(curMsg); // Free the buffer.
  }

  // Get child end time.
  clock_gettime(CLOCK_MONOTONIC, &child_end_time);
  double difftime =
    (child_end_time.tv_nsec - child_start_time.tv_nsec) / 1000000.0 +
    (child_end_time.tv_sec - child_start_time.tv_sec) * 1000.0;

  printf("Child (%d): ...Completed. Elapsed time = %f ms\n", getpid(), difftime);
}

// Main function
int main(int argc, char *args[])
{
  // Record the process start time and end time.
  struct timespec proc_start_time, proc_end_time;

  // Get process start time
  clock_gettime(CLOCK_MONOTONIC, &proc_start_time);

  // Input argument representing the number of child processes to be created.
  int childCount;

  // File descriptor
  int fd[2];

  // Check the number of arguments
  if (argc != 2)
  {
    fprintf(stderr, "Usage: ./program [numOfChildProc]\n");
    exit(1);
  }
  assert(args != NULL);

  // Validate and obtain the number of child processes.
  if (sscanf(args[1], "%i", &childCount) != 1) {
    fprintf(stderr, "Input argument is NOT an integer!\n");
    exit(1);
  }

  // Generate mandelbrot image and store each pixel for later display.
  // Each pixel is represented as a value in the range of [0,1].

  // Store the 2D image as a linear array of pixels (in row-major format).
  float *pixels;

  // allocate memory to store the pixels
  pixels = (float *)malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);

  if (pixels == NULL) {
    printf("Out of memory!!\n");
    exit(1);
  }

  // Create pipe.
  if (pipe(fd) == -1) {
    fprintf(stderr, "Can't create pipe!\n");
    exit(1);
  }

  // ---------------------------------------------------------------------------

  // Compute the mandelbrot image
  // Keep track of the execution time.
  // We are going to parallelize this part
  printf("Start the computation ...\n");
  pid_t cPid[childCount];         // Array of pid's of children process

  for (int c = 0; c < childCount; c++) {
    if ((cPid[c] = fork()) < 0) { // fork() error.
      perror("fork() error");
      exit(1);
    } else if (cPid[c] == 0) {    // Child.
      close(fd[0]);               // Child closes read end.
      childCompute(c, childCount, fd);
      close(fd[1]);               // Child closes write end. Both ends closed.
      exit(0);
    }
  }

  close(fd[1]); // Parent closes write end.

  // Read from the pipe.
  fprintf(stderr, "Start collecting the image lines.\n");
  MSG *msgBuffer = (MSG *)malloc(sizeof *msgBuffer);

  while (read(fd[0], msgBuffer, sizeof(MSG)) != 0) {
    memcpy(&pixels[msgBuffer->row_index * IMAGE_WIDTH],
           msgBuffer->row_data,
           IMAGE_WIDTH * sizeof(float));
  }

  // Parent waits for all children to terminate.
  while (wait(NULL) > 0) {}

  free(msgBuffer); // Free the buffer.

  close(fd[0]);    // Parent closes read end. Both ends closed.

  fprintf(stderr, "All child processes have completed.\n");

  // ---------------------------------------------------------------------

  struct rusage children_usage, self_usage;
  getrusage(RUSAGE_CHILDREN, &children_usage);
  getrusage(RUSAGE_SELF,     &self_usage);
  fprintf(
    stderr,
    "Total time spent by all children in user mode = %f ms\n",
    children_usage.ru_utime.tv_usec / 1000000.0 +
    children_usage.ru_utime.tv_sec * 1000.0);
  fprintf(
    stderr,
    "Total time spent by all children in system mode = %f ms\n",
    children_usage.ru_stime.tv_usec / 1000000.0 +
    children_usage.ru_stime.tv_sec * 1000.0);
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
  clock_gettime(CLOCK_MONOTONIC, &proc_end_time);

  // Calculate and display the total elapsed time.
  double elapsedTime =
    (proc_end_time.tv_nsec - proc_start_time.tv_nsec) / 1000000.0 +
    (proc_end_time.tv_sec - proc_start_time.tv_sec) * 1000.0;
  fprintf(stderr,
          "Total elapsed time measured by parent process = %f ms\n",
          elapsedTime);

  printf("Draw the image\n");

  // Draw the image by using the SDL2 library
  DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 10000);

  free(pixels); // Free the pixels.

  return 0;
}
