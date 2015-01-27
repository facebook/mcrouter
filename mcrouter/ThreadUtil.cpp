/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ThreadUtil.h"

#include <sys/capability.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <folly/Format.h>
#include <folly/ThreadName.h>

#include "mcrouter/options.h"

namespace facebook { namespace memcache { namespace mcrouter {

void mcrouterSetThreadName(pthread_t tid,
                           const McrouterOptions& opts,
                           folly::StringPiece prefix) {
  auto name = folly::format("{}-{}", prefix, opts.router_name).str();
  if (!folly::setThreadName(tid, name)) {
    LOG(WARNING) << "Unable to set thread name to " << name;
  }
}

bool spawnThread(pthread_t* thread_handle, void** stack,
                 void* (thread_run)(void*), void* arg, int realtime) {
  /* Default thread stack size if RLIMIT_STACK is unlimited */
  static constexpr size_t DEFAULT_STACK_SIZE = 8192 * 1024;

  pthread_attr_t attr;
  pthread_attr_init(&attr);

  struct rlimit rlim;
  getrlimit(RLIMIT_STACK, &rlim);
  size_t stack_sz =
    rlim.rlim_cur == RLIM_INFINITY ? DEFAULT_STACK_SIZE : rlim.rlim_cur;
  PCHECK(posix_memalign(stack, 8, stack_sz) == 0);
  PCHECK(pthread_attr_setstack(&attr, *stack, stack_sz) == 0);

  if (realtime) {
    struct sched_param sched_param;
    cap_t cap_p = cap_get_proc();
    cap_flag_value_t cap_val;
    cap_get_flag(cap_p, CAP_SYS_NICE, CAP_EFFECTIVE, &cap_val);
    if (cap_val == CAP_SET) {
      sched_param.sched_priority = DEFAULT_REALTIME_PRIORITY_LEVEL;
      pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
      pthread_attr_setschedparam(&attr, &sched_param);
      pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }
    cap_free(cap_p);
  }

  int rc = pthread_create(thread_handle, &attr, thread_run, arg);
  pthread_attr_destroy(&attr);

  if (rc != 0) {
    *thread_handle = 0;
    LOG(ERROR) << "CRITICAL: Failed to create thread";
    return false;
  }

  return true;
}

}}} // facebook::memcache::mcrouter
