#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/lib/routes/WarmUpRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeWarmUpRouteAdd(
  McrouterRouteHandlePtr warmh,
  McrouterRouteHandlePtr coldh,
  uint32_t exptime) {

  return makeMcrouterRouteHandle<WarmUpRoute, McOperation<mc_op_add>>(
    std::move(warmh),
    std::move(coldh),
    exptime);
}

McrouterRouteHandlePtr makeWarmUpRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json,
  uint32_t exptime) {

  return makeMcrouterRouteHandle<WarmUpRoute, McOperation<mc_op_add>>(
    factory, json, exptime);
}

}}}
