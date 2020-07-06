/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "mcrouter/ExternalStatsHandler.h"
#include "mcrouter/StandaloneConfig.h"
#include "mcrouter/lib/carbon/Result.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/* Not specialized for all routers. Does nothing */
template <class RouterInfo>
class RequestAclChecker : public std::false_type {
 public:
  template <class... Args>
  explicit RequestAclChecker(Args&&...) {}
};

/* Memcache specialization. Initializes ACL checker */
template <>
class RequestAclChecker<MemcacheRouterInfo> : public std::true_type {
 public:
  explicit RequestAclChecker(
      ExternalStatsHandler& statsHandler,
      bool requestAclCheckerEnable)
      : requestAclCheckerEnable_(requestAclCheckerEnable),
        requestAclCheckCb_(initRequestAclCheckCbIfEnabled(statsHandler)) {}

  /* Do not apply ACL checks on DELETE */
  template <class Request, class Callback>
  typename std::
      enable_if_t<folly::IsOneOf<Request, McDeleteRequest>::value, bool>
      operator()(Callback&&, Request&&) const {
    return false;
  }

  /* Anything but DELETE, apply ACL checks */
  template <class Request, class Callback>
  typename std::
      enable_if_t<!folly::IsOneOf<Request, McDeleteRequest>::value, bool>
      operator()(Callback&& ctx, Request&& req) const {
    if (requestAclCheckerEnable_ &&
        !requestAclCheckCb_(ctx.getTransport(), req.key_ref()->routingKey())) {
      // TODO: Change this error code when T67679592 is done
      auto reply = ReplyT<Request>{carbon::Result::BAD_FLAGS};
      reply.message_ref() = "Permission Denied";
      Callback::reply(std::move(ctx), std::move(reply));
      return true;
    }
    return false;
  }

 private:
  MemcacheRequestAclCheckerCallback initRequestAclCheckCbIfEnabled(
      ExternalStatsHandler& statsHandler) const {
    if (requestAclCheckerEnable_) {
      return getMemcacheServerRequestAclCheckCallback(statsHandler);
    }
    return {};
  }

  const bool requestAclCheckerEnable_;
  const MemcacheRequestAclCheckerCallback requestAclCheckCb_;
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
