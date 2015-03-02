/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "asox_semaphore.h"

#include <unistd.h>
#include <fcntl.h>

#include "folly/io/async/EventFDWrapper.h"
#include "mcrouter/lib/fbi/debug.h"

typedef struct event event_t;

/**
 * This semaphore implementation is a thin wrapper around eventfd to hide all
 * the event logic. Eventfd provides an interface to a counter thats kept in the
 * kernel. The write system call takes the 8 byte value specified in the buffer.
 * The major advantage of this mechanism is that the fd provided from eventfd
 * can be multiplexed in epoll along with normal network sockets so we can wait
 * either on network *or* the semaphore. The callback is triggered when the
 * kernel sees the value associated with the eventfd be larger than 0.
 *
 * There are two modes of operation.
 * (1) in semaphore mode (EFD_SEMAPHORE passed in) the kernel will
 * always return the value (1) to the read system call and decrement
 * the counter by 1. It will return EAGAIN if the value is 0.
 *
 * (2) in the non semaphore mode, the kernel will return the number
 * and reset the value down to 0. Thus it is then the clients'
 * responsibility to call the associated callbacks as needed.
 *
 * This library exposes both modes of operation with ASOX_SEM_MULTI
 *
 * see the man page for eventfd for more details on how it works
 *
 * Compatibility Mode:
 * We also have a compatibility mode, for the hopelessly ancient kernels with no
 * event fd support at all.  In compatibility mode, we just use a pipe to pass
 * num_ready around.  We depend on the atomicity of write (which is only true
 * for writes smaller than PIPEBUF size) to make this not horribly broken.
 *
 * If the atomicity thing ends up being an issue, we'll just write 1 every time
 * and ignore the value altogether (essentially falling back to the semaphore
 * mode), but I don't anticipate it becoming a problem.
 */

struct asox_sem_s {
  int read_fd;
  int write_fd;
  struct event_base* base;
  event_t signal_event;
  // in compatibility mode, we're using this to store the counter and the pipe
  // is only used for signalling
  volatile int64_t num_pending_signals;
  void *arg;
  int priority;
  asox_sem_flags_t flags;
  asox_sem_callbacks_t const *callbacks;
};

static inline void flush_read_fd(int fd) {
  char buf[512];
  while (read(fd, buf, sizeof(buf)) > 0) { };
  FBI_ASSERT(errno == EAGAIN);
}

static inline uint64_t asox_sem_read(struct asox_sem_s *sem) {
  const int compat_mode = !!(sem->flags & ASOX_SEM_COMPATIBILITY_MODE);
  uint64_t num_ready = 0;
  if (!compat_mode) {
    int r = read(sem->read_fd, &num_ready, sizeof(num_ready));
    FBI_ASSERT(r > 0 || errno == EAGAIN);
  } else {
    flush_read_fd(sem->read_fd);
    num_ready = __sync_lock_test_and_set(&sem->num_pending_signals, 0);
  }
  return num_ready;
}

void asox_sem_handler(int fd, short flags, void *arg) {
  struct asox_sem_s* sem = (struct asox_sem_s*) arg;
  uint64_t num_ready;
  const int compat_mode = !!(sem->flags & ASOX_SEM_COMPATIBILITY_MODE);
  const int multi_mode = !!(sem->flags & ASOX_SEM_MULTI);

  // we assume that we only get woken up when
  // the eventfd is non zero for the read call
  FBI_ASSERT(flags & EV_READ);

  // this will decrement the counter associated with the FD.
  // If the value drops to 0 then errno will be set to EAGAIN
  // see man page for eventfd for more details
  errno = 0;
  while ((num_ready = asox_sem_read(sem)) > 0) {
    // if the EFD_SEMAPHORE flag is specified
    // the value returned from read will always be 1
    // and the kernel decrements the internal counter by 1
    FBI_ASSERT(compat_mode || multi_mode || (num_ready == 1));

    if (sem->callbacks->on_signal == NULL) {
        errno = 0;
        continue;
    }

    if (compat_mode && ! multi_mode) {
      uint64_t i;
      for (i = 0; i < num_ready; i++) {
        if (!sem->callbacks->on_signal((asox_sem_t)sem, 1, sem->arg)) {
          return;
        }
      }
    } else {
      if (!sem->callbacks->on_signal((asox_sem_t)sem, num_ready, sem->arg)) {
        return;
      }
    }
    errno = 0;
  }
}

