/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <string>

#include <folly/Range.h>
#include <folly/fibers/FiberManager.h>

#include "mcrouter/ProxyRequestPriority.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook {
namespace memcache {

struct AccessPoint;

namespace mcrouter {

template <class RouterInfo>
class Proxy;
template <class RouterInfo>
class ProxyRoute;

class ProxyBase;
class CarbonRouterClientBase;
class ShardSplitter;

struct PoolContext {
  folly::StringPiece poolName;
  size_t indexInPool;
  bool isShadow;
};

/**
 * This object is alive for the duration of user's request,
 * including any subrequests that might have been sent out.
 *
 * It starts it's life under a unique_ptr outside of proxy threads.
 * When handed off to a proxy thread and ready to execute,
 * we save the current configuration and convert it to shared
 * ownership.
 *
 * Records collected stats on destruction.
 */
class ProxyRequestContext {
 public:
  using ClientCallback =
      std::function<void(const PoolContext&, const AccessPoint&)>;
  using ShardSplitCallback = std::function<void(const ShardSplitter&)>;

  virtual ~ProxyRequestContext();

  ProxyBase& proxy() const {
    return proxyBase_;
  }

  bool recording() const noexcept {
    return recording_;
  }

  void recordDestination(const PoolContext& poolContext, const AccessPoint& ap)
      const {
    if (recording_ && recordingState_->clientCallback) {
      recordingState_->clientCallback(poolContext, ap);
    }
  }

  void recordShardSplitter(const ShardSplitter& splitter) const {
    if (recording_ && recordingState_->shardSplitCallback) {
      recordingState_->shardSplitCallback(splitter);
    }
  }

  uint64_t senderId() const;

  void setSenderIdForTest(uint64_t id);

  bool failoverDisabled() const {
    return failoverDisabled_;
  }

  ProxyRequestPriority priority() const {
    return priority_;
  }

  /**
   * Continues processing current request.
   * Should be called only from the attached proxy thread.
   */
  virtual void startProcessing() {
    throw std::logic_error(
        "Calling startProcessing on an incomplete instance "
        "of ProxyRequestContext");
  }

  const std::string& userIpAddress() const noexcept {
    return userIpAddr_;
  }

  void setUserIpAddress(folly::StringPiece newAddr) noexcept {
    userIpAddr_ = newAddr.str();
  }

  bool isProcessing() const {
    return processing_;
  }
  void markAsProcessing() {
    processing_ = true;
  }

  void setRequester(std::shared_ptr<CarbonRouterClientBase> requester) {
    requester_ = std::move(requester);
  }

  void setFinalResult(mc_res_t result) {
    finalResult_ = result;
  }

  mc_res_t finalResult() const {
    return finalResult_;
  }

 protected:
  /**
   * The function that will be called when all replies (including async)
   * come back.
   * Guaranteed to be called after enqueueReply_ (right after in sync mode).
   */
  void (*reqComplete_)(ProxyRequestContext& preq){nullptr};
  mc_res_t finalResult_{mc_res_unknown};
  bool replied_{false};

  ProxyRequestContext(ProxyBase& pr, ProxyRequestPriority priority__);

  enum RecordingT { Recording };
  ProxyRequestContext(
      RecordingT,
      ProxyBase& pr,
      ClientCallback clientCallback,
      ShardSplitCallback shardSplitCallback);

 private:
  ProxyBase& proxyBase_;

  std::shared_ptr<CarbonRouterClientBase> requester_;

  struct RecordingState {
    ClientCallback clientCallback;
    ShardSplitCallback shardSplitCallback;
  };

  union {
    void* context_{nullptr};
    std::unique_ptr<RecordingState> recordingState_;
  };

  uint64_t senderIdForTest_{0};

  std::string userIpAddr_;

  ProxyRequestPriority priority_{ProxyRequestPriority::kCritical};

  bool failoverDisabled_{false};
  /** If true, this is currently being processed by a proxy and
      we want to notify we're done on destruction. */
  bool processing_{false};
  bool recording_{false};

  ProxyRequestContext(const ProxyRequestContext&) = delete;
  ProxyRequestContext(ProxyRequestContext&&) noexcept = delete;
  ProxyRequestContext& operator=(const ProxyRequestContext&) = delete;
  ProxyRequestContext& operator=(ProxyRequestContext&&) = delete;

 public:
  /* Do not use for new code */
  class LegacyPrivateAccessor {
   public:
    using ReqCompleteFunc = void (*)(ProxyRequestContext&);

    static ReqCompleteFunc& reqComplete(ProxyRequestContext& preq) {
      return preq.reqComplete_;
    }

    static void*& context(ProxyRequestContext& preq) {
      assert(!preq.recording_);
      return preq.context_;
    }

    static bool& failoverDisabled(ProxyRequestContext& preq) {
      return preq.failoverDisabled_;
    }
  };

 private:
  friend class ProxyBase;
};

} // mcrouter
} // memcache
} // facebook
