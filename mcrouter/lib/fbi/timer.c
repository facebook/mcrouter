/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "timer.h"

#include <stdio.h>

// pulled from the defaults in memcached
#define FB_TIMER_DEFAULT_LATENCY_COUNTDOWN 100
#define FB_TIMER_DEFAULT_LATENCY_AVG_POWER 6

static fb_timer_list_t all_timers;
static int num_timers = 0;
fb_cycle_timer_func_t* cycle_timer_func = &cycle_timer;
static double cpu_frequency_MHz;

static nstring_t timer_output_names[] = {
  NSTRING_LIT(".total_time_us"),
  NSTRING_LIT(".recent_peak_us"),
  NSTRING_LIT(".avg_peak_us"),
  NSTRING_LIT(".recent_min_us"),
  NSTRING_LIT(".avg_min_us"),
  NSTRING_LIT(".abs_min_us"),
};

static void fb_timer_init() {
  static int fb_timer_initialized = 0;
  if (fb_timer_initialized == 0) {
    TAILQ_INIT(&all_timers);
    num_timers = 0;
    cpu_frequency_MHz = get_cpu_frequency();
    fb_timer_initialized = 1;
  }

}

void fb_timer_set_cycle_timer_func(fb_cycle_timer_func_t func,
                                   double cycles_per_usec) {
  fb_timer_init();

  cycle_timer_func = func;
  cpu_frequency_MHz = cycles_per_usec;
}

fb_timer_t* fb_timer_alloc(nstring_t name, int window_size,
                           int decay_power) {
  fb_timer_t *ret;
  int i;

  fb_timer_init();
  ret = calloc(1, sizeof(fb_timer_t));
  if (!ret) {
    return NULL;
  }

  ret->names = malloc(sizeof(nstring_t)*NUM_TIMER_OUTPUT_TYPES);
  for (i = 0; i < NUM_TIMER_OUTPUT_TYPES; i++) {
    ret->names[i].len = name.len + timer_output_names[i].len;
    ret->names[i].str = malloc(ret->names[i].len + 1);
    memcpy(ret->names[i].str, name.str, name.len);
    memcpy(ret->names[i].str + name.len,
           timer_output_names[i].str,
           timer_output_names[i].len);
    ret->names[i].str[ret->names[i].len] = '\0';
  }

  ret->latency_countdown = (window_size == 0 ?
                            FB_TIMER_DEFAULT_LATENCY_COUNTDOWN :
                            window_size);

  ret->latency_avg_power = (decay_power == 0 ?
                            FB_TIMER_DEFAULT_LATENCY_AVG_POWER :
                            decay_power);

  ret->idx = ret->latency_countdown;

  ret->abs_min = 0;
  ret->min = 0;

  ret->status = TIMER_STOPPED;
  return ret;
}

void fb_timer_register(fb_timer_t* timer) {
  if (!timer) {
    return;
  }
  if (!timer->registered) {
    TAILQ_INSERT_TAIL(&all_timers, timer, entry);
    timer->registered = 1;
    num_timers++;
  }
}

fb_timer_list_t fb_timer_get_all_timers() {
  return all_timers;
}

int fb_timer_get_num_timers() {
  return num_timers;
}


void fb_timer_free(fb_timer_t* timer) {
  fb_timer_t *curr_temp, *curr;
  int i;
  TAILQ_FOREACH_SAFE(curr, &all_timers, entry, curr_temp) {
    if (curr == timer) {
      TAILQ_REMOVE(&all_timers, curr, entry);
      timer->registered = 0;
      break;
    }
  }
  for (i = 0; i < NUM_TIMER_OUTPUT_TYPES; i ++) {
    free(timer->names[i].str);
  }
  free(timer->names);
  free(timer);
}

double fb_timer_get_total_time(fb_timer_t* timer) {
  return ((double) timer->total_time) / cpu_frequency_MHz;
}

double fb_timer_get_recent_peak(fb_timer_t* timer) {
  return ((double) timer->peak) / cpu_frequency_MHz;
}

double fb_timer_get_avg_peak(fb_timer_t* timer) {
  return ((double) timer->avg_peak) / cpu_frequency_MHz;
}

double fb_timer_get_avg(fb_timer_t* timer) {
  return ((double) timer->avg) / cpu_frequency_MHz;
}

double fb_timer_get_recent_min(fb_timer_t *timer){
  return ((double) timer->min) / cpu_frequency_MHz;
}

double fb_timer_get_avg_min(fb_timer_t* timer){
  return ((double) timer->avg_min) / cpu_frequency_MHz;
}

double fb_timer_get_abs_min(fb_timer_t *timer){
  return ((double) timer->abs_min) / cpu_frequency_MHz;
}

static inline void fb_timer_to_nstring_helper(nstring_t* nstr, double value) {
  char temp[100];
  int len;
  len = snprintf(temp, 100, "%g", value);
  // the string was truncated so we only care about the truncated length
  if (len > 100) {
    len = 100;
  }

  nstr->str = malloc(len + 1);
  nstr->len = len;
  memcpy(nstr->str, temp, nstr->len);
  nstr->str[len] = '\0';
}

void fb_timer_to_nstring(fb_timer_t* timer, nstring_t* vals) {
  fb_timer_to_nstring_helper(&vals[TIMER_TOTAL],
                             fb_timer_get_total_time(timer));
  fb_timer_to_nstring_helper(&vals[TIMER_RECENT_PEAK],
                             fb_timer_get_recent_peak(timer));
  fb_timer_to_nstring_helper(&vals[TIMER_AVG_PEAK],
                             fb_timer_get_avg_peak(timer));
  fb_timer_to_nstring_helper(&vals[TIMER_RECENT_MIN],
                             fb_timer_get_recent_min(timer));
  fb_timer_to_nstring_helper(&vals[TIMER_AVG_MIN],
                             fb_timer_get_avg_min(timer));
  fb_timer_to_nstring_helper(&vals[TIMER_ABS_MIN],
                             fb_timer_get_abs_min(timer));
}
