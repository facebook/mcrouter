/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ProxyDestinationMap.h"

#include <memory>

#include <folly/Format.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyDestinationBase.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/network/AccessPoint.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

std::string ProxyDestinationMap::genProxyDestinationKey(
    const AccessPoint& ap,
    std::chrono::milliseconds timeout) const {
  if (ap.getProtocol() == mc_ascii_protocol) {
    // we cannot send requests with different timeouts for ASCII, since
    // it will break in-order nature of the protocol
    return folly::sformat("{}-{}", ap.toString(), timeout.count());
  } else {
    return ap.toString();
  }
}

struct ProxyDestinationMap::StateList {
  using List = folly::IntrusiveList<
      ProxyDestinationBase,
      &ProxyDestinationBase::stateListHook_>;
  List list;
};

ProxyDestinationMap::ProxyDestinationMap(ProxyBase* proxy)
    : proxy_(proxy),
      active_(std::make_unique<StateList>()),
      inactive_(std::make_unique<StateList>()),
      inactivityTimeout_(0) {}

// Note: caller must be holding destionationsLock_.
std::shared_ptr<ProxyDestinationBase> ProxyDestinationMap::find(
    const std::string& key) const {
  auto it = destinations_.find(key);
  if (it == destinations_.end()) {
    return nullptr;
  }
  return it->second.lock();
}

void ProxyDestinationMap::removeDestination(ProxyDestinationBase& destination) {
  if (destination.stateList_ == active_.get()) {
    active_->list.erase(StateList::List::s_iterator_to(destination));
  } else if (destination.stateList_ == inactive_.get()) {
    inactive_->list.erase(StateList::List::s_iterator_to(destination));
  }
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);
    destinations_.erase(destination.key());
  }
}

void ProxyDestinationMap::markAsActive(ProxyDestinationBase& destination) {
  if (destination.stateList_ == active_.get()) {
    return;
  }
  if (destination.stateList_ == inactive_.get()) {
    inactive_->list.erase(StateList::List::s_iterator_to(destination));
  }
  active_->list.push_back(destination);
  destination.stateList_ = active_.get();
}

void ProxyDestinationMap::resetAllInactive() {
  for (auto& it : inactive_->list) {
    it.resetInactive();
    it.stateList_ = nullptr;
  }
  inactive_->list.clear();
  active_.swap(inactive_);
}

void ProxyDestinationMap::setResetTimer(std::chrono::milliseconds interval) {
  assert(interval.count() > 0);
  inactivityTimeout_ = static_cast<uint32_t>(interval.count());
  resetTimer_ =
      folly::AsyncTimeout::make(proxy_->eventBase(), [this]() noexcept {
        resetAllInactive();
        scheduleTimer(false /* initialAttempt */);
      });
  scheduleTimer(true /* initialAttempt */);
}

void ProxyDestinationMap::scheduleTimer(bool initialAttempt) {
  if (!resetTimer_->scheduleTimeout(inactivityTimeout_)) {
    MC_LOG_FAILURE(
        proxy_->router().opts(),
        memcache::failure::Category::kSystemError,
        "failed to {}schedule inactivity timer",
        initialAttempt ? "" : "re-");
  }
}

void ProxyDestinationMap::releaseProxyDestinationRef(
    std::shared_ptr<const ProxyDestinationBase>&& destination) {
  ProxyBase& proxy = destination->proxy();
  proxy.eventBase().runInEventBaseThread([dst = std::move(destination)]() {});
}

ProxyDestinationMap::~ProxyDestinationMap() {}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
