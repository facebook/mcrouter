/*
 *  Copyright (c) 2015, Facebook, Inc.
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

#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/config.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyRequestPriority.h"
#include "mcrouter/ProxyRequestLogger.h"
#include "mcrouter/routes/McOpList.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

class McrouterClient;
class ProxyClientCommon;
class ProxyRoute;
class ShardSplitter;

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
  using ClientCallback = std::function<void(const ProxyClientCommon&)>;
  using ShardSplitCallback = std::function<void(const ShardSplitter&)>;

  /**
   * A request with this context will not be sent/logged anywhere.
   *
   * @param clientCallback  If non-nullptr, called by DestinationRoute when
   *   the request would normally be sent to destination;
   *   also in traverse() of DestinationRoute.
   * @param shardSplitCallback  If non-nullptr, called by ShardSplitRoute
   *   in traverse() with itself as the argument.
   */
  static std::shared_ptr<ProxyRequestContext> createRecording(
    proxy_t& proxy,
    ClientCallback clientCallback,
    ShardSplitCallback shardSplitCallback = nullptr);

  /**
   * Same as createRecording(), but also notifies the baton
   * when this context is destroyed (i.e. all requests referencing it
   * finish executing).
   */
  static std::shared_ptr<ProxyRequestContext> createRecordingNotify(
    proxy_t& proxy,
    folly::fibers::Baton& baton,
    ClientCallback clientCallback,
    ShardSplitCallback shardSplitCallback = nullptr);

  virtual ~ProxyRequestContext();

  proxy_t& proxy() const {
    return proxy_;
  }

  bool recording() const noexcept {
    return recording_;
  }

  void recordDestination(const ProxyClientCommon& destination) const {
    if (recording_ && recordingState_->clientCallback) {
      recordingState_->clientCallback(destination);
    }
  }

  void recordShardSplitter(const ShardSplitter& splitter) const {
    if (recording_ && recordingState_->shardSplitCallback) {
      recordingState_->shardSplitCallback(splitter);
    }
  }

  uint64_t senderId() const;

  void setSenderIdForTest(uint64_t id);

  ProxyRoute& proxyRoute() const {
    assert(!recording_);
    return config_->proxyRoute();
  }

  const ProxyConfig& proxyConfig() const {
    assert(!recording_);
    return *config_;
  }

  bool failoverDisabled() const {
    return failoverDisabled_;
  }

  ProxyRequestPriority priority() const {
    return priority_;
  }

  /**
   * Called once a reply is received to record a stats sample if required.
   */
  template <class Operation, class Request>
  void onReplyReceived(const ProxyClientCommon& pclient,
                       const Request& request,
                       const ReplyT<Operation, Request>& reply,
                       const int64_t startTimeUs,
                       const int64_t endTimeUs,
                       Operation) {
    if (recording_) {
      return;
    }

    assert(logger_.hasValue());
    logger_->log(request, reply, startTimeUs, endTimeUs, Operation());
    assert(additionalLogger_.hasValue());
    additionalLogger_->log(
      pclient, request, reply, startTimeUs, endTimeUs, Operation());
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

  /**
   * Returns the id of this requests.
   */
  uint64_t requestId() const;

 protected:
  bool replied_{false};
  std::shared_ptr<const ProxyConfig> config_;

  ProxyRequestContext(proxy_t& pr, ProxyRequestPriority priority__);

 private:
  const uint64_t requestId_;
  proxy_t& proxy_;
  bool failoverDisabled_{false};

  /** If true, this is currently being processed by a proxy and
      we want to notify we're done on destruction. */
  bool processing_{false};

  bool recording_{false};

  std::shared_ptr<McrouterClient> requester_;

  struct RecordingState {
    ClientCallback clientCallback;
    ShardSplitCallback shardSplitCallback;
  };

  union {
    void* context_{nullptr};
    std::unique_ptr<RecordingState> recordingState_;
  };

  /**
   * The function that will be called when all replies (including async)
   * come back.
   * Guaranteed to be called after enqueueReply_ (right after in sync mode).
   */
  void (*reqComplete_)(ProxyRequestContext& preq){nullptr};

  folly::Optional<ProxyRequestLogger> logger_;
  folly::Optional<AdditionalProxyRequestLogger> additionalLogger_;

  uint64_t senderIdForTest_{0};

  ProxyRequestPriority priority_{ProxyRequestPriority::kCritical};

  std::string userIpAddr_;

  enum RecordingT { Recording };
  ProxyRequestContext(
    RecordingT,
    proxy_t& pr,
    ClientCallback clientCallback,
    ShardSplitCallback shardSplitCallback);

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
  friend class McrouterClient;
  friend class proxy_t;
};

template <class Operation, class Request>
class ProxyRequestContextTyped : public ProxyRequestContext {
 public:
  using Type = ProxyRequestContextTyped<Operation, Request>;
  /**
   * Sends the reply for this proxy request.
   * @param newReply the message that we are sending out as the reply
   *   for the request we are currently handling
   */
  void sendReply(ReplyT<Operation, Request>&& reply);

  /**
   * Convenience method, that constructs reply and calls non-template
   * method.
   */
  template <class... Args>
  void sendReply(Args&&... args) {
    sendReply(ReplyT<Operation, Request>(std::forward<Args>(args)...));
  }

  virtual void startProcessing() override;

  /**
   * Internally converts the context into one ready to route.
   * Config pointer is saved to keep the config alive, and
   * ownership is changed to shared so that all subrequests
   * keep track of this context.
   */
  static std::shared_ptr<Type> process(
      std::unique_ptr<Type> preq, std::shared_ptr<const ProxyConfig> config);

 protected:
  ProxyRequestContextTyped(proxy_t& pr,
                           const Request& req,
                           ProxyRequestPriority priority__)
      : ProxyRequestContext(pr, priority__), req_(&req) {}

  virtual void sendReplyImpl(ReplyT<Operation, Request>&& reply) = 0;

  // It's guaranteed to point to an existing request until we call user callback
  // (i.e. replied_ changes to true), after that it's nullptr.
  const Request* req_;
};

/**
 * Creates a new proxy request context
 */
template <class Operation, class Request, class F>
std::unique_ptr<ProxyRequestContextTyped<Operation, Request>>
createProxyRequestContext(
    proxy_t& pr,
    const Request& req,
    Operation,
    F&& f,
    ProxyRequestPriority priority = ProxyRequestPriority::kCritical);

/**
 * Creates proxy request context along with the request itself.
 * NOTE: this is a temporary method to support old McrouterClient interface.
 */
template <class F>
std::unique_ptr<ProxyRequestContext> createLegacyProxyRequestContext(
    proxy_t& pr,
    McMsgRef req,
    F&& f,
    ProxyRequestPriority priority = ProxyRequestPriority::kCritical);
}}}  // facebook::memcache::mcrouter

#include "ProxyRequestContext-inl.h"
