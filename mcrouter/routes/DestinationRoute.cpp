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
#include "mcrouter/McrouterLogFailure.h"

namespace facebook { namespace memcache { namespace mcrouter {

const char kFailoverHostPortSeparator = '@';
const char* const kFailoverTagStart = ":failover=";

std::string DestinationRoute::routeName() const {
  return folly::sformat("host|pool={}|id={}|ssl={}|ap={}|timeout={}ms",
    client_->pool.getName(),
    client_->indexInPool,
    client_->useSsl,
    client_->ap->toString(),
    client_->server_timeout.count());
}

bool DestinationRoute::spool(const McRequest& req) const {
  auto asynclogName = fiber_local::getAsynclogName();
  if (asynclogName.empty()) {
    return false;
  }

  folly::StringPiece key = client_->keep_routing_prefix ?
    req.fullKey() :
    req.keyWithoutRoute();

  auto proxy = &fiber_local::getSharedCtx()->proxy();
  auto& client = *client_;
  folly::fibers::Baton b;
  auto res = proxy->router().asyncWriter().run(
    [&b, &client, proxy, key, asynclogName] () {
      asynclog_delete(proxy, client, key, asynclogName);
      b.post();
    }
  );
  if (!res) {
    logFailure(proxy->router(), memcache::failure::Category::kOutOfResources,
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
    const folly::StringPiece fullKey) const {
  const size_t tagLength =
    strlen(kFailoverTagStart) +
    client_->ap->getHost().size() +
    6; // 1 for kFailoverHostPortSeparator + 5 for port.
  std::string failoverTag;
  failoverTag.reserve(tagLength);
  failoverTag = kFailoverTagStart;
  failoverTag += client_->ap->getHost();
  if (client_->ap->getPort() != 0) {
    failoverTag += kFailoverHostPortSeparator;
    failoverTag += folly::to<std::string>(client_->ap->getPort());
  }

  // Safety check: scrub the host and port for ':' to avoid appending
  // more than one field to the key.
  // Note: we start after the ':failover=' part of the string,
  // since we need the initial ':' and we know the remainder is safe.
  for (size_t i = strlen(kFailoverTagStart); i < failoverTag.size(); i++) {
    if (failoverTag[i] == ':') {
      failoverTag[i] = '$';
    }
  }

  return fullKey.str() + failoverTag;
}

McrouterRouteHandlePtr makeDestinationRoute(
  std::shared_ptr<const ProxyClientCommon> client,
  std::shared_ptr<ProxyDestination> destination) {

  return std::make_shared<McrouterRouteHandle<DestinationRoute>>(
    std::move(client),
    std::move(destination));
}

}}}
