#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/lib/routes/MissFailoverRoute.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, MissFailoverRoute>(
  RouteHandleFactory<mcrouter::McrouterRouteHandleIf>&,
  const folly::dynamic&);

namespace mcrouter {

McrouterRouteHandlePtr makeMissFailoverRoute(
  std::vector<McrouterRouteHandlePtr> targets) {

  return makeMcrouterRouteHandle<MissFailoverRoute>(
    std::move(targets));
}

}}}
