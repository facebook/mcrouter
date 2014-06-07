#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/routes/LatestRoute.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, LatestRoute>(
  const folly::dynamic&,
  std::vector<std::shared_ptr<mcrouter::McrouterRouteHandleIf>>&&);

namespace mcrouter {

McrouterRouteHandlePtr makeLatestRoute(
  std::string name,
  std::vector<McrouterRouteHandlePtr> targets,
  size_t failoverCount) {

  return makeMcrouterRouteHandle<LatestRoute>(
    std::move(name),
    std::move(targets),
    failoverCount);
}

}}}
