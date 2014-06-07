#include "ProxyMcReply.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyMcReply::ProxyMcReply(McReplyBase reply)
  : McReplyBase(std::move(reply)) {}

void ProxyMcReply::setDestination(
    std::shared_ptr<const ProxyClientCommon> dest) {
  dest_ = std::move(dest);
}

std::shared_ptr<const ProxyClientCommon> ProxyMcReply::getDestination() const {
  return dest_;
}

}}}
