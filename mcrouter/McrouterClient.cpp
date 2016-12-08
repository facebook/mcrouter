/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McrouterClient.h"

#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ProxyRequestContextTyped.h"
#include "mcrouter/lib/MessageQueue.h"

namespace facebook { namespace memcache { namespace mcrouter {

void McrouterClient::sendRemoteThread(
    std::unique_ptr<ProxyRequestContext> req) {
  proxy_->messageQueue_->blockingWriteRelaxed(ProxyMessage::Type::REQUEST,
                                              req.release());
}

void McrouterClient::sendSameThread(std::unique_ptr<ProxyRequestContext> req) {
  proxy_->messageReady(ProxyMessage::Type::REQUEST, req.release());
}

McrouterClient::McrouterClient(
  std::weak_ptr<MemcacheMcrouterInstance> rtr,
  size_t maxOutstanding,
  bool maxOutstandingError,
  bool sameThread) :
    router_(std::move(rtr)),
    sameThread_(sameThread),
    maxOutstanding_(maxOutstanding),
    maxOutstandingError_(maxOutstandingError) {

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
  std::weak_ptr<MemcacheMcrouterInstance> router,
  size_t maxOutstanding,
  bool maxOutstandingError,
  bool sameThread) {

  auto client = new McrouterClient(std::move(router),
                                   maxOutstanding,
                                   maxOutstandingError,
                                   sameThread);
  client->self_ = std::shared_ptr<McrouterClient>(client);
  return Pointer(client);
}

McrouterClient::~McrouterClient() {
  assert(disconnected_);
}

}}}  // facebook::memcache::mcrouter
