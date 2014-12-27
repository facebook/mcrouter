/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "RecordingContext.h"

#include <folly/IPAddress.h>
#include <folly/Memory.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/routes/ShardSplitter.h"

namespace facebook { namespace memcache { namespace mcrouter {

RecordingContext::RecordingContext(ClientCallback clientCallback,
                                   ShardSplitCallback shardSplitCallback)
    : clientCallback_(std::move(clientCallback)),
      shardSplitCallback_(std::move(shardSplitCallback)) {
}

RecordingContext::~RecordingContext() {
  if (promise_) {
    promise_->setValue();
  }
}

void RecordingContext::recordShardSplitter(const ShardSplitter& splitter) {
  if (shardSplitCallback_) {
    shardSplitCallback_(splitter);
  }
}

void RecordingContext::recordDestination(const ProxyClientCommon& destination) {
  if (clientCallback_) {
    clientCallback_(destination);
  }
}

void RecordingContext::waitForRecorded(
  std::shared_ptr<RecordingContext>&& ctx) {

  if (ctx.unique()) {
    /* This was the last reference, nothing to do */
    return;
  }

  /* Make sure we get notified on destruction */
  fiber::await([&ctx](FiberPromise<void> promise) {
      ctx->promise_ = std::move(promise);

      /* Surrender the reference we hold */
      ctx.reset();
    });
}

}}}
