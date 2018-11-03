/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include "mcrouter/lib/carbon/RequestReplyUtil.h"

#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/carbon/facebook/ArtilleryUtil.h"
#endif

namespace carbon {
namespace tracing {

#ifdef LIBMC_FBTRACE_DISABLE

template <class Request>
std::pair<uint64_t, uint64_t> sendingRequest(const Request&) {
  return {0, 0};
}

inline std::string getReplyTraceContext(std::pair<uint64_t, uint64_t>) {
  return "";
}

template <class Reply>
void replyReceived(const std::string&, const Reply&) {}

template <class Reply>
std::pair<uint64_t, uint64_t> sendingReply(const Reply&) {
  return {0, 0};
}

#else

template <class Request>
std::pair<uint64_t, uint64_t> sendingRequest(const Request& request) {
  if (!request.traceContext().empty()) {
    auto newContext = sendingRequestInternal(
        request.traceContext(), Request::name, carbon::getFullKey(request));
    return serializeTraceContext(newContext);
  }
  return {0, 0};
}

inline std::string getReplyTraceContext(
    std::pair<uint64_t, uint64_t> serializedTraceId) {
  if (serializedTraceId.first == 0 && serializedTraceId.second == 0) {
    return "";
  }
  return deserializeTraceContext(serializedTraceId);
}

template <class Reply>
void replyReceived(const std::string& requestTraceContext, const Reply& reply) {
  if (!requestTraceContext.empty() && !reply.traceContext().empty()) {
    receivedReplyInternal(
        requestTraceContext, reply.traceContext(), reply.result());
  }
}

template <class Reply>
std::pair<uint64_t, uint64_t> sendingReply(const Reply& reply) {
  if (!reply.traceContext().empty()) {
    return serializeTraceContext(reply.traceContext());
  }
  return {0, 0};
}

#endif

} // namespace tracing
} // namespace carbon
