/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_TIMER_H
#define FBI_TIMER_H

#include "mcrouter/lib/fbi/fb_cpu_util.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/fbi/queue.h"

__BEGIN_DECLS

typedef enum {
  TIMER_STOPPED,
  TIMER_RUNNING,
  NUM_TIMER_STATES
} fb_timer_status_t;

typedef enum {
  TIMER_TOTAL=0,
  TIMER_RECENT_PEAK,
  TIMER_AVG_PEAK,
  TIMER_RECENT_MIN,
  TIMER_AVG_MIN,
  TIMER_ABS_MIN,
  NUM_TIMER_OUTPUT_TYPES,
} fb_timer_output_type_t;

typedef struct fb_timer_s {
  nstring_t *names;
  uint64_t start_time;
  int latency_countdown;
  int latency_avg_power;
  uint64_t total_time;
  uint64_t avg_peak;
  uint64_t peak;
  uint64_t _last_total;
  uint64_t avg;
  uint64_t abs_min;
  uint64_t avg_min;
  uint64_t min;
  uint64_t idx;
  int registered;
  fb_timer_status_t status;
  TAILQ_ENTRY(fb_timer_s) entry;
} fb_timer_t;

typedef TAILQ_HEAD(fb_timer_list, fb_timer_s) fb_timer_list_t;

typedef uint64_t fb_cycle_timer_func_t(void);

extern fb_cycle_timer_func_t* cycle_timer_func;

void fb_timer_set_cycle_timer_func(fb_cycle_timer_func_t func,
                                   double cycles_per_usec);

/**
 * Allocate a timer object with the specified settings
 *
 * @param name nstring_t the name of the timer
 * @param window_size int the size of the window during to calculate peaks
 *                        and minimums (use 0 for the default value of 100)
 * @param decay_power int (2^power) specifies how quickly to decay the peak and
 *                        min values (use 0 for the default value of 2^6 = 64)
 * @return fb_timer_t* the timer object (NULL if the malloc failed)
 */
fb_timer_t* fb_timer_alloc(nstring_t name,
                           int window_size,
                           int decay_power);
/**
 * Free the timer object and the associated nstring
 * This will also remove the timer object from the TAILQ
 *
 * @param timer fb_timer_t the pointer to the timer object to free
 */
void fb_timer_free(fb_timer_t* timer);

/**
 * Register the timer internally to the library so that it is
 * returned with all the available timers. Thus we can have different
 * timers across multiple libraries and only have the highest level
 * library print them out on the behalf of everyone else.
 *
 * @param timer fb_timer_t the pointer to the timer object to register
 */
void fb_timer_register(fb_timer_t *timer);

/**
 * Return a list of all registered timer objects. use TAILQ_FOREACH
 * to iterate through them
 *
 * @return fb_timer_list_t a TAILQ of all the timers
 */
fb_timer_list_t fb_timer_get_all_timers();

int fb_timer_get_num_timers();

/**
 * Return the most recent peak value for the timer for the past
 * N samples where N is specified by the window size above
 *
 * @param timer fb_timer_t the timer object
 * @return double the most recent peak value
 */
double fb_timer_get_recent_peak(fb_timer_t* timer);

/**
 * Return the moving window average for the worst 1% samples. These samples
 * decay over time as specified by the 2^(decay power) above
 *
 * @param timer fb_timer_t the timer object
 * @return double average peak for the worst 1% of the samples
 */
double fb_timer_get_avg_peak(fb_timer_t* timer);


/**
 * Return the total amount of time in (us) spent in this timer block
 *
 * @param timer fb_timer_t the timer object
 * @return double the total time spent in the block
 */
double fb_timer_get_total_time(fb_timer_t* timer);

/**
 * Return the current average of the sample
 *
 * @param timer fb_timer_t the timer object
 * @return double current average
 */
double fb_timer_get_avg(fb_timer_t* timer);


/**
 * Return the most recent minimum value for the timer for the past
 * N samples where N is specified by the window size above
 *
 * @param timer fb_timer_t the timer object
 * @return double the most recent minimum value
 */
double fb_timer_get_recent_min(fb_timer_t *timer);

