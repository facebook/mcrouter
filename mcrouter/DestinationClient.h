/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>

#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McOperation.h"

namespace facebook { namespace memcache {

class AsyncMcClient;
class McRequest;

namespace mcrouter {

class ProxyDestination;
class proxy_t;

class DestinationClient {
 public:
  explicit DestinationClient(ProxyDestination& pdstn);

  void resetInactive();

  size_t getPendingRequestCount() const;
  size_t getInflightRequestCount() const;

  /**
   * Get average request batch size that is sent over network in one write.
   *
   * See AsyncMcClient::getBatchingStat() for more details.
   */
  std::pair<uint64_t, uint64_t> getBatchingStat() const;

  template <int Op, class Request>
  typename ReplyType<McOperation<Op>, Request>::type
  send(const Request& request, McOperation<Op>, uint64_t senderId);

  ~DestinationClient();

 private:
  proxy_t* proxy_;
  std::unique_ptr<AsyncMcClient> asyncMcClient_;
  ProxyDestination& pdstn_;

  AsyncMcClient& getAsyncMcClient();
  void initializeAsyncMcClient();
  void updateStats(mc_res_t result, mc_op_t op);
};

}}}  // facebook::memcache::mcrouter

#include "DestinationClient-inl.h"
