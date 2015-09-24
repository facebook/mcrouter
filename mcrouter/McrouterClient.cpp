/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McrouterClient.h"

#include "mcrouter/lib/MessageQueue.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyRequestContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

size_t McrouterClient::send(
  const mcrouter_msg_t* requests,
  size_t nreqs,
  folly::StringPiece ipAddr /* = folly::StringPiece() */ ) {

  assert(!disconnected_);

  auto router = router_.lock();
  if (nreqs == 0 || !router) {
    return 0;
  }

  auto makePreq = [this, &requests, ipAddr](size_t i) {
    auto cb =
      [this, context = requests[i].context,
       req = McMsgRef::cloneRef(requests[i].req)]
      (ProxyRequestContext&, McReply&& reply) mutable {
        this->onReply(std::move(reply), std::move(req), context);
      };
    auto preq = createLegacyProxyRequestContext(
        *proxy_, McMsgRef::cloneRef(requests[i].req), std::move(cb));
    preq->requester_ = self_;

    if (!ipAddr.empty()) {
      preq->setUserIpAddress(ipAddr);
    }
    return preq;
  };

  if (sameThread_) {
    /*
     * Skip the extra queue hop and directly call the queue callback,
     * since we're staying in the same thread.
     *
     * Note: maxOutstanding_ is ignored in this case.
     */
    for (size_t i = 0; i < nreqs; i++) {
      sendSameThread(makePreq(i));
    }
  } else if (maxOutstanding_ == 0) {
    for (size_t i = 0; i < nreqs; i++) {
      sendRemoteThread(makePreq(i));
    }
  } else {
    size_t i = 0;
    size_t n = 0;

    while (i < nreqs) {
      n += counting_sem_lazy_wait(&outstandingReqsSem_, nreqs - n);
      for (size_t j = i; j < n; ++j) {
        sendRemoteThread(makePreq(j));
      }

      i = n;
    }
  }

  return nreqs;
}

void McrouterClient::sendRemoteThread(
    std::unique_ptr<ProxyRequestContext> req) {
  proxy_->messageQueue_->blockingWriteRelaxed(ProxyMessage::Type::REQUEST,
                                              req.release());
}

void McrouterClient::sendSameThread(std::unique_ptr<ProxyRequestContext> req) {
  proxy_->messageReady(ProxyMessage::Type::REQUEST, req.release());
}

McrouterClient::McrouterClient(
  std::weak_ptr<McrouterInstance> rtr,
  mcrouter_client_callbacks_t callbacks,
  void* arg,
  size_t maxOutstanding,
  bool sameThread) :
    router_(std::move(rtr)),
    sameThread_(sameThread),
    callbacks_(callbacks),
    arg_(arg),
    maxOutstanding_(maxOutstanding) {

  static std::atomic<uint64_t> nextClientId(0ULL);
  clientId_ = nextClientId++;

  if (maxOutstanding_ != 0) {
    counting_sem_init(&outstandingReqsSem_, maxOutstanding_);
  }

  if (auto router = router_.lock()) {
    std::lock_guard<std::mutex> guard(router->nextProxyMutex_);
    assert(router->nextProxy_ < router->opts().num_proxies);
    proxy_ = router->getProxy(router->nextProxy_);
    router->nextProxy_ =
      (router->nextProxy_ + 1) % router->opts().num_proxies;
  }
}

McrouterClient::Pointer McrouterClient::create(
  std::weak_ptr<McrouterInstance> router,
  mcrouter_client_callbacks_t callbacks,
  void* arg,
  size_t maxOutstanding,
  bool sameThread) {

  auto client = new McrouterClient(std::move(router),
                                   callbacks,
                                   arg,
                                   maxOutstanding,
                                   sameThread);
  client->self_ = std::shared_ptr<McrouterClient>(client);
  return Pointer(client);
}

void McrouterClient::onReply(McReply&& reply, McMsgRef&& req, void* context) {
  mcrouter_msg_t router_reply;

  // Don't increment refcounts, because these are transient stack
  // references, and are guaranteed to be shorted lived than router_entry's
  // reference.  This is a premature optimization.
  router_reply.req = const_cast<mc_msg_t*>(req.get());
  router_reply.reply = std::move(reply);
  router_reply.context = context;

  auto replyBytes = router_reply.reply.value().computeChainDataLength();

  switch (req->op) {
    case mc_op_get:
    case mc_op_gets:
    case mc_op_lease_get:
      stats_.recordFetchRequest(req->key.len, replyBytes);
      break;

    case mc_op_add:
    case mc_op_set:
    case mc_op_replace:
    case mc_op_lease_set:
    case mc_op_cas:
    case mc_op_append:
    case mc_op_prepend:
    case mc_op_incr:
    case mc_op_decr:
      stats_.recordUpdateRequest(req->key.len, req->value.len);
      break;

    case mc_op_delete:
      stats_.recordInvalidateRequest(req->key.len);
      break;

    case mc_op_unknown:
    case mc_op_echo:
    case mc_op_quit:
    case mc_op_version:
    case mc_op_servererr:
    case mc_op_flushall:
    case mc_op_flushre:
    case mc_op_stats:
    case mc_op_verbosity:
    case mc_op_shutdown:
    case mc_op_end:
    case mc_op_metaget:
    case mc_op_exec:
    case mc_op_get_service_info:
    case mc_nops:
      break;
  }

  if (LIKELY(callbacks_.on_reply && !disconnected_)) {
    callbacks_.on_reply(&router_reply, arg_);
  } else if (callbacks_.on_cancel && disconnected_) {
    // This should be called for all canceled requests, when cancellation is
    // implemented properly.
    callbacks_.on_cancel(context, arg_);
  }
}

McrouterClient::~McrouterClient() {
  assert(disconnected_);
  if (callbacks_.on_disconnect) {
    callbacks_.on_disconnect(arg_);
  }
}

}}}  // facebook::memcache::mcrouter
