#include "AsynclogRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeAsynclogRoute(McrouterRouteHandlePtr rh,
                                         std::string poolName,
                                         AsynclogFunc asyncLog) {
  return makeMcrouterRouteHandle<AsynclogRoute>(
    std::move(rh),
    std::move(poolName),
    std::move(asyncLog));
}

}}}
