#include "BigValueRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeBigValueRoute(McrouterRouteHandlePtr rh,
                                         BigValueRouteOptions options) {
  return makeMcrouterRouteHandle<BigValueRoute>(
    std::move(rh),
    std::move(options));
}

}}}
