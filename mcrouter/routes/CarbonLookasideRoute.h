/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <string>
#include <utility>

#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/CarbonRouterFactory.h"
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

std::shared_ptr<CarbonRouterInstance<MemcacheRouterInfo>>
createCarbonLookasideRouter(
    const std::string& persistenceId,
    folly::StringPiece flavorUri,
    std::unordered_map<std::string, std::string> optionOverrides =
        std::unordered_map<std::string, std::string>());

/**
 * CarbonLookasideRoute is a route handle that can store replies in memcache
 * with a user defined key. The user controls which replies should be cached.
 * Replies found in memcache will be returned directly without having to
 * traverse further into the routing tree.
 *
 * This behavior is controlled through a user defined class with the following
 * prototype:
 *
 * class CarbonLookasideHelper {
 *    std::string name();
 *
 *    template <typename Request>
 *    bool cacheCandidate(const Request& req);
 *
 *   template <typename Request>
 *   std::string buildKey(const Request& req);
 *};
 *
 * @tparam RouterInfo            The Router
 * @tparam CarbonLookasideHelper User defined class with helper functions
 */
template <class RouterInfo, class CarbonLookasideHelper>
class CarbonLookasideRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;
  using RouteHandlePtr = typename RouterInfo::RouteHandlePtr;

 public:
  std::string routeName() const {
    return folly::sformat(
        "CarbonLookaside|name={}|ttl={}s", carbonLookasideHelper_.name(), ttl_);
  }

  /**
   * Constructs CarbonLookasideRoute.
   *
   * @param children                List of children route handles.
   * @param router                  Router used to communicate with memcache
   * @param client                  Client used to communicate with memcache
   * @param prefix                  Prefix to use with CarbonLookaside keys
   * @param ttl                     TTL of item in CarbonLookaside in seconds
   * @param flavor                  McRouter flavor to use with CarbonLookaside
   */
  CarbonLookasideRoute(
      RouteHandlePtr child,
      std::shared_ptr<CarbonRouterInstance<MemcacheRouterInfo>> router,
      CarbonRouterClient<MemcacheRouterInfo>::Pointer client,
      std::string prefix,
      int32_t ttl)
      : child_(std::move(child)),
        router_(std::move(router)),
        client_(std::move(client)),
        prefix_(std::move(prefix)),
        ttl_(ttl) {
    assert(router_);
    assert(client_);
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*child_, req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    bool cacheCandidate = false;
    std::string key;
    cacheCandidate = carbonLookasideHelper_.cacheCandidate(req);
    if (cacheCandidate) {
      key =
          folly::to<std::string>(prefix_, carbonLookasideHelper_.buildKey(req));
      if (auto optReply = carbonLookasideGet<Request>(key)) {
        return optReply.value();
      }
    }

    auto reply = child_->route(req);

    if (cacheCandidate) {
      carbonLookasideSet(key, reply);
    }
    return reply;
  }

 private:
  const RouteHandlePtr child_;
  const std::shared_ptr<CarbonRouterInstance<MemcacheRouterInfo>> router_;
  const CarbonRouterClient<MemcacheRouterInfo>::Pointer client_;
  const std::string prefix_;
  const int32_t ttl_{0};
  CarbonLookasideHelper carbonLookasideHelper_;

  // Build a request to CarbonLookaside to query for key. Successful replies
  // are deserialized.
  template <typename Request>
  folly::Optional<ReplyT<Request>> carbonLookasideGet(folly::StringPiece key) {
    McGetRequest cacheRequest(key);
    folly::Optional<ReplyT<Request>> ret;
    folly::fibers::Baton baton;
    client_->send(
        cacheRequest,
        [&baton, &ret](const McGetRequest&, McGetReply&& cacheReply) {
          if (isHitResult(cacheReply.result()) &&
              cacheReply.value().hasValue()) {
            folly::io::Cursor cur(cacheReply.value().get_pointer());
            carbon::CarbonProtocolReader reader(cur);
            ReplyT<Request> reply;
            reply.deserialize(reader);
            ret.assign(std::move(reply));
          }
          baton.post();
        });
    baton.wait();
    return ret;
  }

  // Build a request to memcache to store the serialized reply with the
  // provided key.
  template <typename Reply>
  void carbonLookasideSet(folly::StringPiece key, const Reply& reply) {
    McSetRequest req(key);
    folly::fibers::runInMainContext([this, &req, &reply]() {
      carbon::CarbonQueueAppenderStorage storage;
      carbon::CarbonProtocolWriter writer(storage);
      reply.serialize(writer);
      folly::IOBuf body(folly::IOBuf::CREATE, storage.computeBodySize());
      const auto iovs = storage.getIovecs();
      for (size_t i = 0; i < iovs.second; ++i) {
        const struct iovec* iov = iovs.first + i;
        std::memcpy(body.writableTail(), iov->iov_base, iov->iov_len);
        body.append(iov->iov_len);
      }
      req.value() = std::move(body);
      req.exptime() = ttl_;
    });

    folly::fibers::addTask([this, req = std::move(req)]() {
      folly::fibers::Baton baton;
      client_->send(
          req, [&baton](const McSetRequest&, McSetReply&&) { baton.post(); });
      baton.wait();
    });
  }
};

