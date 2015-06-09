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

#include "mcrouter/lib/fbi/asox_queue.h"
#include "mcrouter/lib/MessageQueue.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyRequestContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

size_t McrouterClient::send(
  const mcrouter_msg_t* requests,
  size_t nreqs,
  folly::StringPiece ipAddr /* = folly::StringPiece() */ ) {

  if (nreqs == 0) {
    return 0;
  }
  assert(!isZombie_);

  auto makePreq = [this, &requests, ipAddr] (size_t i) {
    auto preq = ProxyRequestContext::create(
      *proxy_,
      McMsgRef::cloneRef(requests[i].req),
      [] (ProxyRequestContext& prq) {
        prq.requester_->onReply(prq);
      },
      requests[i].context);
    preq->requester_ = incref();
    if (!ipAddr.empty()) {
      preq->setUserIpAddress(ipAddr);
    }
    if (requests[i].saved_request.hasValue()) {
      // TODO: remove copy
      preq->savedRequest_.emplace(requests[i].saved_request->clone());
    }
    return preq;
  };

  if (router_->opts().standalone) {
    /*
     * Skip the extra queue hop and directly call the queue callback,
     * since we're standalone and thus staying in the same thread.
     *
     * Note: maxOutstanding_ is ignored in this case.
     */
    for (size_t i = 0; i < nreqs; i++) {
      auto preq = makePreq(i);
      proxy_->messageReady(ProxyMessage::Type::REQUEST, preq.release());
    }
  } else if (maxOutstanding_ == 0) {
    for (size_t i = 0; i < nreqs; i++) {
      auto preq = makePreq(i);
      proxy_->messageQueue_->blockingWriteRelaxed(ProxyMessage::Type::REQUEST,
                                                  preq.release());
    }
  } else {
    size_t i = 0;
    size_t n = 0;

    while (i < nreqs) {
      n += counting_sem_lazy_wait(&outstandingReqsSem_, nreqs - n);
      for (size_t j = i; j < n; ++j) {
        auto preq = makePreq(j);
        proxy_->messageQueue_->blockingWriteRelaxed(ProxyMessage::Type::REQUEST,
                                                    preq.release());
      }

      i = n;
    }
  }

  return nreqs;
}

folly::EventBase* McrouterClient::getBase() const {
  if (router_->opts().standalone) {
    return proxy_->eventBase;
  } else {
    return nullptr;
  }
}

McrouterClient::McrouterClient(
  McrouterInstance* router,
  mcrouter_client_callbacks_t callbacks,
  void* arg,
  size_t maxOutstanding) :
    router_(router),
    callbacks_(callbacks),
    arg_(arg),
    maxOutstanding_(maxOutstanding) {

  static std::atomic<uint64_t> nextClientId(0ULL);
  clientId_ = nextClientId++;

  if (maxOutstanding_ != 0) {
    counting_sem_init(&outstandingReqsSem_, maxOutstanding_);
  }

  {
    std::lock_guard<std::mutex> guard(router_->clientListLock_);
    router_->clientList_.push_front(*this);
  }

  {
    std::lock_guard<std::mutex> guard(router_->nextProxyMutex_);
    assert(router_->nextProxy_ < router_->opts().num_proxies);
    proxy_ = router_->getProxy(router_->nextProxy_);
    router_->nextProxy_ =
      (router_->nextProxy_ + 1) % router_->opts().num_proxies;
  }
}

void McrouterClient::onReply(ProxyRequestContext& preq) {
  mcrouter_msg_t router_reply;

  // Don't increment refcounts, because these are transient stack
  // references, and are guaranteed to be shorted lived than router_entry's
  // reference.  This is a premature optimization.
  router_reply.req = const_cast<mc_msg_t*>(preq.origReq().get());
  router_reply.reply = std::move(preq.reply_.value());
  router_reply.context = preq.context_;

  auto replyBytes = router_reply.reply.value().computeChainDataLength();

  switch (preq.origReq()->op) {
    case mc_op_get:
    case mc_op_gets:
    case mc_op_lease_get:
      stats_.recordFetchRequest(preq.origReq()->key.len, replyBytes);
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
      stats_.recordUpdateRequest(preq.origReq()->key.len,
                                 preq.origReq()->value.len);
      break;

    case mc_op_delete:
      stats_.recordInvalidateRequest(preq.origReq()->key.len);
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
    callbacks_.on_cancel(preq.context_, arg_);
  }

  numPending_--;
  if (numPending_ == 0 && disconnected_) {
    cleanup();
  }
}

void McrouterClient::disconnect() {
  if (isZombie_) {
    return;
  }
  if (proxy_->eventBase->isInEventBaseThread()) {
    performDisconnect();
  } else {
    proxy_->messageQueue_->blockingWrite(ProxyMessage::Type::DISCONNECT, this);
  }
}

void McrouterClient::performDisconnect() {
  disconnected_ = true;
  if (numPending_ == 0) {
    cleanup();
  }
}

void McrouterClient::cleanup() {
  {
    std::lock_guard<std::mutex> guard(router_->clientListLock_);
    router_->clientList_.erase(router_->clientList_.iterator_to(*this));
  }
  if (callbacks_.on_disconnect) {
    callbacks_.on_disconnect(arg_);
  }
  decref();
}

McrouterClient* McrouterClient::incref() {
  refcount_++;
  return this;
}

void McrouterClient::decref() {
  assert(refcount_ > 0);
  refcount_--;
  if (refcount_ == 0) {
    router_->onClientDestroyed();
    delete this;
  }
}

}}}  // facebook::memcache::mcrouter
