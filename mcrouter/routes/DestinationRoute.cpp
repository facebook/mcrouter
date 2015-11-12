/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "DestinationRoute.h"

#include <folly/Format.h>

#include "mcrouter/async.h"
#include "mcrouter/awriter.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"

namespace facebook { namespace memcache { namespace mcrouter {

const char* const kFailoverTag = ":failover=1";

std::string DestinationRoute::routeName() const {
  return folly::sformat("host|pool={}|id={}|ssl={}|ap={}|timeout={}ms",
    poolName_,
    indexInPool_,
    destination_->useSsl(),
    destination_->accessPoint()->toString(),
    timeout_.count());
}

bool DestinationRoute::spool(const McRequest& req) const {
  auto asynclogName = fiber_local::getAsynclogName();
  if (asynclogName.empty()) {
    return false;
  }

  folly::StringPiece key = keepRoutingPrefix_ ?
    req.fullKey() :
    req.keyWithoutRoute();

  auto proxy = &fiber_local::getSharedCtx()->proxy();
  auto& ap = *destination_->accessPoint();
  folly::fibers::Baton b;
  auto res = proxy->router().asyncWriter().run(
    [&b, &ap, proxy, key, asynclogName] () {
      asynclog_delete(proxy, ap, key, asynclogName);
      b.post();
    }
  );
  if (!res) {
    MC_LOG_FAILURE(proxy->router().opts(),
                   memcache::failure::Category::kOutOfResources,
                   "Could not enqueue asynclog request (key {}, pool {})",
                   key, asynclogName);
  } else {
    /* Don't reply to the user until we safely logged the request to disk */
    b.wait();
    stat_incr(proxy->stats, asynclog_requests_stat, 1);
  }
  return true;
}

std::string DestinationRoute::keyWithFailoverTag(
    folly::StringPiece fullKey) const {
  return folly::to<std::string>(fullKey, kFailoverTag);
}

McrouterRouteHandlePtr makeDestinationRoute(
  std::shared_ptr<ProxyDestination> destination,
  std::string poolName,
  size_t indexInPool,
  std::chrono::milliseconds timeout,
  bool keepRoutingPrefix) {

  return std::make_shared<McrouterRouteHandle<DestinationRoute>>(
    std::move(destination),
    std::move(poolName),
    indexInPool,
    timeout,
    keepRoutingPrefix);
}

}}}  // facebook::memcache::mcrouter