template <class RouterInfo, class CarbonLookasideHelper>
typename RouterInfo::RouteHandlePtr createCarbonLookasideRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "CarbonLookasideRoute is not an object");

  auto jChild = json.get_ptr("child");
  checkLogic(
      jChild != nullptr, "CarbonLookasideRoute: 'child' property is missing");

  auto child = factory.create(*jChild);
  checkLogic(
      child != nullptr,
      "CarbonLookasideRoute: cannot create route handle from 'child'");

  auto jTtl = json.get_ptr("ttl");
  checkLogic(
      jTtl != nullptr, "CarbonLookasideRoute: 'ttl' property is missing");
  checkLogic(jTtl->isInt(), "CarbonLookasideRoute: 'ttl' is not an integer");
  int32_t ttl = jTtl->getInt();

  std::string prefix = ""; // Defaults to no prefix.
  if (auto jPrefix = json.get_ptr("prefix")) {
    checkLogic(
        jPrefix->isString(), "CarbonLookasideRoute: 'prefix' is not a string");
    prefix = jPrefix->getString();
  }
  std::string flavor = "web"; // Defaults to web flavor.
  if (auto jFlavor = json.get_ptr("flavor")) {
    checkLogic(
        jFlavor->isString(), "CarbonLookasideRoute: 'flavor' is not a string");
    flavor = jFlavor->getString();
  }

  // Creates a McRouter client to communicate with memcache using the
  // specified flavor information. The route handle owns the router resource
  // via a shared_ptr. The router will survive reconfigurations given that
  // at least one route handle will maintain a reference to it at any one time.
  // It will be cleaned up automatically whenever the last route handle using it
  // is removed.
  auto persistenceId = folly::to<std::string>("CarbonLookasideClient:", flavor);
  auto router = createCarbonLookasideRouter(persistenceId, flavor);
  if (!router) {
    LOG(ERROR) << "Failed to create router from flavor '" << flavor
               << "' for CarbonLookasideRouter.";
    return std::move(child);
  }

  CarbonRouterClient<MemcacheRouterInfo>::Pointer client{nullptr};
  try {
    client = router->createClient(0 /* max_outstanding_requests */);
  } catch (const std::runtime_error& e) {
    LOG(ERROR)
        << "Failed to create client for CarbonLookasideRouter. Exception: "
        << e.what();
    return std::move(child);
  }
  return makeRouteHandleWithInfo<
      RouterInfo,
      CarbonLookasideRoute,
      CarbonLookasideHelper>(
      std::move(child),
      std::move(router),
      std::move(client),
      std::move(prefix),
      ttl);
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
