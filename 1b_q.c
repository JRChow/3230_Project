/* 2017-18 Programming Project
   Part One B - part1b-mandelbrot.c
    - 2017.10.28
    - Qiu Haoran (3035234478)
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "Mandel.h"
#include "draw.h"

// data structure for passing data from child process to parent process through
// pipe
typedef struct Messsage {
  int   pid_idx;
  int   row_idx;
  int   done; // 0 for uncompleted 1 for completed
  float data[IMAGE_WIDTH];
} msg;

// data structure for passing task from parent process to child process through
// pipe
typedef struct Task {
  int pid_idx;
  int start_row;
  int num_of_rows;
} task;

// global variables for signal_handler
int data_pfd[2];   // data pipe - worker writes boss reads
int task_pfd[2];   // task pipe - boss writes worker reads
int tasks_num = 0; // total number of tasks a process has performed

// handler for sigusr1 - get task from pipe and put data into pipe
void sigusr1_handler(int signum) {
  // get a task from Task pipe
  task *newTask = (task *)malloc(sizeof(task));

  read(task_pfd[0], newTask, sizeof(task));

  int myPID = (int)getpid();
  printf("Child(%d): Start the computation - row %d to %d ...\n",
         myPID,
         newTask->start_row,
         newTask->start_row + newTask->num_of_rows - 1);
  struct timespec start_compute, end_compute;
  clock_gettime(CLOCK_MONOTONIC, &start_compute);
  int  i, j;
  msg *result;
  result = (msg *)malloc(sizeof(msg));

  for (i = 0; i < newTask->num_of_rows; i++) {
    for (j = 0; j < IMAGE_WIDTH; j++) {
      // compute a value for each point c (x, y) in the complex plane
      result->data[j] = Mandelbrot(j, i + newTask->start_row);
    }
    result->row_idx = i + newTask->start_row;
    result->pid_idx = newTask->pid_idx;

    if (i == newTask->num_of_rows - 1) {
      result->done = 1;
    } else result->done = 0;

    // pass the result of this row to parent process via the pipe
    int status = write(data_pfd[1], result, sizeof(msg));

    if (status == -1) printf("Error occurs during writting!\n");
  }

  // timing statistics
  float difftime;
  clock_gettime(CLOCK_MONOTONIC, &end_compute);
  difftime =
    (end_compute.tv_nsec -
     start_compute.tv_nsec) / 1000000.0 +
    (end_compute.tv_sec - start_compute.tv_sec) * 1000.0;
  printf("Child(%d):	 ... completed. Elapse time = %.3f ms\n", myPID, difftime);
  tasks_num++;

  // free allocated memory
  free(result);
}

// handler for sigint - print to display and return the total number of tasks it
// has done
void sigint_handler(int signum) {
  int myPID = (int)getpid();

  printf("Process %d is interrupted by ^C. Bye Bye\n", myPID);
  exit(tasks_num);
}

int main(int argc, char *args[]) {
  if (argc < 3) {
    printf(
      "You need to input 1. number of processes; 2. number of rows per task!\n");
    exit(1);
  }
  int num_processes, num_rows_per_task;
  sscanf(args[1], "%d", &num_processes);
  sscanf(args[2], "%d", &num_rows_per_task);
  printf("Number of processes to use: %d\nNumber of rows per task: %d\n",
         num_processes,
         num_rows_per_task);

  // storing PIDs for child processes
  int *pids = malloc(num_processes * sizeof(int));

  // data structure to store the start and end times of the whole program
  struct timespec start_time, end_time;
  clock_gettime(CLOCK_MONOTONIC, &start_time);

  // store the 2D image as a linear array of pixels (in row-major format)
  float *pixels;

  // allocate memory to store the pixels
  pixels = (float *)malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);

  if (pixels == NULL) {
    printf("Out of memory!!\n");
    exit(1);
  }

  // register signal handler
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGINT,  sigint_handler);

  // create pipe for communication
  if (pipe(data_pfd) != 0) printf("Error occurs when constructing data pipe!\n");

  if (pipe(task_pfd) != 0) printf("Error occurs when constructing task pipe!\n");

  int   idx, row_idx = 0; // from 0 to IMAGE_WIDTH
  pid_t who;
  task *task_msg = (task *)malloc(sizeof(task));

  for (idx = 0; idx < num_processes; idx++) {
    who = fork();

    if (who == 0) { // child process
      int myPID = (int)getpid();
      printf("Child(%d): Start up. Wait for task!\n", myPID);

      while (1) ;  // wait for tasks and do computation
      return 0;
    } else {       // parent process
      pids[idx] = (int)who;
    }
  }
  usleep(40000); // wait for child process construction

  for (idx = 0; idx < num_processes; idx++) {
    task_msg->start_row = row_idx;

    if (row_idx + num_rows_per_task >= IMAGE_WIDTH) task_msg->num_of_rows =
        IMAGE_WIDTH - row_idx;
    else task_msg->num_of_rows = num_rows_per_task;
    task_msg->pid_idx = idx;
    row_idx          += task_msg->num_of_rows;

    if (write(task_pfd[1], task_msg, sizeof(task)) > 0) kill(pids[idx], SIGUSR1);
    else printf("Error occurs when writing into task pipe!\n");

    // usleep(200);
  }

  msg *income_msg;
  income_msg = (msg *)malloc(sizeof(msg));
  int status, notask = 0, total = 0;
      printf("Start collecting the image lines\n");

  while (1) {
    status = read(data_pfd[0], income_msg, sizeof(msg));

    if (status == -1) {
      printf("Error occurs when reading from the pipe!\n");
      exit(1);
    } else if (status != 0) {
      // store the result into pixels
      for (int i = 0; i < IMAGE_WIDTH;
           i++) pixels[income_msg->row_idx * IMAGE_WIDTH +
                       i] = income_msg->data[i];
      total++;

      if (total >= IMAGE_HEIGHT) break;

      if (row_idx >= IMAGE_HEIGHT) notask = 1;

      // if child has finished his task, distribute new task to him
      if ((income_msg->done == 1) && (notask == 0)) {
        // has finished
        task_msg->start_row = row_idx;

        if (row_idx + num_rows_per_task >= IMAGE_WIDTH) task_msg->num_of_rows =
            IMAGE_WIDTH - row_idx;
        else task_msg->num_of_rows = num_rows_per_task;
        task_msg->pid_idx = income_msg->pid_idx;
        row_idx          += task_msg->num_of_rows;

        if (write(task_pfd[1], task_msg,
                  sizeof(task)) > 0) kill(pids[income_msg->pid_idx], SIGUSR1);
        else printf("Error occurs when writing into task pipe!\n");

        // usleep(200);
      }
    }
  }

  for (int i = 0; i < num_processes; i++) kill(pids[i], SIGINT);
  usleep(200);
  status = 0;
  int pid = (int)wait(&status);

  while (pid > 0) {
    printf("Child process %d terminated and complete %d tasks\n",
           pid,
           WEXITSTATUS(status));
    pid = (int)wait(&status);
  }
  printf("All Child processes have completed\n");

  // retrieve CPU resource usage statistics
  struct rusage stat_parent, stat_children;
  getrusage(RUSAGE_SELF,     &stat_parent);
  getrusage(RUSAGE_CHILDREN, &stat_children);

  printf("Total time spent by all child processes in user mode = %.3f ms\n",
         (float)(stat_children.ru_utime.tv_usec / 1000.0 +
                 stat_children.ru_utime.tv_sec * 1000));
  printf("Total time spent by all child processes in system mode = %.3f ms\n",
         (float)(stat_children.ru_stime.tv_usec / 1000.0 +
                 stat_children.ru_stime.tv_sec * 1000));
  printf("Total time spent by parent process in user mode = %.3f ms\n",
         (float)(stat_parent.ru_utime.tv_usec / 1000.0 +
                 stat_parent.ru_utime.tv_sec *
                 1000));
  printf("Total time spent by parent process in system mode = %.3f ms\n",
         (float)(stat_parent.ru_stime.tv_usec / 1000.0 +
                 stat_parent.ru_stime.tv_sec *
                 1000));

  // Report timing
  float difftime;
  clock_gettime(CLOCK_MONOTONIC, &end_time);
  difftime =
    (end_time.tv_nsec -
     start_time.tv_nsec) / 1000000.0 +
    (end_time.tv_sec - start_time.tv_sec) * 1000.0;
  printf("Total elapse time measured by parent process = %.3f ms\n", difftime);

  printf("Draw the image\n");
  DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 3000);

  // free allocated memory
  free(pids);
  free(pixels);
  free(task_msg);
  free(income_msg);

  return 0;
}