int asox_sem_signal(asox_sem_t asox_sem, uint64_t num_signals) {
  struct asox_sem_s* sem = (struct asox_sem_s*) asox_sem;
  const int compat_mode = !!(sem->flags & ASOX_SEM_COMPATIBILITY_MODE);
  int ret = 1;
  int retval;

  if (! compat_mode) {
    retval = write(sem->write_fd, &num_signals, sizeof(num_signals));
    // check for errors and set ret to 0 indicating failure to signal
    if (retval == 0) {
      ret = 0;
      dbg_error("sem signal failed");
    } else if (retval < 0) {
      switch(errno) {
        case EINVAL:
          ret = 0;
          dbg_error("WTF...rajeshn can't write code");
          break;
        case EAGAIN:
          ret = 0;
          break;
        default:
          dbg_error("unknown error code for sem");
          break;
      }
    }
  } else {
    __sync_add_and_fetch(&sem->num_pending_signals, num_signals);
    if (write(sem->write_fd, " ", 1) <= 0) {
      dbg_error("Unexpected write failure on asox_sem_signal()");
    }
  }

  return ret;
}

asox_sem_t asox_sem_new_from_fds(int read_fd,
                                 int write_fd,
                                 struct event_base* base,
                                 asox_sem_callbacks_t const* sem_callbacks,
                                 int priority,
                                 asox_sem_flags_t flags,
                                 void *arg) {
  struct asox_sem_s *sem;
  sem = malloc(sizeof(struct asox_sem_s));

  if (!sem) {
    return NULL;
  }

  FBI_ASSERT(base);
  FBI_ASSERT(sem_callbacks);

  sem->read_fd = read_fd;
  sem->write_fd = write_fd;
  sem->arg = arg;
  sem->callbacks = sem_callbacks;
  sem->base = base;
  sem->flags = flags;
  sem->num_pending_signals = 0;

  event_set(&sem->signal_event, sem->read_fd, EV_READ | EV_PERSIST,
            asox_sem_handler, sem);
  event_base_set(sem->base, &sem->signal_event);
  if (priority >= 0) {
    event_priority_set(&sem->signal_event, priority);
  }
  event_add(&sem->signal_event, NULL);
  return sem;
}

asox_sem_t asox_sem_new(struct event_base* base,
                        asox_sem_callbacks_t const* sem_callbacks,
                        int priority,
                        asox_sem_flags_t flags,
                        void *arg) {
  int ev_flags;
  int fd;
  int fds[2];
  int rc;

  if (!(flags & ASOX_SEM_COMPATIBILITY_MODE)) {
    ev_flags = EFD_NONBLOCK | (flags & ASOX_SEM_MULTI ? 0 : EFD_SEMAPHORE);
    fd = eventfd(0, ev_flags);
    if (fd >= 0) {
      return asox_sem_new_from_fds(fd, fd, base, sem_callbacks, priority,
                                   flags, arg);
    }

    // before 2.6.27 the flags have to be 0
    // so if the flags aren't 0 then its
    // probably this cause so let the caller try
    // again.
    fd = eventfd(0, 0);
    if (fd >= 0) {
      fcntl(fd, F_SETFL, O_NONBLOCK);
      return asox_sem_new_from_fds(fd, fd, base, sem_callbacks, priority,
                                   flags, arg);
    }
  }

  // Compatibility mode:
  // really old kernels don't have eventfd at all, so let's just fallback to
  // unix pipes and stuff
  rc = pipe(fds);
  if (rc != 0) {
    switch (errno) {
      case EMFILE:
        dbg_error("Too many file descriptors, cannot create pipe");
        break;
      case EFAULT:
        dbg_error("WTF...agartrell can't write code either");
        break;
      default:
        assert(!"This is impossible per `man 2 pipe`");
    }
    return NULL;
  }
  fcntl(fds[0], F_SETFL, O_NONBLOCK);
  fcntl(fds[1], F_SETFL, O_NONBLOCK);
  return asox_sem_new_from_fds(fds[0], fds[1], base, sem_callbacks, priority,
                               flags | ASOX_SEM_COMPATIBILITY_MODE, arg);
}

void asox_sem_remove_event(asox_sem_t sem_in) {
  struct asox_sem_s *sem = (struct asox_sem_s*) sem_in;
  event_del(&sem->signal_event);
}

void asox_sem_del(asox_sem_t sem_in) {
  struct asox_sem_s *sem = (struct asox_sem_s*) sem_in;
  asox_sem_remove_event(sem_in);
  if (sem->read_fd != -1) close(sem->read_fd);
  if (sem->write_fd != -1 && sem->write_fd != sem->read_fd)
    close(sem->write_fd);
  sem->read_fd = sem->write_fd = -1;
  free(sem);
  return;
}
