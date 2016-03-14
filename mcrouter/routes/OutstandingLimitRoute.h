/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include <folly/Conv.h>
#include <folly/experimental/fibers/Baton.h>
#include <folly/ScopeGuard.h>

#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/network/ThriftMessageTraits.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/options.h"
#include "mcrouter/proxy.h"
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
  std::string routeName() const {
    return folly::to<std::string>("outstanding-limit|limit=", maxOutstanding_);
  }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    t(*target_, req);
  }

  OutstandingLimitRoute(McrouterRouteHandlePtr target, size_t maxOutstanding)
    : target_(std::move(target)), maxOutstanding_(maxOutstanding) {
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    if (outstanding_ == maxOutstanding_) {
      auto& ctx = fiber_local::getSharedCtx();
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

      auto& stats = ctx->proxy().stats;
      folly::fibers::Baton baton;
      int64_t waitingSince = 0;
      if (GetLike<Request>::value) {
        ++currentGetReqsWaiting_;
        waitingSince = nowUs();
      } else if (UpdateLike<Request>::value) {
        ++currentUpdateReqsWaiting_;
        waitingSince = nowUs();
      }
      entry.batons.push_back(&baton);
      baton.wait();
      if (waitingSince > 0) {
        if (GetLike<Request>::value) {
          stat_incr(stats, outstanding_route_get_wait_time_sum_us_stat,
                    static_cast<uint64_t>(nowUs() - waitingSince));
          stat_incr(stats, outstanding_route_get_reqs_queued_helper_stat,
                    currentGetReqsWaiting_);
          --currentGetReqsWaiting_;
          stat_incr(stats, outstanding_route_get_reqs_queued_stat, 1);
        } else if (UpdateLike<Request>::value) {
          stat_incr(stats, outstanding_route_update_wait_time_sum_us_stat,
                    static_cast<uint64_t>(nowUs() - waitingSince));
          stat_incr(stats, outstanding_route_update_reqs_queued_helper_stat,
                    currentUpdateReqsWaiting_);
          --currentUpdateReqsWaiting_;
          stat_incr(stats, outstanding_route_update_reqs_queued_stat, 1);
        }
      }
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

    return target_->route(req);
  }

 private:
  const McrouterRouteHandlePtr target_;
  const size_t maxOutstanding_;
  size_t outstanding_{0};
  size_t currentGetReqsWaiting_{0};
  size_t currentUpdateReqsWaiting_{0};

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
