/**
 * CS2106 AY 20/21 Semester 1 - Lab 2 Exercise 6
 *
 * This file contains function definitions. Your implementation should go in
 * this file.
 * 
 * Name: Zhu Hanming
 * Student No: A0196737L
 * Lab Group: 13
 */

#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include "sm.h"

#define NORMAL 0
#define LOG 1
#define KILL 2
#define WAIT 3

// Wrapper struct around the given sm_status_t
typedef struct sm_service {
  sm_status_t status;
  const pid_t *pids;
  int num_processes;
} sm_service_t;

// Global variables
sm_service_t global_services[32];
int service_count;

// Use this function to any initialisation if you need to.
void sm_init(void) {
  service_count = 0;
}

// Use this function to do any cleanup of resources.
void sm_free(void) {
  for (int i = 0; i < service_count; i++) {
    free((void *)global_services[i].status.path);
    free((void *)global_services[i].pids);
  }
}

// ======================== //
// GENERAL HELPER FUNCTIONS //
// ======================== //

int num_digits(int value) {
  int result = 1;
  while (value > 9) {
    value /= 10;
    result++;
  }
  return result;
}

char* get_filename(int service_no) {
  // Allocate memory to store the filename
  char* filename = (char*) malloc((11 + num_digits(service_no)) * sizeof(char) + 1);
  if (filename == NULL) {
    fprintf(stderr, "Error allocating memory for filename: %d\n", errno);
    exit(1);
  }

  // Fill up char array with the required filename
  int num_printed = sprintf(filename, "service%d.log", service_no);
  if (num_printed < 0) {
    fprintf(stderr, "Error copying filename into variable with sprintf\n");
    exit(1);
  }

  // Return char array
  return filename;
}

// ======================== //
// PROCESS HELPER FUNCTIONS //
// ======================== //
int count_processes(const char *processes[]) {
  int number = 0;
  for (int i = 0; processes[i] != NULL || processes[i+1] != NULL; i++) {
    // Means we have reached the end of one command/process
    if (processes[i] == NULL) {
      number++;
    }
  }

  // To include the final command/process, which was not
  // counted due to the while loop terminating condition
  return number + 1;
}

void increment_pointer_to_next_command(int* pointer, const char *processes[]) {
  // Get the pointer to toggle to the next NULL
  while (processes[*pointer] != NULL) {
    (*pointer)++;
  }
  // Shift it one more time past the NULL
  (*pointer)++;
}

// ======================== //
// STORAGE HELPER FUNCTIONS //
// ======================== //

sm_status_t* create_sm_status(const char* path, pid_t child_pid) {
  // Initialise sm_status instance
  sm_status_t* new_sm_status = (sm_status_t*) malloc(sizeof(sm_status_t));
  if (new_sm_status == NULL) {
    fprintf(stderr, "Error allocating memory for new_sm_status: %d\n", errno);
    exit(1);
  }

  // Initialise char array for path
  char *placeholder = (char *) malloc(strlen(path) + 1);
  if (placeholder == NULL) {
    fprintf(stderr, "Error allocating memory for placeholder: %d\n", errno);
    exit(1);
  }
  strcpy(placeholder, path);

  // Set initial values for sm_status
  new_sm_status->path = (const char*) placeholder;
  new_sm_status->pid = child_pid;
  return new_sm_status;
}

void update_global_array(sm_status_t* status, int num_processes, pid_t* pids) {
  global_services[service_count].status = *status;
  global_services[service_count].num_processes = num_processes;
  global_services[service_count++].pids = pids;

  // Free the space taken for status after copying over the values
  free(status);
}

FILE* initialise_file_pointer() {
  char* filename = get_filename(service_count);

  // Open filename in append mode
  FILE *fp = fopen(filename, "a");
  if (fp == NULL) {
    fprintf(stderr, "Error appending to service%d.log: %d\n", service_count, errno);
    exit(1);
  }

  // Free the filename string
  free(filename);
  return fp;
}

FILE* read_file_pointer(int service_no) {
  char* filename = get_filename(service_no);

  // Open filename in read mode
  FILE *fp = fopen(filename, "r");
  
  // We have no NULL handling here. The caller will handle it.

  // Free the filename string
  free(filename);
  return fp;
}

// ===================== //
// PIPE HELPER FUNCTIONS //
// ===================== //
int* initialise_pipes(int num_processes) {
  // Allocate space for pipes
  int *pipes = (int *) malloc(num_processes * 2 * sizeof(int));
  if (pipes == NULL) {
    fprintf(stderr, "Error allocating memory for pipes: %d\n", errno);
    exit(1);
  }

  int pipe_status;
  for (int i = 0; i < num_processes; i++) {
    pipe_status = pipe(pipes + (i * 2));
    if (pipe_status == -1) {
      fprintf(stderr, "Error opening pipes: %d\n", errno);
      exit(1);
    }
  }
  return pipes;
}

