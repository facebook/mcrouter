#include "ProxyRequestContext.h"

#include "folly/Memory.h"
#include "mcrouter/mcrouter_client.h"
#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyMcRequest ProxyMcRequest::clone() const {
  ProxyMcRequest req(McRequestWithContext<ProxyRequestContext>::clone());
  req.reqClass_ = reqClass_;
  return req;
}

folly::StringPiece ProxyMcRequest::getRequestClassString() const {
  switch (reqClass_) {
    case RequestClass::NORMAL:
      return "normal";
    case RequestClass::FAILOVER:
      return "failover";
    case RequestClass::SHADOW:
      return "shadow";
  }
  CHECK(false) << "Unknown request class";
}

ProxyRequestContext::ProxyRequestContext(proxy_request_t* preq,
                                         std::shared_ptr<ProxyRoute> pr)
    : preq_(proxy_request_incref(preq)),
      proxyRoute_(std::move(pr)),
      logger_(folly::make_unique<ProxyMcRequestLogger>(preq_->proxy)) {
  FBI_ASSERT(preq_);
}

ProxyRequestContext::~ProxyRequestContext() {
  proxy_request_decref(preq_);
}

uint64_t ProxyRequestContext::senderId() const {
  uint64_t id = 0;
  if (preq_->requester) {
    id = preq_->requester->clientId;
  }

  return id;
}

proxy_request_t& ProxyRequestContext::proxyRequest() const {
  return *preq_;
}

ProxyRoute& ProxyRequestContext::proxyRoute() const {
  return *proxyRoute_;
}

}}}
