#include "ProxyClientCommon.h"

#include "folly/Conv.h"
#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyClientCommon::ProxyClientCommon(unsigned index,
                                     timeval_t timeout,
                                     AccessPoint ap_,
                                     int keep_routing_prefix_,
                                     bool devnull_asynclog_,
                                     ProxyPool* pool_,
                                     std::string key,
                                     int rxpri,
                                     int txpri,
                                     size_t indexInPool_,
                                     bool useSsl_)
    : pool(pool_),
      idx(index),
      ap(std::move(ap_)),
      proxy_client_key(std::move(key)),
      keep_routing_prefix(keep_routing_prefix_),
      devnull_asynclog(devnull_asynclog_),
      rxpriority(rxpri),
      txpriority(txpri),
      server_timeout(std::move(timeout)),
      indexInPool(indexInPool_),
      useSsl(useSsl_) {
  FBI_ASSERT(ap.getTransport() == mc_stream); /* here there be clowns */

  destination_key = ap.getHost() + ":" + ap.getPort();
}

std::string ProxyClientCommon::genProxyDestinationKey() const {
  return ap.toString() + "-" + folly::to<std::string>(server_timeout.tv_sec)
         + "-" + folly::to<std::string>(server_timeout.tv_usec);
}

}}}  // facebook::memcache::mcrouter
