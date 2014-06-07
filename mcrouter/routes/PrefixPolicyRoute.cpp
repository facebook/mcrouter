#include "PrefixPolicyRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makePrefixPolicyRoute(
  std::string name,
  std::vector<McrouterRouteHandlePtr> operationPolicies,
  McrouterRouteHandlePtr defaultPolicy) {

  return makeMcrouterRouteHandle<PrefixPolicyRoute>(
    std::move(name),
    std::move(operationPolicies),
    std::move(defaultPolicy));
}

McrouterRouteHandlePtr makePrefixPolicyRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return makeMcrouterRouteHandle<PrefixPolicyRoute>(factory, json);
}

}}}
