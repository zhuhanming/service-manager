/**
 * CS2106 AY 20/21 Semester 1 - Lab 2 Exercise 6
 * 
 * Name: Zhu Hanming
 * Student No: A0196737L
 * Lab Group: 13
 */

#define  _POSIX_C_SOURCE 200809L
#define  _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <poll.h>

#define EXIT 1
#define SHUTDOWN 2
#define WAIT 3
#define KILL 4
#define SOCKET_NAME "sm_socket"

static void process_commands(FILE *file, int sock);
static int handle_command(char ***tokensp);
static size_t tokenise(char *const line, char ***tokens);

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  int sock;
  struct sockaddr_un server;

  // Create socket
  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    fprintf(stderr, "Error creating socket in main: %d\n", errno);
    exit(1);
  }

  // Connect to socket
  server.sun_family = AF_UNIX;
  strcpy(server.sun_path, SOCKET_NAME);
  if (connect(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un)) < 0) {
    close(sock);
    fprintf(stderr, "Error connecting to socket in main: %d\n", errno);
    exit(1);
  }

  process_commands(stdin, sock);
  close(sock);
}

static void print_prompt(void) {
  printf("sm> ");
  fflush(stdout);
}

static bool has_wait_time(int exit_status) {
  return exit_status == WAIT || exit_status == KILL || exit_status == SHUTDOWN;
}

static bool should_read(int poll_result, int exit_status) {
  return poll_result > 0 || has_wait_time(exit_status);
}

static void process_commands(FILE *file, int sock) {
  // Initialise variables
  bool exiting = false;
  char *line = NULL;
  size_t line_size = 0;
  int exit_status, read_value, poll_result, timeout;
  char buff[8192];

  print_prompt();

  // Create struct required for polling
  struct pollfd fds[1];
  memset(fds, 0 , sizeof(fds));
  fds[0].fd = sock;
  fds[0].events = POLLIN;
  timeout = 100; // 100msec

  while (!exiting) {
    if (getline(&line, &line_size, file) == -1) {
      if (feof(file)) {
        printf("End of commands; shutting down\n");
      } else {
        perror("Error while reading command; shutting down\n");
      }
      break;
    }

    // Need to use line_copy as tokenise modifies the line in-place
    char *line_copy = (char*) malloc(line_size + 1);
    strcpy(line_copy, line);
    char **tokens = NULL;
    tokenise(line_copy, &tokens);
    if (!tokens) {
      printf("Failed to tokenise command\n");
      close(sock);
      exit(1);
    }

    exit_status = handle_command(&tokens);
    free(tokens);
    free(line_copy);
    
    if (exit_status == EXIT) {
      break;
    }

    if (write(sock, line, line_size) < 0) {
      fprintf(stderr, "Error writing on socket in process_commands: %d\n", errno);
      close(sock);
      exit(1);
    }

    memset(&buff, 0, sizeof(buff));
    poll_result = poll(fds, 1, timeout);
    if (should_read(poll_result, exit_status)) {
      if ((read_value = read(sock, buff, 8192)) < 0) {
        fprintf(stderr, "Error reading response in process_commands: %d\n", errno);
        close(sock);
        exit(1);
      } else if (read_value > 0) {
        if (has_wait_time(exit_status)) {
          if (strcmp(buff, "okay\n") != 0) {
            fprintf(stderr, "Error killing, waiting or shutting down: %d\n", errno);
            close(sock);
            exit(1);
          }
        } else {
          printf("%s", buff);
          fflush(stdout);
        }
      }
    } else if (poll_result < 0) {
      fprintf(stderr, "Error polling response in process_commands: %d\n", errno);
      close(sock);
      exit(1);
    }

    if (exit_status == SHUTDOWN) {
      exiting = true;
    }

    if (!exiting) {
      print_prompt();
    }
  }

  if (line) {
    free(line);
  }

  if (ferror(file)) {
    perror("Failed to read line");
    close(sock);
    exit(1);
  }
}

// Parses for specific commands that require special handling
int handle_command(char ***tokensp) {
  const char *const cmd = (*tokensp)[0];
  if (!cmd) {
    // no-op
  } else if (strcmp(cmd, "exit") == 0) {
    return EXIT;
  } else if (strcmp(cmd, "shutdown") == 0) {
    return SHUTDOWN;
  } else if (strcmp(cmd, "stop") == 0) {
    return KILL;
  } else if (strcmp(cmd, "wait") == 0) {
    return WAIT;
  }
  return 0;
}

// Copied from original main.c
static size_t tokenise(char *const line, char ***tokens) {
  size_t reg_argv_buf_index = 0;
  size_t ret_argv_nmemb = 8;
  size_t ret_argv_index = 0;
  char **ret = calloc(ret_argv_nmemb, sizeof(char *));
  if (!ret) {
    goto fail;
  }

  bool last_was_tok = false;
  while (1) {
    char *const cur = line + reg_argv_buf_index;
    if (*cur == '\0') {
      // if we've hit the end of the line, break
      break;
    } else if (isspace(*cur)) {
      // this is whitespace; if the the last character was part of a token,
      // write a null byte here to terminate the last token
      if (last_was_tok) {
        *cur = '\0';
      }
      last_was_tok = false;
    } else {
      // this is not whitespace (so part of a token)
      // if the previous character was not part of a token (start of line or
      // whitespace), then add this to the result
      if (!last_was_tok) {
        // + 1 for the NULL at the end
        if (ret_argv_index + 1 >= ret_argv_nmemb) {
          // our result array is full, resize it
          ret_argv_nmemb += 8;
          ret = realloc(ret, ret_argv_nmemb * sizeof(char *));
          if (!ret) {
            goto fail;
          }
        }
        ret[ret_argv_index++] = cur;
      }
      last_was_tok = true;
    }
    reg_argv_buf_index++;
  }

  // NULL-terminate the result array
  ret[ret_argv_index] = NULL;
  *tokens = ret;
  return ret_argv_index;

  // N.B. goto is idiomatic for error-handling in C
fail:
  if (ret) {
    free(ret);
  }
  *tokens = NULL;
  return 0;
}
