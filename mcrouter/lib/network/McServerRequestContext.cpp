#include "McServerRequestContext.h"

#include "mcrouter/lib/network/McServerTransaction.h"

namespace facebook { namespace memcache {

void McServerRequestContext::sendReply(McReply&& reply) {
  transaction_.sendReply(std::move(reply));
}

}}  // facebook::memcache
