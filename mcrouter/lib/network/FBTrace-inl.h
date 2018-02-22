/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef LIBMC_FBTRACE_DISABLE
#include <folly/fibers/FiberManager.h>

#include "fbtrace/libfbtrace/c/fbtrace.h"
#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

#include "mcrouter/lib/carbon/RequestCommon.h"

namespace facebook {
namespace memcache {

// An opaque identifier. Pass it around after starting tracing with
// traceRequestReceived().
using CommIdType = std::string;

#ifdef LIBMC_FBTRACE_DISABLE

template <class Request>
bool fbTraceOnSend(const Request& request, const AccessPoint& ap) {
  // Do nothing for non-mc operations.
  return false;
}

inline void fbTraceOnReceive(
    const mc_fbtrace_info_s* fbtraceInfo,
    const mc_res_t result) {
  // Do nothing by default.
}

inline bool traceCheckRateLimit() {
  return false;
}

inline uint64_t traceGetCount() {
  return 0;
}

inline mc_fbtrace_info_s* getFbTraceInfo(const carbon::RequestCommon&) {
  return nullptr;
}

template <class Request>
inline CommIdType traceRequestReceived(const Request& req) {
  // Do nothing by default.
  return CommIdType();
}

inline void traceRequestHandlerCompleted(const CommIdType& commId) {
  // Do nothing by default.
}

#else

namespace {

const char* FBTRACE_TAO = "tao";
const char* FBTRACE_MC = "mc";
const char* FBTRACE_OTHER = "other";

inline void fbtraceAddItem(
    fbtrace_item_t* info,
    size_t& idx,
    folly::StringPiece key,
    folly::StringPiece value) {
  fbtrace_item_t* item = &info[idx++];
  item->key = key.begin();
  item->key_len = key.size();
  item->val = value.begin();
  item->val_len = value.size();
}

template <class Request>
typename std::enable_if<Request::hasKey, void>::type
addTraceKey(const Request& request, fbtrace_item_t* info, size_t idx) {
  fbtraceAddItem(info, idx, "key", request.key().routingKey());
}

template <class Request>
typename std::enable_if<!Request::hasKey, void>::type
addTraceKey(const Request&, fbtrace_item_t*, size_t) {}

template <class Request>
typename std::enable_if<Request::hasValue, void>::type
addTraceValue(const Request& request, fbtrace_item_t* info, size_t idx) {
  const auto* value = carbon::valuePtrUnsafe(request);
  fbtraceAddItem(
      info,
      idx,
      "value_len",
      folly::to<std::string>(value ? value->computeChainDataLength() : 0));
}

template <class Request>
typename std::enable_if<!Request::hasValue, void>::type
addTraceValue(const Request&, fbtrace_item_t*, size_t) {}

template <class Request>
typename std::enable_if<Request::hasKey, const char*>::type getRemoteService(
    const Request& request) {
  return request.key().routingKey().startsWith("tao") ? FBTRACE_TAO
                                                      : FBTRACE_MC;
}

template <class Request>
typename std::enable_if<!Request::hasKey, const char*>::type getRemoteService(
    const Request&) {
  return FBTRACE_OTHER;
}

} // anonymous

template <class Request>
bool fbTraceOnSend(const Request& request, const AccessPoint& ap) {
  mc_fbtrace_info_s* fbtraceInfo = request.fbtraceInfo();

  if (fbtraceInfo == nullptr) {
    return false;
  }

  assert(fbtraceInfo->fbtrace);

  fbtrace_item_t info[4];
  size_t idx = 0;

  addTraceKey(request, info, idx);
  addTraceValue(request, info, idx);
  // host:port:transport:protocol or [ipv6]:port:transport:protocol
  std::string dest = ap.toString();
  fbtraceAddItem(info, idx, "remote:host", dest);
  fbtraceAddItem(info, idx, folly::StringPiece(), folly::StringPiece());

  const char* remoteService = getRemoteService(request);

  /* fbtrace talks to scribe via thrift,
     which can use up too much stack space */
  return folly::fibers::runInMainContext([fbtraceInfo, remoteService, &info] {
    if (fbtrace_request_send(
            &fbtraceInfo->fbtrace->node,
            &fbtraceInfo->child_node,
            fbtraceInfo->metadata,
            FBTRACE_METADATA_SZ,
            Request::name,
            remoteService,
            info) != 0) {
      VLOG(1) << "Error in fbtrace_request_send: " << fbtrace_error();
      return false;
    }
    return true;
  });
}

inline void fbTraceOnReceive(
    const mc_fbtrace_info_s* fbtraceInfo,
    const mc_res_t result) {
  if (fbtraceInfo == nullptr) {
    return;
  }

  assert(fbtraceInfo->fbtrace);

  fbtrace_item_t info[2];
  int idx = 0;

  fbtrace_add_item(info, &idx, "result", mc_res_to_string(result));
  fbtrace_add_item(info, &idx, nullptr, nullptr);

  /* fbtrace talks to scribe via thrift,
     which can use up too much stack space */
  folly::fibers::runInMainContext([fbtraceInfo, &info] {
    if (fbtrace_reply_receive(&fbtraceInfo->child_node, info) != 0) {
      VLOG(1) << "Error in fbtrace_reply_receive: " << fbtrace_error();
    }
  });
}

inline const mc_fbtrace_info_s* getFbTraceInfo(
    const carbon::RequestCommon& request) {
  return request.fbtraceInfo();
}

// Start tracing for a request.
// NOTE: this function does not exist if LIBMC_FBTRACE_DISABLE is defined.
CommIdType traceRequestReceived(
    const mc_fbtrace_info_t& trace,
    folly::StringPiece requestType);

// Call this when the handler for a request has completed.
void traceRequestHandlerCompleted(const CommIdType& commId);

template <class Request>
inline CommIdType traceRequestReceived(const Request& req) {
  assert(getFbTraceInfo(req));
  return traceRequestReceived(*getFbTraceInfo(req), Request::name);
}

#endif
}
} // facebook::memcache
