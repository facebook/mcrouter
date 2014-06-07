#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, NullRoute>();

namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute(const char* name) {
  return makeMcrouterRouteHandle<NullRoute>(name);
}

}}}