void dup_pipes(int* pipes, int i, int num_processes, int type) {
  int dup_status;
  // Replace stdout
  if (i != num_processes - 1) {
    // This is not the last process
    dup_status = dup2(pipes[1 + (i * 2)], STDOUT_FILENO);
    if (dup_status == -1) {
      fprintf(stderr, "Error duplicating pipes: %d\n", errno);
      exit(1);
    }
  } else if (type == LOG) {
    FILE *fp = initialise_file_pointer();

    // Last process and log file
    // Replacing stdout
    dup_status = dup2(fileno(fp), STDOUT_FILENO);
    if (dup_status == -1) {
      fprintf(stderr, "Error duplicating pipes: %d\n", errno);
      exit(1);
    }
    
    // Replacing stderr
    dup_status = dup2(fileno(fp), STDERR_FILENO);
    if (dup_status == -1) {
      fprintf(stderr, "Error duplicating pipes: %d\n", errno);
      exit(1);
    }

    // Close log file pointer
    dup_status = fclose(fp);
    if (dup_status != 0) {
      fprintf(stderr, "Error closing file pointer: %d\n", errno);
      exit(1);
    }

  }
  // Replace stdin of all processes except the first
  if (i != 0) {
    dup_status = dup2(pipes[(i - 1) * 2], STDIN_FILENO);
    if (dup_status == -1) {
      fprintf(stderr, "Error duplicating pipes: %d\n", errno);
      exit(1);
    }
  }
}

void close_pipes(int* pipes, int num_processes) {
  int close_status;
  for (int j = 0; j < num_processes * 2; j++) {
    close_status = close(pipes[j]);
    if (close_status == -1) {
      fprintf(stderr, "Error closing pipes: %d\n", errno);
      exit(1);
    }
  }
}

void close_and_free_pipes(int* pipes, int num_processes) {
  close_pipes(pipes, num_processes);
  free(pipes);
}

// ===================== //
// MAIN HELPER FUNCTIONS //
// ===================== //

void sm_start_helper(const char *processes[], int type) {
  // Count processes and initialise pipes
  int num_processes = count_processes(processes);
  int *pipes = initialise_pipes(num_processes);

  // Prepare pid array and placeholder
  pid_t *pids = (pid_t *) malloc(num_processes * sizeof(pid_t));
  pid_t child_pid;

  if (pids == NULL) {
    fprintf(stderr, "Error allocating memory for pids: %d\n", errno);
    exit(1);
  }

  // Pointer that points to first string of process command
  int pointer = 0;

  // Fork and run command
  for (int i = 0; i < num_processes; i++) {
    child_pid = fork();
    if (child_pid == -1) {
      fprintf(stderr, "Error forking in sm_start_helper: %d\n", errno);
      exit(1);
    } else if (child_pid == 0) {
      // Following part is only run by child process
      dup_pipes(pipes, i, num_processes, type);
      close_pipes(pipes, num_processes);
      execvp(processes[pointer], (char *const *)(processes + pointer));
      // If it's still running, means error has occurred
      fprintf(stderr, "Error executing %s: %d\n", processes[pointer], errno);
      exit(1);
    } else if (i != num_processes - 1) {
      increment_pointer_to_next_command(&pointer, processes);
      pids[i] = child_pid;
    } else {
      pids[i] = child_pid;
    }
  }

  // Close and clean up pipes
  close_and_free_pipes(pipes, num_processes);

  // Create sm_status_t to store values
  sm_status_t *new_sm_status = create_sm_status(processes[pointer], child_pid);

  // Update global status array
  update_global_array(new_sm_status, num_processes, pids);
}

void sm_kill_wait_helper(size_t index, int type) {
  sm_service_t service = global_services[index];
  int num_processes = service.num_processes;
  const pid_t *pids = service.pids;
  int status;

  // We won't have handling of errors for waitpid because
  // some of the child processes may have been "waited"
  // multiple times, i.e. entry in process table cleaned up
  for (int i = 0; i < num_processes; i++) {
    if (type == KILL) {
      if (waitpid(pids[i], &status, WNOHANG) == 0) {
        int kill_result = kill(pids[i], SIGTERM);
        if (kill_result == -1) {
          fprintf(stderr, "Error killing the process: %d\n", errno);
          exit(1);
        }
        waitpid(pids[i], &status, WUNTRACED);
      }
    } else if (type == WAIT) {
      waitpid(pids[i], &status, WUNTRACED);
    } else {
      // Unknown type passed in
      fprintf(stderr, "Unrecognised command running!\n");
      exit(1);
    }
  }
}

// ======================= //
// MAIN SOLUTION FUNCTIONS //
// ======================= //

// Exercise 1a/2: start services
void sm_start(const char *processes[]) {
  sm_start_helper(processes, NORMAL);
}

// Exercise 1b: print service status
size_t sm_status(sm_status_t statuses[]) {
  int status;
  for (int i = 0; i < service_count; i++) {
    statuses[i].pid = global_services[i].status.pid;
    statuses[i].path = global_services[i].status.path;
    // No handling of error from waitpid - same as sm_stop and sm_wait
    statuses[i].running = waitpid(statuses[i].pid, &status, WNOHANG) == 0;
  }
  return service_count;
}

// Exercise 3: stop service, wait on service, and shutdown
void sm_stop(size_t index) {
  sm_kill_wait_helper(index, KILL);
}

void sm_wait(size_t index) {
  sm_kill_wait_helper(index, WAIT);
}

void sm_shutdown(void) {
  for (int i = 0; i < service_count; i++) {
    sm_stop(i);
  }
}

// Exercise 4: start with output redirection
void sm_startlog(const char *processes[]) {
  sm_start_helper(processes, LOG);
}

// Exercise 5: show log file
void sm_showlog(size_t index) {
  FILE* fp = read_file_pointer(index);
  if (fp == NULL) {
    printf("service has no log file\n");
    return;
  }

  // Print file character by character
  char c;
  while((c = fgetc(fp)) != EOF) {
    printf("%c", c);
  }

  fclose(fp);
}