/**
 * Return the moving window average for the best 1% samples. These samples
 * decay over time as specified by the 2^(decay power) above
 *
 * @param timer fb_timer_t the timer object
 * @return double average min for the best 1% of the samples
 */
double fb_timer_get_avg_min(fb_timer_t* timer);

/**
 * Return the absolute minimum value
 *
 * @param timer fb_timer_t the timer object
 * @return double absolute minimum sample
 * */
double fb_timer_get_abs_min(fb_timer_t *timer);

/**
 * fill out the given nstring array (which should be a
 * preallocated array of NUM_TIMER_OUTPUT_TYPES nstring objects)
 * with the timer values turned into strings.
 *
 * @param timer fb_timer_t* the timer object
 * @param vals nstring_t* the nstrings to fill out with the timer values
 */
void fb_timer_to_nstring(fb_timer_t* timer, nstring_t* vals);

/**
 * Start the timer
 *
 * @param timer fb_timer_t* the timer object
 * @return uint64_t the value returned from the cycle_timer()
 */
static inline uint64_t fb_timer_start(fb_timer_t* timer) {
  if (!timer) {
    return 0;
  }
  timer->start_time = cycle_timer_func();
  timer->status = TIMER_RUNNING;
  return timer->start_time;
}

/**
 * wrapper around the internal cycle timer
 *
 * @return uint64_t the current value of cycle_timer
 */
static inline uint64_t fb_timer_cycle_timer() {
  return cycle_timer_func();
}

static inline uint64_t
exponential_moving_avg(uint64_t avg, uint64_t x, int shift) {
  if (avg == 0) {
    return x;
  }
  return ((avg << shift) - avg + x) >> shift;
}

static inline uint64_t fb_record_time(uint64_t start, uint64_t finish,
                                      int countdown, int shift,
                                      uint64_t *sum, uint64_t *last_sum,
                                      uint64_t *avg, uint64_t *avg_peak,
                                      uint64_t *peak, uint64_t *avg_min,
                                      uint64_t *min, uint64_t *abs_min,
                                      uint64_t *idx) {

  // WallClockUsecFast is not guaranteed to return monotonic results,
  // which can cause finish < start on very short time intervals.
  // Ignore record if this happens.
  if (finish < start) {
    return start;
  }

  uint64_t delta = finish - start;
  *sum = *sum + delta;
  if (delta > *peak) {
    *peak = delta;
  }
  if (delta < *abs_min || *abs_min == 0) {
    *abs_min = delta;
  }
  if (delta < *min || *min == 0) {
    *min = delta;
  }

  *idx -= 1;
  if (*idx == 0) {
    *idx = countdown;
    *avg_peak = exponential_moving_avg(*avg_peak, *peak, shift);
    *peak = 0;
    uint64_t new_avg = (*sum - *last_sum) / countdown;
    *avg = exponential_moving_avg(*avg, new_avg, shift);
    *last_sum = *sum;
    *avg_min = exponential_moving_avg(*avg_min, *min, shift);
    *min = 0;
  }
  return finish;
}

/**
 * record the results for a timer object
 *
 * @param timer fb_timer_t* the timer object
 * @param uint64_t delta the difference between the start and finish times to
 *                       to record
 */
static inline void fb_timer_record_finish(fb_timer_t* timer, uint64_t start,
                                          uint64_t finish){
  if (!timer) {
    return;
  }

  fb_record_time(
    start, finish, timer->latency_countdown, timer->latency_avg_power,
    &timer->total_time, &timer->_last_total, &timer->avg, &timer->avg_peak,
    &timer->peak, &timer->avg_min, &timer->min, &timer->abs_min, &timer->idx);
}

/**
 * Stop the timer and record the results
 *
 * @param timer fb_timer_t* the timer object
 * @return uint64_t the value returned from the cycle_timer()
 */
static inline uint64_t fb_timer_finish(fb_timer_t* timer) {
  uint64_t finish = cycle_timer_func();
  if (!timer) {
    return 0;
  }
  timer->status = TIMER_STOPPED;
  fb_timer_record_finish(timer, timer->start_time, finish);
  return finish;
}

__END_DECLS

#endif
