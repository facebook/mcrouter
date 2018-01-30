/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/fbi/queue.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

// Forward declaration.
struct awriter_entry_t;

struct awriter_callbacks_t {
  void (*completed)(awriter_entry_t*, int);
  int (*perform_write)(awriter_entry_t*);
};

struct awriter_entry_t {
  TAILQ_ENTRY(awriter_entry_t) links;
  void* context;
  const awriter_callbacks_t* callbacks;
};

} // mcrouter
} // memcache
} // facebook
