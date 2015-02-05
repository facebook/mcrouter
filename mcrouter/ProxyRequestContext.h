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

#include "mcrouter/config.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/ProxyConfigIf.h"
#include "mcrouter/ProxyRequestLogger.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyClientCommon;
class ProxyMcReply;
class ProxyMcRequest;
class ProxyRoute;

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
  /**
   * Creates a new context
   */
  template <typename... Args>
  static std::unique_ptr<ProxyRequestContext> create(Args&&... args) {
    return std::unique_ptr<ProxyRequestContext>(
      new ProxyRequestContext(std::forward<Args>(args)...));
  }

  /**
   * Internally converts the context into one ready to route.
   * Config pointer is saved to keep the config alive, and
   * ownership is changed to shared so that all subrequests
   * keep track of this context.
   */
  static std::shared_ptr<ProxyRequestContext> process(
    std::unique_ptr<ProxyRequestContext> preq,
    std::shared_ptr<const ProxyConfigIf> config) {
    preq->config_ = std::move(config);
    return std::shared_ptr<ProxyRequestContext>(
      preq.release(),
      /* Note: we want to delete on main context here since the destructor
         can do complicated things, like finalize stats entry and
         destroy a stale config.  There might not be enough stack space
         for these operations. */
      [] (ProxyRequestContext* ctx) {
        fiber::runInMainContext([ctx]{ delete ctx; });
      });
  }

  ~ProxyRequestContext();

  proxy_t& proxy() const {
    return proxy_;
  }

  uint64_t senderId() const;

  ProxyRoute& proxyRoute() const {
    return config_->proxyRoute();
  }

  bool failoverDisabled() const {
    return failoverDisabled_;
  }

  /**
   * Called once a reply is received to record a stats sample if required.
   */
  template <typename Operation>
  void onReplyReceived(const ProxyClientCommon& pclient,
                       const ProxyMcRequest& request,
                       const ProxyMcReply& reply,
                       const int64_t startTimeUs,
                       const int64_t endTimeUs,
                       Operation) {
    logger_.log(pclient, request, reply, startTimeUs, endTimeUs, Operation());
    additionalLogger_.log(
      pclient, request, reply, startTimeUs, endTimeUs, Operation());
  }

  const McMsgRef& origReq() const {
    return origReq_;
  }

  /**
   * Sets the reply for this proxy request and sends it out
   * @param newReply the message that we are sending out as the reply
   *   for the request we are currently handling
   */
  void sendReply(McReply newReply);

 private:
  proxy_t& proxy_;
  McMsgRef origReq_;
  folly::Optional<McReply> reply_;
  folly::Optional<McRequest> savedRequest_;
  bool replied_{false};
  bool failoverDisabled_{false};

  /** If true, this is currently being processed by a proxy and
      we want to notify we're done on destruction. */
  bool processing_{false};

  McrouterClient* requester_{nullptr};
  void* context_{nullptr};

  /**
   * The function that will be called when the reply is ready
   */
  void (*enqueueReply_)(ProxyRequestContext& preq){nullptr};

  /**
   * The function that will be called when all replies (including async)
   * come back.
   * Guaranteed to be called after enqueueReply_ (right after in sync mode).
   */
  void (*reqComplete_)(ProxyRequestContext& preq){nullptr};

  std::shared_ptr<const ProxyConfigIf> config_;

  ProxyRequestLogger logger_;
  AdditionalProxyRequestLogger additionalLogger_;

  ProxyRequestContext(
    proxy_t& pr,
    McMsgRef req,
    void (*enqReply)(ProxyRequestContext& preq),
    void* context,
    void (*reqComplete)(ProxyRequestContext& preq) = nullptr);

  ProxyRequestContext(const ProxyRequestContext&) = delete;
  ProxyRequestContext(ProxyRequestContext&&) noexcept = delete;
  ProxyRequestContext& operator=(const ProxyRequestContext&) = delete;
  ProxyRequestContext& operator=(ProxyRequestContext&&) = delete;

 public:
  /* Do not use for new code */
  class LegacyPrivateAccessor {
   public:
    static void*& context(ProxyRequestContext& preq) {
      return preq.context_;
    }

    static bool& failoverDisabled(ProxyRequestContext& preq) {
      return preq.failoverDisabled_;
    }

    static McReply& reply(ProxyRequestContext& preq) {
      return preq.reply_.value();
    }
  };

private:
  friend class McrouterClient;
  friend class proxy_t;
};

}}}  // facebook::memcache::mcrouter
