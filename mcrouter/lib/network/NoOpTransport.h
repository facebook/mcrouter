/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#pragma once

#include <chrono>

#include <folly/Range.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/VirtualEventBase.h>

#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/Transport.h"

namespace facebook {
namespace memcache {

/**
 * A simple NoOp transport.
 * It always returns the default reply, without ever sending it to the server.
 */
class NoOpTransport : public Transport {
 public:
  NoOpTransport(
      folly::VirtualEventBase& /* eventBase */,
      ConnectionOptions /* options */) {}
  ~NoOpTransport() override final = default;

  static constexpr folly::StringPiece name() {
    return "NoOpTransport";
  }

  static constexpr bool isCompatible(mc_protocol_t protocol) {
    return protocol == mc_noop_protocol;
  }

  void closeNow() override final {}

  void setConnectionStatusCallbacks(
      ConnectionStatusCallbacks /* callbacks */) override final {}

  void setRequestStatusCallbacks(
      RequestStatusCallbacks /* callbacks */) override final {}

  void setThrottle(size_t /* maxInflight */, size_t /* maxPending */)
      override final {}

  RequestQueueStats getRequestQueueStats() const override final {
    return RequestQueueStats{0, 0};
  }

  void updateTimeoutsIfShorter(
      std::chrono::milliseconds /* connectTimeout */,
      std::chrono::milliseconds /* writeTimeout */) override final {}

  const folly::AsyncTransportWrapper* getTransport() const override final {
    return nullptr;
  }

  double getRetransmitsPerKb() override final {
    return 0.0;
  }

  void setFlushList(FlushList* /* flushList */) override final {}

  template <class Request>
  ReplyT<Request> sendSync(
      const Request& request,
      std::chrono::milliseconds /* timeout */,
      RpcStatsContext* /* rpcStats */ = nullptr) {
    return createReply(DefaultReply, request);
  }
};

} // namespace memcache
} // namespace facebook
