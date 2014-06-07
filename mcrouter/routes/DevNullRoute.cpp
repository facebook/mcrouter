#include "DevNullRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeDevNullRoute(const char* name) {
  return makeMcrouterRouteHandle<DevNullRoute>(name);
}

}}}
