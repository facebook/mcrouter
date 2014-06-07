#include "ShadowRoute.h"

#include "mcrouter/routes/DefaultShadowPolicy.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/ShadowRouteIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeShadowRouteDefault(
  McrouterRouteHandlePtr normalRoute,
  McrouterShadowData shadowData,
  size_t normalIndex,
  DefaultShadowPolicy shadowPolicy) {

  return makeMcrouterRouteHandle<ShadowRoute, DefaultShadowPolicy>(
    std::move(normalRoute),
    std::move(shadowData),
    normalIndex,
    std::move(shadowPolicy));
}

}}}
