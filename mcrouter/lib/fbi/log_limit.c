#include "log_limit.h"

#include <stdlib.h>

static int cycle = 0;
static struct timeval *queue = NULL;
static int head = 0, tail = 0, count = 0;

int set_log_limit(int max_log_num, int cycle_sec) {
  if (max_log_num <= 0 || cycle_sec <= 0) { // invalid parameter
    return -1;
  }

  cycle = cycle_sec;
  head = 0;
  tail = 0;
  count = max_log_num + 1;

  if(queue != NULL) {
    free(queue);
  }
  queue = malloc(sizeof(struct timeval) * count);
  if(queue == NULL) {
    return -1;
  }

  return 0;
}

int check_log_limit(const struct timeval *now) {
  if (cycle == 0 || queue == NULL) { // no limit
    return 0;
  }

  struct timeval one_cycle_ago = *now;
  one_cycle_ago.tv_sec -= cycle;

  while (head != tail) {
    if (queue[head].tv_sec > one_cycle_ago.tv_sec) {
      break;
    }
    if (queue[head].tv_sec == one_cycle_ago.tv_sec &&
        queue[head].tv_usec > one_cycle_ago.tv_usec) {
      break;
    }

    head = (head + 1) % count;
  }

  if ((tail + 1) % count == head) { // hit limit
    return 1;
  }

  queue[tail] = *now;
  tail = (tail + 1) % count;
  return 0;
}
