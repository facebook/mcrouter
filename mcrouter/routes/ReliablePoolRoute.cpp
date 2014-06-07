#include "ReliablePoolRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/lib/Ch3HashFunc.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeReliablePoolRouteCh3(
  std::vector<McrouterRouteHandlePtr> destinations, std::string init_salt,
  size_t failoverCount) {

  auto n = destinations.size();
  return makeMcrouterRouteHandle<ReliablePoolRoute, Ch3HashFunc>(
    std::move(destinations),
    Ch3HashFunc(n),
    std::move(init_salt),
    failoverCount);
}

}}}
