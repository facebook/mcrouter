#ifdef LIBMC_FBTRACE_DISABLE

namespace facebook { namespace memcache {

template<class Operation, class Request>
bool fbTraceOnSend(Operation, const Request& request, const AccessPoint& ap) {
  return false;
}

template<class Operation, class Request, class Reply>
void fbTraceOnReceive(Operation, const Request& request, const Reply& reply) {
}

}}  // facebook::memcache

#else

#include "fbtrace/libfbtrace/c/fbtrace.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/mc/mc_fbtrace_info.h"

namespace facebook { namespace memcache {

namespace {

const char* FBTRACE_TAO = "tao";
const char* FBTRACE_MC = "mc";

inline void fbtraceAddItem(fbtrace_item_t* info, size_t& idx,
                           folly::StringPiece key, folly::StringPiece value) {
  fbtrace_item_t* item = &info[idx++];
  item->key = key.begin();
  item->key_len = key.size();
  item->val = value.begin();
  item->val_len = value.size();
}

}  // namespace

template<class Operation, class Request>
bool fbTraceOnSend(Operation, const Request& request, const AccessPoint& ap) {
  // Do nothing for non-mc operations.
  return false;
}

template<int McOp, class Request>
bool fbTraceOnSend(McOperation<McOp>, const Request& request,
                   const AccessPoint& ap) {
  mc_fbtrace_info_s* fbtraceInfo = request.fbtraceInfo();

  if (fbtraceInfo == nullptr) {
    return false;
  }

  assert(fbtraceInfo->fbtrace);

  fbtrace_item_t info[4];
  size_t idx = 0;
  if (mc_op_has_key((mc_op_t)McOp)) {

    fbtraceAddItem(info, idx, "key", request.routingKey());
  }

  std::string valueLen;
  if (mc_op_has_value((mc_op_t)McOp)) {
    valueLen = folly::to<std::string>(request.value().computeChainDataLength());
    fbtraceAddItem(info, idx, "value_len", valueLen);
  }

  // host:port:transport:protocol or [ipv6]:port:transport:protocol
  std::string dest = ap.toString();
  fbtraceAddItem(info, idx, "remote:host", dest);
  fbtraceAddItem(info, idx, folly::StringPiece(), folly::StringPiece());

  const char *op = mc_op_to_string((mc_op_t)McOp);
  const char *remote_service =
    request.routingKey().startsWith("tao") ? FBTRACE_TAO : FBTRACE_MC;
  if (fbtrace_request_send(&fbtraceInfo->fbtrace->node,
                           &fbtraceInfo->child_node, fbtraceInfo->metadata,
                           FBTRACE_METADATA_SZ, op, remote_service,
                           info) != 0) {
    failure::log("FBTrace", failure::Category::kOther,
                 "Error in fbtrace_request_send: {}", fbtrace_error());
    return false;
  }
  return true;
}

template<class Operation, class Request, class Reply>
void fbTraceOnReceive(Operation, const Request& request, const Reply& reply) {
  // Do nothing by default.
}

template<int McOp, class Request, class Reply>
void fbTraceOnReceive(McOperation<McOp>, const Request& request,
                      const Reply& reply) {
  const mc_fbtrace_info_s* fbtraceInfo = request.fbtraceInfo();

  if (fbtraceInfo == nullptr) {
    return;
  }

  assert(fbtraceInfo->fbtrace);

  fbtrace_item_t info[2];
  int idx = 0;

  fbtrace_add_item(info, &idx, "result", mc_res_to_string(reply.result()));
  fbtrace_add_item(info, &idx, nullptr, nullptr);
  if (fbtrace_reply_receive(&fbtraceInfo->child_node, info) != 0) {
    failure::log("FBTrace", failure::Category::kOther,
                 "Error in fbtrace_reply_receive: {}", fbtrace_error());
  }
}

}}  // facebook::memcache

#endif
