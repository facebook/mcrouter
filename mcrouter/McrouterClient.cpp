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
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyRequestContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

/**
 * @return true  If precheck finds an interesting request and has the reply
 *   set up otherwise this request needs to go through normal flow.
 */
bool precheckRequest(ProxyRequestContext& preq) {
  switch (preq.origReq()->op) {
    // Return error (pretend to not even understand the protocol)
    case mc_op_shutdown:
      preq.sendReply(McReply(mc_res_bad_command));
      break;

    // Return 'Not supported' message
    case mc_op_append:
    case mc_op_prepend:
    case mc_op_flushre:
      preq.sendReply(McReply(mc_res_local_error, "Command not supported"));
      break;

    case mc_op_flushall:
      if (!preq.proxy().opts.enable_flush_cmd) {
        preq.sendReply(McReply(mc_res_local_error, "Command disabled"));
        break;
      }
      /* fallthrough */

    // Everything else is supported
    default:
      auto err = mc_client_req_check(preq.origReq().get());
      if (err != mc_req_err_valid) {
        preq.sendReply(McReply(mc_res_local_error, mc_req_err_to_string(err)));
        break;
      }
      return false;
  }
  return true;
}

}

size_t McrouterClient::send(const mcrouter_msg_t* requests, size_t nreqs) {
  if (nreqs == 0) {
    return 0;
  }
  assert(!isZombie_);

  asox_queue_entry_t scratch[100];
  asox_queue_entry_t* entries;

  if (nreqs <= sizeof(scratch)/sizeof(scratch[0])) {
    entries = scratch;
  } else {
    entries = (asox_queue_entry_t*)malloc(sizeof(entries[0]) * nreqs);
    if (entries == nullptr) {
      // errno is ENOMEM
      return 0;
    }
  }

  for (size_t i = 0; i < nreqs; i++) {
    auto preq = ProxyRequestContext::create(
      *proxy_,
      McMsgRef::cloneRef(requests[i].req),
      [] (ProxyRequestContext& prq) {
        prq.requester_->onReply(prq);
      },
      requests[i].context);
    preq->requester_ = incref();
    if (requests[i].saved_request.hasValue()) {
      // TODO: remove copy
      preq->savedRequest_.emplace(requests[i].saved_request->clone());
    }

    entries[i].data = preq.release();
    entries[i].nbytes = sizeof(ProxyRequestContext*);
    entries[i].priority = 0;
    entries[i].type = request_type_request;
  }

  if (router_->opts().standalone) {
    /*
     * Skip the extra asox queue hop and directly call the queue callback,
     * since we're standalone and thus staying in the same thread
     */
    if (maxOutstanding_ == 0) {
      for (int i = 0; i < nreqs; i++) {
        requestReady(proxy_->request_queue, &entries[i], proxy_);
      }
    } else {
      size_t i = 0;
      size_t n = 0;

      while (i < nreqs) {
        while (counting_sem_value(&outstandingReqsSem_) == 0) {
          proxy_->eventBase->loopOnce();
        }
        n += counting_sem_lazy_wait(&outstandingReqsSem_, nreqs - n);

        for (int j = i; j < n; j++) {
          requestReady(proxy_->request_queue, &entries[j], proxy_);
        }

        i = n;
      }
    }
  } else if (maxOutstanding_ == 0) {
    asox_queue_multi_enqueue(proxy_->request_queue, entries, nreqs);
  } else {
    size_t i = 0;
    size_t n = 0;

    while (i < nreqs) {
      n += counting_sem_lazy_wait(&outstandingReqsSem_, nreqs - n);
      asox_queue_multi_enqueue(proxy_->request_queue, &entries[i],
                               n - i);
      i = n;
    }
  }

  if (entries != scratch) {
    free(entries);
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
    case mc_op_get_count:
    case mc_op_get_unique_count:
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
    case mc_op_bump_count:
    case mc_op_bump_unique_count:
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
  asox_queue_entry_t entry;
  entry.type = request_type_disconnect;
  // the libevent priority for disconnect must be greater than or equal to
  // normal request to avoid race condition. (In libevent,
  // higher priority value means lower priority)
  entry.priority = 0;
  entry.data = this;
  entry.nbytes = sizeof(*this);
  asox_queue_enqueue(proxy_->request_queue, &entry);
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

void McrouterClient::requestReady(asox_queue_t q,
                                  asox_queue_entry_t* entry,
                                  void* arg) {
  switch(entry->type)
  {

  case request_type_request:
  {
    proxy_t* proxy = (proxy_t*)arg;
    auto preq =
      std::unique_ptr<ProxyRequestContext>(
        reinterpret_cast<ProxyRequestContext*>(entry->data));
    auto client = preq->requester_;

    client->numPending_++;

    if (precheckRequest(*preq)) {
      return;
    }

    if (proxy->being_destroyed) {
      /* We can't process this, since 1) we destroyed the config already,
         and 2) the clients are winding down, so we wouldn't get any
         meaningful response back anyway. */
      LOG(ERROR) << "Outstanding request on a proxy that's being destroyed";
      preq->sendReply(McReply(mc_res_unknown));
      return;
    }
    proxy->dispatchRequest(std::move(preq));
    break;
  }
  case request_type_old_config:
  {
    auto oldConfig = (old_config_req_t*) entry->data;
    delete oldConfig;
    break;
  }
  case request_type_disconnect:
  {
    auto client = (McrouterClient*) entry->data;
    client->disconnected_ = true;
    if (client->numPending_ == 0) client->cleanup();
    break;
  }
  case request_type_router_shutdown:
    /*
     * No-op. We just wanted to wake this event base up so that
     * it can exit event loop and check router->shutdown
     */
    break;
  }
}

}}}  // facebook::memcache::mcrouter
