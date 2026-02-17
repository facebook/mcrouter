#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ServiceInfo.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

namespace facebook::memcache::mcrouter {
template class ProxyConfig<facebook::memcache::MemcacheRouterInfo>;
}
