/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <condition_variable>
#include <mutex>

#include "mcrouter/lib/fbi/queue.h"

namespace facebook { namespace memcache { namespace mcrouter {

// Forward declaration.
struct awriter_entry_t;

struct awriter_callbacks_t {
  void (*completed)(awriter_entry_t*, int);
  int (*perform_write)(awriter_entry_t*);
};

struct awriter_entry_t {
  TAILQ_ENTRY(awriter_entry_t) links;
  void *context;
  const awriter_callbacks_t *callbacks;
};

struct awriter_t {
  std::mutex lock;
  std::condition_variable cond;
  unsigned qsize;
  unsigned qlimit;
  int is_active;
  TAILQ_HEAD(awriter_entry_head, awriter_entry_t) entries;

  explicit awriter_t(unsigned limit);
  ~awriter_t();
};

void awriter_stop(awriter_t *w);
int awriter_queue(awriter_t *w, awriter_entry_t *e);

void *awriter_thread_run(void *arg);

}}} // facebook::memcache::mcrouter
