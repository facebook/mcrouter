#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/lib/routes/AllFastestRoute.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, AllFastestRoute>(
  RouteHandleFactory<mcrouter::McrouterRouteHandleIf>&,
  const folly::dynamic&);

namespace mcrouter {

McrouterRouteHandlePtr makeAllFastestRoute(
  std::vector<McrouterRouteHandlePtr> rh) {

  return makeMcrouterRouteHandle<AllFastestRoute>(
    std::move(rh));
}

}}}
