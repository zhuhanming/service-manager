/**
 * This file contains declarations for the service manager.
 */

#ifndef CS2106_SM_H
#define CS2106_SM_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define SM_MAX_SERVICES 32

typedef struct sm_status {
  pid_t pid;
  const char *path;
  bool running;
} sm_status_t;

void sm_init(void);
void sm_free(void);
void sm_start(const char *processes[]);
void sm_startlog(const char *processes[]);
size_t sm_status(sm_status_t statuses[]);
void sm_stop(size_t index);
void sm_wait(size_t index);
void sm_shutdown(void);
void sm_showlog(size_t index);

#endif
