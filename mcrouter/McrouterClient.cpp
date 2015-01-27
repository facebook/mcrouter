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

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

struct mcrouter_queue_entry_t {
  mc_msg_t* request;
  McReply reply{mc_res_unknown};
  folly::Optional<McRequest> saved_request;
  McrouterClient* router_client;
  proxy_t *proxy;
  void* context;
};

/**
 * @return true  If precheck finds an interesting request and has the reply
 *   set up otherwise this request needs to go through normal flow.
 */
bool precheckRequest(mcrouter_queue_entry_t* mcreq) {
  FBI_ASSERT(mcreq &&
             mcreq->request &&
             mcreq->request->op >= 0 &&
             mcreq->request->op < mc_nops);

  switch (mcreq->request->op) {
    // Return error (pretend to not even understand the protocol)
    case mc_op_shutdown:
      mcreq->reply = McReply(mc_res_bad_command);
      break;

    // Return 'Not supported' message
    case mc_op_append:
    case mc_op_prepend:
    case mc_op_flushall:
    case mc_op_flushre:
      mcreq->reply = McReply(mc_res_remote_error, "Command not supported");
      break;

    // Everything else is supported
    default:
      auto err = mc_client_req_check(mcreq->request);
      if (err != mc_req_err_valid) {
        mcreq->reply = McReply(mc_res_remote_error, mc_req_err_to_string(err));
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

  __sync_fetch_and_add(&stats_.nreq, nreqs);
  for (size_t i = 0; i < nreqs; i++) {
    mcrouter_queue_entry_t* router_entry = new mcrouter_queue_entry_t();
    FBI_ASSERT(requests[i].req->_refcount > 0);
    router_entry->request = requests[i].req;
    mc_msg_incref(router_entry->request);
    __sync_fetch_and_add(&stats_.op_count[requests[i].req->op], 1);
    __sync_fetch_and_add(&stats_.op_value_bytes[requests[i].req->op],
                         requests[i].req->value.len);
    __sync_fetch_and_add(&stats_.op_key_bytes[requests[i].req->op],
                         requests[i].req->key.len);

    router_entry->context = requests[i].context;
    router_entry->router_client = this;
    router_entry->proxy = proxy_;
    if (requests[i].saved_request.hasValue()) {
      router_entry->saved_request.emplace(
        std::move(*requests[i].saved_request));
    }
    entries[i].data = router_entry;
    entries[i].nbytes = sizeof(mcrouter_queue_entry_t*);
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
          mcrouterLoopOnce(proxy_->eventBase);
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

  memset(&stats_, 0, sizeof(stats_));
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

void McrouterClient::onReply(asox_queue_entry_t* entry) {
  if (maxOutstanding_ != 0) {
    counting_sem_post(&outstandingReqsSem_, 1);
  }

  mcrouter_queue_entry_t* router_entry = (mcrouter_queue_entry_t*) entry->data;
  mcrouter_msg_t router_reply;

  // Don't increment refcounts, because these are transient stack
  // references, and are guaranteed to be shorted lived than router_entry's
  // reference.  This is a premature optimization.
  router_reply.req = router_entry->request;
  router_reply.reply = std::move(router_entry->reply);

  router_reply.context = router_entry->context;

  if (router_reply.reply.result() == mc_res_timeout ||
      router_reply.reply.result() == mc_res_connect_timeout) {
    __sync_fetch_and_add(&stats_.ntmo, 1);
  }

  __sync_fetch_and_add(&stats_.op_value_bytes[router_entry->request->op],
                       router_reply.reply.value().length());

  if (LIKELY(callbacks_.on_reply && !disconnected_)) {
      callbacks_.on_reply(&router_reply, arg_);
  } else if (callbacks_.on_cancel && disconnected_) {
    // This should be called for all canceled requests, when cancellation is
    // implemented properly.
    callbacks_.on_cancel(router_entry->context, arg_);
  }

  stat_decr_safe(router_entry->proxy->stats,
                 mcrouter_queue_entry_num_outstanding_stat);

  mc_msg_decref(router_entry->request);
  delete router_entry;

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

std::unordered_map<std::string, int64_t>
McrouterClient::getStatsHelper(bool clear) {

  std::function<uint32_t(uint32_t*)> fetch_func;

  if (clear) {
    fetch_func = [](uint32_t* ptr) {
      return xchg32_barrier(ptr, 0);
    };
  } else {
    fetch_func = [](uint32_t* ptr) {
      return *ptr;
    };
  }

  std::unordered_map<std::string, int64_t> ret;
  ret["nreq"] = fetch_func(&stats_.nreq);
  for (int op = 0; op < mc_nops; op++) {
    std::string op_name = mc_op_to_string((mc_op_t)op);
    ret[op_name + "_count"] = fetch_func(&stats_.op_count[op]);
    ret[op_name + "_key_bytes"] = fetch_func(&stats_.op_key_bytes[op]);
    ret[op_name + "_value_bytes"] = fetch_func(
      &stats_.op_value_bytes[op]);
  }
  ret["ntmo"] = fetch_func(&stats_.ntmo);

  return ret;
}

void McrouterClient::enqueueReply(proxy_request_t *preq) {
  asox_queue_entry_t entry;
  mcrouter_queue_entry_t *router_entry = new mcrouter_queue_entry_t();

  stat_incr_safe(preq->proxy->stats, mcrouter_queue_entry_num_outstanding_stat);
  router_entry->request = mc_msg_incref(const_cast<mc_msg_t*>(
                                          preq->orig_req.get()));

  router_entry->reply = std::move(preq->reply);

  router_entry->context = preq->context;
  router_entry->proxy = preq->proxy;

  entry.data = router_entry;
  entry.nbytes = sizeof(router_entry);
  entry.type = entry.priority = 0;
  preq->requester->onReply(&entry);
  FBI_ASSERT(preq->_refcount >= 1);
}

void McrouterClient::requestReady(asox_queue_t q,
                                  asox_queue_entry_t* entry,
                                  void* arg) {
  if (entry->type == request_type_request) {
    proxy_t* proxy = (proxy_t*)arg;
    proxy_request_t* preq = nullptr;
    mcrouter_queue_entry_t* router_entry = (mcrouter_queue_entry_t*)entry->data;
    auto client = router_entry->router_client;

    client->numPending_++;

    FBI_ASSERT(entry->nbytes == sizeof(mcrouter_queue_entry_t*));

    if (precheckRequest(router_entry)) {
      client->onReply(entry);
      return;
    }

    if (proxy->being_destroyed) {
      /* We can't process this, since 1) we destroyed the config already,
         and 2) the clients are winding down, so we wouldn't get any
         meaningful response back anyway. */
      LOG(ERROR) << "Outstanding request on a proxy that's being destroyed";
      client->onReply(entry);
      return;
    }

    // this will also grab a reference to the router_entry->request
    // so free the reference from the enqueue below
    try {
      /* steal router_entry->request, so we don't need to mc_msg_decref() */
      preq = new proxy_request_t(proxy,
                                 McMsgRef::moveRef(router_entry->request),
                                 &McrouterClient::enqueueReply,
                                 router_entry->context,
                                 /* reqComplete= */ nullptr,
                                 client->clientId_);
      if (router_entry->saved_request.hasValue()) {
        preq->saved_request.emplace(std::move(*router_entry->saved_request));
      }
    } catch (...) {
      preq = nullptr;
      LOG(ERROR) << "Failed to create proxy_request";
    }
    // we weren't able to construct a preq so pass it back
    // to the client as an error
    if (!preq) {
      router_entry->reply = McReply(mc_res_local_error,
                                    "Couldn't create proxy_request_t");
      client->onReply(entry);
      return;
    }
    preq->requester = client->incref();
    proxy->dispatchRequest(preq);

    delete router_entry;
    proxy_request_decref(preq);
  } else if (entry->type == request_type_old_config) {
    auto oldConfig = (old_config_req_t*) entry->data;
    delete oldConfig;
  } else if (entry->type == request_type_disconnect) {
    auto client = (McrouterClient*) entry->data;
    client->disconnected_ = true;
    if (client->numPending_ == 0) client->cleanup();
  } else if (entry->type == request_type_router_shutdown) {
    /*
     * No-op. We just wanted to wake this event base up so that
     * it can exit event loop and check router->shutdown
     */
  } else {
    LOG(ERROR) << "CRITICAL: Unrecognized request type " << entry->type << "!";
    FBI_ASSERT(0);
  }
}

}}}  // facebook::memcache::mcrouter
