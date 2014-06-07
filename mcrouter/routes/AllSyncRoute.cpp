#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/lib/routes/AllSyncRoute.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, AllSyncRoute>(
  RouteHandleFactory<mcrouter::McrouterRouteHandleIf>&,
  const folly::dynamic&);

namespace mcrouter {

McrouterRouteHandlePtr makeAllSyncRoute(
  std::vector<McrouterRouteHandlePtr> rh) {

  return makeMcrouterRouteHandle<AllSyncRoute>(
    std::move(rh));
}

McrouterRouteHandlePtr makeAllSyncRoute(
  std::string name,
  std::vector<McrouterRouteHandlePtr> rh) {

  return makeMcrouterRouteHandle<AllSyncRoute>(
    std::move(name),
    std::move(rh));
}

}}}
