#include "RateLimitRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeRateLimitRoute(
  McrouterRouteHandlePtr normalRoute,
  RateLimiter rateLimiter) {

  return makeMcrouterRouteHandle<RateLimitRoute>(
    std::move(normalRoute),
    std::move(rateLimiter));
}

}}}  // facebook::memcache::mcrouter
