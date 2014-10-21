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
  explicit DestinationClient(std::shared_ptr<ProxyDestination> pdstn);

  void resetInactive();

  size_t getPendingRequestCount() const;
  size_t getInflightRequestCount() const;

  /**
   * Get average request batch size that is sent over network in one write.
   *
   * See AsyncMcClient::getBatchingStat() for more details.
   */
  std::pair<uint64_t, uint64_t> getBatchingStat() const;

  template <int Op>
  int send(const McRequest& request, McOperation<Op>, void* req_ctx,
           uint64_t senderId);

  ~DestinationClient();

 private:
  proxy_t* proxy_;
  std::unique_ptr<AsyncMcClient> asyncMcClient_;
  std::weak_ptr<ProxyDestination> pdstn_;

  AsyncMcClient& getAsyncMcClient();
  void initializeAsyncMcClient();

  // Callback that would forward reply to the ProxyDestination.
  static void onReply(McReply reply, mc_op_t op, void* req_ctx,
                      std::weak_ptr<ProxyDestination> pdstn);
};

}}}  // facebook::memcache::mcrouter

#include "DestinationClient-inl.h"
