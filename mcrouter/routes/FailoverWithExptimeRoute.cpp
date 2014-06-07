#include "FailoverWithExptimeRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  McrouterRouteHandlePtr normalTarget,
  std::vector<McrouterRouteHandlePtr> failoverTargets,
  uint32_t failoverExptime,
  FailoverWithExptimeSettings settings) {

  return makeMcrouterRouteHandle<FailoverWithExptimeRoute>(
    std::move(normalTarget),
    std::move(failoverTargets),
    failoverExptime,
    std::move(settings));
}

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return makeMcrouterRouteHandle<FailoverWithExptimeRoute>(factory, json);
}

}}}
