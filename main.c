/**
 * CS2106 AY 20/21 Semester 1 - Lab 2 Exercise 6
 * 
 * Name: Zhu Hanming
 * Student No: A0196737L
 * Lab Group: 13
 */

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

#include "sm.h"
#define SOCKET_NAME "sm_socket"

static void daemonise_self();
static void start_server();
static void process_commands(int msg_sock, int sock);
static bool handle_command(const size_t num_tokens, char ***tokensp);
static void transform_tokens_for_start(const size_t num_tokens, char ***tokens);
static size_t tokenise(char *const line, char ***tokens);

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  daemonise_self();
  start_server();
  return 0;
}

static void daemonise_self(void) {
  pid_t pid, sid;
  pid = fork();
  if (pid == -1) {
    fprintf(stderr, "Error forking in daemonise_self: %d\n", errno);
    exit(1);
  } else if (pid > 0) {
    // Kill the parent process
    exit(0);
  }

  // Set up the child process
  umask(0);
  sid = setsid();
  if (sid < -1) {
    fprintf(stderr, "Error setting sid in daemonise_self: %d\n", errno);
    exit(1);
  }

  close(STDIN_FILENO);
}

// Helper function with clean-up of 1 socket
static void close_socket_and_exit(int sock, int exit_code) {
  close(sock);
  unlink(SOCKET_NAME);
  exit(exit_code);
}

// Helper function with clean-up of 2 sockets
static void close_sockets_and_exit(int sock1, int sock2, int exit_code) {
  close(sock1);
  close(sock2);
  unlink(SOCKET_NAME);
  exit(exit_code);
}

// Redirects stdout and stderr to socket
static void redirect_init(int sock) {
  int dup_status;
  // Replace stdout
  dup_status = dup2(sock, STDOUT_FILENO);
  if (dup_status == -1) {
    fprintf(stderr, "Error duplicating socket: %d\n", errno);
    exit(1);
  }
    
  // Replace stderr
  dup_status = dup2(sock, STDERR_FILENO);
  if (dup_status == -1) {
    fprintf(stderr, "Error duplicating socket: %d\n", errno);
    exit(1);
  }
}

static void start_server(void) {
  int sock, msg_sock;
  struct sockaddr_un server;
  int option = 1;

  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    fprintf(stderr, "Error creating socket in start_server: %d\n", errno);
    exit(1);
  }
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
    fprintf(stderr, "Error setting socket options in start_server: %d\n", errno);
    close(sock);
    exit(1);
  }
  server.sun_family = AF_UNIX;
  strcpy(server.sun_path, SOCKET_NAME);
  if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un)) < 0) {
    fprintf(stderr, "Error binding socket in start_server: %d\n", errno);
    close_socket_and_exit(sock, 1);
  }
  listen(sock, 5); // Limit to 5 requests in backlog for now
  sm_init();
  for (;;) {
    msg_sock = accept(sock, 0, 0);
    if (msg_sock == -1) {
      fprintf(stderr, "Error accepting request in start_server: %d\n", errno);
      close_socket_and_exit(sock, 1);
    } else {
      redirect_init(msg_sock);
      process_commands(msg_sock, sock);
    }
    close(msg_sock);
  }
  sm_free();
  close_socket_and_exit(sock, 0); // actually never runs
}

static void process_commands(int msg_sock, int sock) {
  char buff[8192];
  bool exiting = false;
  int read_value;
  while (!exiting) {
    memset(&buff, 0, sizeof(buff));
    if ((read_value = read(msg_sock, buff, 8192)) < 0) {
      fprintf(stderr, "Error reading message in process_commands: %d\n", errno);
      close_sockets_and_exit(msg_sock, sock, 1);
    } else if (read_value > 0) {
      char **tokens = NULL;
      size_t num_tokens = tokenise(buff, &tokens);
      if (!tokens) {
        fprintf(stderr, "Failed to tokenise command\n");
        close_sockets_and_exit(msg_sock, sock, 1);
      }
      exiting = handle_command(num_tokens, &tokens);
      free(tokens);
      fflush(stdout);

      // This only happens when shutdown is received
      if (exiting) {
        sm_free();
        close_sockets_and_exit(msg_sock, sock, 0);
      }
    } else {
      // read_value == 0 i.e. EOF
      exiting = true;
    }
  }
}

#define CHECK_ARGC(nargs)                                                                          \
  do {                                                                                             \
    if (num_tokens < nargs) {                                                                      \
      printf("Insufficient arguments for %s\n", cmd);                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (0)

#define SCAN_SERVICE_NUMBER(into)                                                                  \
  do {                                                                                             \
    if (sscanf((*tokensp)[1], "%zu", &into) != 1) {                                                \
      printf("Invalid service number %s\n", (*tokensp)[1]);                                        \
      return false;                                                                                \
    }                                                                                              \
  } while (0)

static bool handle_command(const size_t num_tokens, char ***tokensp) {
  const char *const cmd = (*tokensp)[0];
  if (!cmd) {
    // no-op
  } else if (strcmp(cmd, "start") == 0) {
    CHECK_ARGC(2);
    transform_tokens_for_start(num_tokens, tokensp);
    sm_start((const char **)(*tokensp) + 1);
  } else if (strcmp(cmd, "startlog") == 0) {
    CHECK_ARGC(2);
    transform_tokens_for_start(num_tokens, tokensp);
    sm_startlog((const char **)(*tokensp) + 1);
  } else if (strcmp(cmd, "wait") == 0) {
    CHECK_ARGC(2);
    size_t service_number;
    SCAN_SERVICE_NUMBER(service_number);
    sm_wait(service_number);
    fprintf(stdout, "okay\n"); // responds with an "okay"
  } else if (strcmp(cmd, "stop") == 0) {
    CHECK_ARGC(2);
    size_t service_number;
    SCAN_SERVICE_NUMBER(service_number);
    sm_stop(service_number);
    fprintf(stdout, "okay\n"); // responds with an "okay"
  } else if (strcmp(cmd, "status") == 0) {
    sm_status_t statuses[SM_MAX_SERVICES] = {0};
    size_t num_services = sm_status(statuses);
    for (size_t i = 0; i < num_services; ++i) {
      sm_status_t *status = statuses + i;
      fprintf(stdout, "%zu. %s (PID %ld): %s\n", i, status->path, (long)status->pid,
             status->running ? "Running" : "Exited");
    }
  } else if (strcmp(cmd, "showlog") == 0) {
    CHECK_ARGC(2);
    size_t service_number;
    SCAN_SERVICE_NUMBER(service_number);
    sm_showlog(service_number);
  } else if (strcmp(cmd, "shutdown") == 0) {
    sm_shutdown();
    fprintf(stdout, "okay\n"); // responds with an "okay"
    return true;
  } else {
    printf("Unknown command %s\n", cmd);
  }

  return false;
}

// ==================== //
// UNMODIFIED FUNCTIONS //
// ==================== //

static void transform_tokens_for_start(const size_t num_tokens, char ***tokens) {
  char **cursor = (*tokens) + 1;
  while (*cursor) {
    // if this is a pipe, then end this subarray
    if (strcmp(*cursor, "|") == 0) {
      *cursor = NULL;
    }
    ++cursor;
  }

  // resize the tokens array to add an additional NULL at the end
  *tokens = realloc(*tokens, (num_tokens + 2) * sizeof(char *));
  if (!*tokens) {
    perror("Failed to resize tokens array");
    exit(1);
  }
  (*tokens)[num_tokens] = (*tokens)[num_tokens + 1] = NULL;
}

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
