/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FB_MEMCACHE_ASOX_SEMAPHORE_H
#define FB_MEMCACHE_ASOX_SEMAPHORE_H

#include <event.h>
#include <stdbool.h>
#include <stdint.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

/**
 * An asox semaphore library based around eventfd
 *
 * In order to make writing multithreaded programs that
 * use libasox easier we need a way to signal other threads
 * (or processes) that they have new data available that
 * might not be from the network. For example if one thread
 * wants to signal another thread that it has added a piece of
 * work to a queue, we'd like that signal to be part
 * of the thread's event base loop. Thus a thread
 * can either wait on netowrk *or* local signals and multiplexed
 * inside.
 */

struct asox_sem_s;
typedef struct asox_sem_s* asox_sem_t;

typedef enum {
  // if set then the callbacks will be called
  // with multiple signals. Otherwise it will
  // be called with at most one signal at a time
  ASOX_SEM_NONE = 0x0,
  ASOX_SEM_MULTI = 0x1,
  ASOX_SEM_COMPATIBILITY_MODE = 0x2,
} asox_sem_flags_t;

/** signal callback
 *
 * the num_signals is the number of signals that this callback
 * is expected to process before the library throws them
 * away. Note that if ASOX_SEM_MULTI is not set this value will
 * be at most 1.
 *
 * note that the library will reset the number of signals
 * after the callback. Thus it assumes that the client
 * will consume all the signals passed in or discard them
 * as needed and won't make an effort to resend
 */
typedef bool (asox_sem_on_signal)(asox_sem_t sem,
                                  uint64_t num_signals,
                                  void* arg);

typedef struct asox_sem_callbacks_s {
  asox_sem_on_signal* on_signal;
} asox_sem_callbacks_t;

/** create a new semaphore object given an event base
 *
 * @param base          the event base
 * @param sem_callbacks the semaphore callbacks array
 * @param priority      the priority of the event associated
 *                      with this semaphore. If the value is less than 0
 *                      then priorities are disabled
 * @param flags         flags to control the semaphore (see above)
 * @param arg           Opage context arg passed to the callback
 */
asox_sem_t asox_sem_new(struct event_base* base,
                        asox_sem_callbacks_t const* sem_callbacks,
                        int priority,
                        asox_sem_flags_t flags,
                        void *arg);

/** create a new semaphore object from a preconstructed eventfd
 *
 *  note that this function assumes that the fd is of type eventfd
 *  the behavior is undefined if it isn't. The rest of the arguments
 *  have the same meaning as above
 */
asox_sem_t asox_sem_new_from_fds(int read_fd,
                                 int write_fd,
                                 struct event_base* base,
                                 asox_sem_callbacks_t const* sem_callbacks,
                                 int priority,
                                 asox_sem_flags_t flags,
                                 void *arg);

/**
 * remove event from the event base.
 */
void asox_sem_remove_event(asox_sem_t sem);

/** unregister the event associated with this semaphore
 * and free the object
 */
void asox_sem_del(asox_sem_t sem);

/**
 * if ASOX_SEM_MULTI is set then the associated callback
 * will be called once with the specified num_signals
 * else, the callback will be called num_signal times with
 * the num_signal value (in the callback) set to 1
 */
int asox_sem_signal(asox_sem_t asox_sem,
                    uint64_t num_signals);

__END_DECLS

#endif /*FB_MEMCACHE_ASOX_SEMAPHORE_H*/
