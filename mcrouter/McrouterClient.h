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

#include <folly/detail/CacheLocality.h>
#include <folly/IntrusiveList.h>
#include <folly/Optional.h>
#include <folly/Range.h>

#include "mcrouter/lib/CacheClientStats.h"
#include "mcrouter/lib/fbi/counting_sem.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"

class asox_queue_s;
using asox_queue_t = asox_queue_s*;
class asox_queue_entry_s;
using asox_queue_entry_t = asox_queue_entry_s;

namespace folly {
class EventBase;
}

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterClient;
class McrouterInstance;
class proxy_t;
class ProxyMessage;
class ProxyRequestContext;

struct mcrouter_msg_t {
  mc_msg_t* req;
  McReply reply{mc_res_unknown};
  void* context;
  folly::Optional<McRequest> saved_request;
};

typedef void (mcrouter_on_reply_t)(mcrouter_msg_t* router_req,
                                   void* client_context);

typedef void (mcrouter_on_cancel_t)(void* request_context,
                                    void* client_context);

typedef void (mcrouter_on_disconnect_ts_t)(void* client_context);

struct mcrouter_client_callbacks_t {
  mcrouter_on_reply_t* on_reply;
  mcrouter_on_cancel_t* on_cancel;
  mcrouter_on_disconnect_ts_t* on_disconnect;
};

/**
 * A mcrouter client is used to communicate with a mcrouter instance.
 * Typically a client is long lived. Request sent through a single client
 * will be sent to the same mcrouter thread that's determined once on creation.
 *
 * Create via McrouterInstance::createClient().
 */
class McrouterClient {
 private:
  struct Disconnecter {
    void operator() (McrouterClient* client) {
      client->disconnect();
    }
  };
  folly::IntrusiveListHook hook_;

 public:
  using Pointer = std::unique_ptr<McrouterClient, Disconnecter>;
  using Queue = folly::IntrusiveList<McrouterClient,
                                     &McrouterClient::hook_>;

  /**
   * Asynchronously send `nreqs' requests from the array started at `requests'.
   * Optionally, `ipAddr` is a StringPiece that contains the IP address of the
   * external client we got the requests from.
   *
   * @returns number of requests successfully sent
   */
  size_t send(
    const mcrouter_msg_t* requests,
    size_t nreqs,
    folly::StringPiece ipAddr = folly::StringPiece()
  );

  /**
   * Returns the mcrouter managed event base that runs the callbacks.
   * Returns nullptr in sync or standalone mode, as mcrouter doesn't create
   * its own event bases.
   */
  folly::EventBase* getBase() const;

  /**
   * Change the context passed back to the callbacks.
   */
  void setClientContext(void* client_context) {
    arg_ = client_context;
  }

  CacheClientCounters getStatCounters() noexcept {
    return stats_.getCounters();
  }

  /**
   * If true, the underlying mcrouter instance went away, so the client
   * is a `zombie' and no requests can be sent.
   */
  bool isZombie() const {
    return isZombie_;
  }

  /**
   * Unique client id. Ids are not re-used for the lifetime of the process.
   */
  uint64_t clientId() const {
    return clientId_;
  }

  /**
   * Override default proxy assignment.
   */
  void setProxy(proxy_t* proxy) {
    proxy_ = proxy;
  }

  McrouterClient(const McrouterClient&) = delete;
  McrouterClient(McrouterClient&&) noexcept = delete;
  McrouterClient& operator=(const McrouterClient&) = delete;
  McrouterClient& operator=(McrouterClient&&) = delete;

 private:
  McrouterInstance* router_;

  mcrouter_client_callbacks_t callbacks_;
  void* arg_;

  proxy_t* proxy_{nullptr};

  CacheClientStats stats_;

  /// Maximum allowed requests in flight (unlimited if 0)
  const unsigned int maxOutstanding_;
  counting_sem_t outstandingReqsSem_;

  // whether the routing thread has received disconnect notification.
  bool disconnected_{false};

  // only updated by mcrouter thread, so we don't need any fancy atomic refcount
  int numPending_{0};

  // If true implies that the underlying mcrouter has already been
  // freed. A zombie client can not serve any more requests.
  std::atomic<bool> isZombie_{false};

  /**
   * Automatically-assigned client id, used for QOS for different clients
   * sharing the same connection.
   */
  uint64_t clientId_;

  std::atomic<size_t> FOLLY_ALIGN_TO_AVOID_FALSE_SHARING refcount_{1};

  McrouterClient(
    McrouterInstance* router,
    mcrouter_client_callbacks_t callbacks,
    void *arg,
    size_t maximum_outstanding);

  void onReply(ProxyRequestContext& preq);
  void disconnect();
  void performDisconnect();
  void cleanup();
  McrouterClient* incref();
  void decref();

 private:
  friend class McrouterInstance;
  friend class proxy_t;
  friend class ProxyRequestContext;
};

}}} // facebook::memcache::mcrouter
