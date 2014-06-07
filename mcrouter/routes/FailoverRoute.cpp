#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/routes/FailoverRoute.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, FailoverRoute>(
  RouteHandleFactory<mcrouter::McrouterRouteHandleIf>&,
  const folly::dynamic&);

}}
