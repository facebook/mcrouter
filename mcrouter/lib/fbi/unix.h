/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_UNIX_H
#define FBI_UNIX_H

#include <fcntl.h>
#include <pwd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

static inline int is_root() {
  return getuid() == 0 || geteuid() == 0;
}

/** Raises file descriptor limit to n if greater than zero
 * If negative, raises to system max
 * If zero, leaves unchanged
 * @param n max number of open files
 * @return 0 on success, -1 on error
 */
static inline int set_fd_limit(int n) {
  char buff[128];
  FILE *f = NULL;
  if (n == 0) {
    dbg_info("Using default open file limits");
    return 0;
  }

  struct rlimit limits = {0, 0};

  // figure out system values for nr_open
  // if none available, keep max open file descriptors same
  if (n < 0) {
    if ((f = fopen("/proc/sys/fs/nr_open", "r")) == NULL){
      dbg_error("Could not open /proc/sys/fs/nr_open for reading");
      goto error;
    }

    if (fgets(buff, sizeof(buff), f) == NULL) {
      dbg_error("Could not read from /proc/sys/fs/nr_open");
      goto error;
    }
    fclose(f);
    f = NULL;

    errno = 0;
    n = strtoull(buff, NULL, 10);
    if (errno != 0) {
      dbg_error("Could not convert /proc/sys/fs/nr_open to integer");
      goto error;
    }
  }

  limits = (struct rlimit) { .rlim_cur = (rlim_t)n, .rlim_max = (rlim_t)n };

  if (setrlimit(RLIMIT_NOFILE, &limits) != 0) {
    dbg_error("Could not set RLIMIT_NOFILE");
    goto error;
  }

  dbg_info("raised soft/hard fd limit to %d", (int)limits.rlim_cur);
  return 0;
error:
  if (f != NULL) {
    fclose(f);
  }
  perror("set_fd_limit");
  return -1;
}

static inline int drop_privileges_to(const char *username) {
  FBI_ASSERT(username != NULL && username[0] != '\0');
  long blen = -1;
  char *buf = NULL;
  struct passwd pws;
  struct passwd *pw = NULL;
  int rc;
  int ret = -1;

  blen = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (blen == -1) {
    dbg_error("sysconf failed");
    goto epilogue;
  }

  if ((buf = (char*)malloc(blen)) == NULL) {
    dbg_error("no memory for buffer");
    goto epilogue;
  }

  if ((rc = getpwnam_r(username, &pws, buf, blen, &pw)) != 0 || pw == NULL) {
    dbg_error("can't find the user %s to switch to", username);
    goto epilogue;
  }

  if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
    dbg_error("failed to assume identity of user %s", username);
    goto epilogue;
  }

  dbg_info("Dropped privileges to %s", username);
  ret = 0;

epilogue:
  free(buf);

  return ret;
}

static inline void daemonize_process() {
  if (getppid() == 1) {
    return;
  }
  switch (fork()) {
  case 0:
    setsid();
    int i;
    // close all descriptors
    for (i = getdtablesize(); i >= 0; i--) {
      close(i);
    }
    i = open("/dev/null", O_RDWR);
    dup2(i, 1);
    dup2(i, 2);
    break;
  case -1:
    fprintf(stderr, "Can't fork background process\n");
    exit(1);
  default: /* Parent process */
    dbg_low("Running in the background");
    exit(0);
  }
}


__END_DECLS

#endif
