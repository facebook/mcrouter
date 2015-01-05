/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "asox_timer.h"

#include "mcrouter/lib/fbi/debug.h"

typedef enum timer_state_e {
  timer_unknown_state = 0,
  timer_on = 1,
  timer_off = 2,
  timer_stopping = 3,
  timer_stopped = 4,
} timer_state_t;

static inline const char* timer_state_to_string(const timer_state_t state) {
  static char const* const strings[] = {
    "unknown",
    "on",
    "off",
    "stopping",
    "stopped",
  };
  return strings[state > timer_stopped ? 0 : state];
}

typedef struct _asox_timer_s {
  struct timeval time;
  asox_on_timer_t* on_timer;
  void* arg;
  timer_state_t state;
  struct event event;
} _asox_timer_t;

static void asox_timer_handler(int fd, short flags, void* arg) {
  _asox_timer_t* timer = (_asox_timer_t*)arg;

  dbg_fentry("timer: %p flags: %u state:%s", timer, flags,
             timer_state_to_string(timer->state));

  if (timer->state == timer_on) {
    timer->state = timer_off;
    timer->on_timer(timer, timer->arg);
  }

  if (timer->state == timer_off) {
    evtimer_add(&timer->event, &timer->time);
    timer->state = timer_on;
  } else {
    FBI_ASSERT(timer->state == timer_stopping);
    timer->state = timer_stopped;
    free(timer);
    timer = NULL;
  }

  dbg_fexit("timer: %p flags: %u state: %s", timer, flags,
            timer_state_to_string(timer && timer->state));
}

asox_timer_t asox_add_timer(struct event_base* event_base,
                            const struct timeval time,
                            asox_on_timer_t* on_timer,
                            void* arg) {
  _asox_timer_t* timer = NULL;

  dbg_fentry("event_base: %p time: %"u64fmt" on_timer: %p arg:%p",
             event_base, timeval_us(&time), on_timer, arg);

  if (event_base == NULL) {
    goto epilogue;
  }

  timer = malloc(sizeof(_asox_timer_t));
  FBI_ASSERT(timer != NULL);

  timer->time = time;
  timer->on_timer = on_timer;
  timer->arg = arg;
  timer->state = timer_on;

  evtimer_set(&timer->event, asox_timer_handler, timer);
  event_base_set(event_base, &timer->event);
  evtimer_add(&timer->event, &timer->time);

epilogue:
  dbg_fexit("event_base: %p time: %"u64fmt" on_timer: %p arg:%p => timer: %p",
            event_base, timeval_us(&time), on_timer, arg,
            timer);

  return timer;
}

void asox_remove_timer(asox_timer_t handle) {
  _asox_timer_t* timer = (_asox_timer_t*)handle;

  dbg_fentry("timer: %p state: %s",
             timer, timer_state_to_string(timer->state));

  FBI_ASSERT(timer != NULL);

  if (timer->state == timer_on) {
    evtimer_del(&timer->event);
    timer->state = timer_stopped;
    free(timer);
  } else {
    /* If we're calling remove from a timer handler,
       let it free the timer when it's done. */
    timer->state = timer_stopping;
  }

  dbg_fexit("timer: %p", timer);
}
