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

#include <list>
#include <memory>
#include <vector>

#include <folly/ScopeGuard.h>
#include <folly/experimental/fibers/Baton.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/*
 * No more than N requests will be allowed to be concurrently processed by child
 * route. All blocked requests will be sent one request per sender id in
 * round-robin fashion to guarantee fairness.
 */
class OutstandingLimitRoute {
 public:
  using ContextPtr = std::shared_ptr<ProxyRequestContext>;

  static std::string routeName() { return "outstanding-limit"; }

  template <class Operation, class Request>
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return {target_};
  }

  OutstandingLimitRoute(McrouterRouteHandlePtr target, size_t maxOutstanding)
    : target_(std::move(target)), maxOutstanding_(maxOutstanding) {
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type
  route(const Request& req, Operation, const ContextPtr& ctx) {
    if (outstanding_ == maxOutstanding_) {
      auto senderId = ctx->senderId();
      auto& entry = [&]() -> QueueEntry& {
        auto entry_it = senderIdToEntry_.find(senderId);
        if (entry_it != senderIdToEntry_.end()) {
          return *entry_it->second;
        }
        blockedRequests_.push_back(folly::make_unique<QueueEntry>(senderId));
        if (senderId) {
          senderIdToEntry_[senderId] = blockedRequests_.back().get();
        }
        return *blockedRequests_.back();
      }();

      folly::fibers::Baton baton;
      entry.batons.push_back(&baton);
      baton.wait();
    } else {
      outstanding_++;
      assert(outstanding_ <= maxOutstanding_);
    }

    SCOPE_EXIT {
      if (!blockedRequests_.empty()) {
        auto entry = std::move(blockedRequests_.front());
        blockedRequests_.pop_front();

        assert(!entry->batons.empty());

        entry->batons.front()->post();
        entry->batons.pop_front();

        if (!entry->batons.empty()) {
          blockedRequests_.push_back(std::move(entry));
        } else {
          senderIdToEntry_.erase(entry->senderId);
        }
      } else {
        outstanding_--;
      }
    };

    return target_->route(req, Operation(), ctx);
  }

 private:
  const McrouterRouteHandlePtr target_;
  const size_t maxOutstanding_;
  size_t outstanding_{0};

  struct QueueEntry {
    QueueEntry(QueueEntry&&) = delete;
    QueueEntry& operator=(QueueEntry&&) = delete;

    explicit QueueEntry(size_t senderId_) : senderId(senderId_) {
    }
    size_t senderId;
    std::list<folly::fibers::Baton*> batons;
  };

  std::list<std::unique_ptr<QueueEntry>> blockedRequests_;
  std::unordered_map<size_t, QueueEntry*> senderIdToEntry_;
};

}}}  // facebook::memcache::mcrouter
