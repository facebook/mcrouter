/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef LIBMC_FBTRACE_DISABLE
#include <folly/experimental/fibers/FiberManager.h>
#include "fbtrace/libfbtrace/c/fbtrace.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

namespace facebook { namespace memcache {

template<class Operation, class Request>
bool fbTraceOnSend(Operation, const Request& request, const AccessPoint& ap) {
  // Do nothing for non-mc operations.
  return false;
}

template<class Operation, class Reply>
void fbTraceOnReceive(Operation, const mc_fbtrace_info_s* fbtraceInfo,
                      const Reply& reply) {
  // Do nothing by default.
}

#ifndef LIBMC_FBTRACE_DISABLE

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

template<int McOp, class Request>
typename std::enable_if<RequestHasFbTraceInfo<Request>::value, bool>::type
fbTraceOnSend(McOperation<McOp>, const Request& request,
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

  /* fbtrace talks to scribe via thrift,
     which can use up too much stack space */
  return folly::fibers::runInMainContext(
    [fbtraceInfo, op, remote_service, &info] {
      if (fbtrace_request_send(&fbtraceInfo->fbtrace->node,
                               &fbtraceInfo->child_node, fbtraceInfo->metadata,
                               FBTRACE_METADATA_SZ, op, remote_service,
                               info) != 0) {
        VLOG(1) << "Error in fbtrace_request_send: " << fbtrace_error();
        return false;
      }
      return true;
    });
}

template<int McOp, class Reply>
void fbTraceOnReceive(McOperation<McOp>, const mc_fbtrace_info_s* fbtraceInfo,
                      const Reply& reply) {
  if (fbtraceInfo == nullptr) {
    return;
  }

  assert(fbtraceInfo->fbtrace);

  fbtrace_item_t info[2];
  int idx = 0;

  fbtrace_add_item(info, &idx, "result", mc_res_to_string(reply.result()));
  fbtrace_add_item(info, &idx, nullptr, nullptr);

  /* fbtrace talks to scribe via thrift,
     which can use up too much stack space */
  folly::fibers::runInMainContext(
    [fbtraceInfo, &info] {
      if (fbtrace_reply_receive(&fbtraceInfo->child_node, info) != 0) {
        VLOG(1) << "Error in fbtrace_reply_receive: " << fbtrace_error();
      }
    });
}

#endif

}}  // facebook::memcache
