/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <string>
#include <vector>

#include <folly/Optional.h>

#include "mcrouter/lib/fibers/FiberPromise.h"
#include "mcrouter/lib/McRequestWithContext.h"
#include "mcrouter/lib/Operation.h"

namespace facebook { namespace memcache {

class McReply;

namespace mcrouter {

class ProxyClientCommon;
class ShardSplitter;

/**
 * With this context, the requests are not actually sent out
 * over the network, we only record where the requests would be sent.
 */
class RecordingContext {
 public:
  typedef std::function<void(const ProxyClientCommon&)> ClientCallback;
  typedef std::function<void(const ShardSplitter&)> ShardSplitCallback;

  explicit RecordingContext(ClientCallback clientCallback,
                            ShardSplitCallback shardSplitCallback = nullptr);

  ~RecordingContext();

  void recordShardSplitter(const ShardSplitter& shardSplitter);

  void recordDestination(const ProxyClientCommon& destination);

  /**
   * Waits until all owners (i.e. requests) of ctx expire on other fibers
   */
  static void waitForRecorded(std::shared_ptr<RecordingContext>&& ctx);

 private:
  ClientCallback clientCallback_;
  ShardSplitCallback shardSplitCallback_;
  folly::Optional<FiberPromise<void>> promise_;
};

typedef McRequestWithContext<RecordingContext> RecordingMcRequest;

} // mcrouter

template <typename Operation>
struct ReplyType<Operation, mcrouter::RecordingMcRequest> {
  typedef McReply type;
};

}}  // facebook::memcache
