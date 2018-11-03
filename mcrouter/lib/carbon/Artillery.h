/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/Range.h>

namespace carbon {
namespace tracing {

/**
 * To be called when we are about to send a request to an upstream server.
 *
 * @param request   The request we are about to send.
 *
 * @return          The serialized traceId, that we need to send upsteam with
 *                  the request.
 */
template <class Request>
std::pair<uint64_t, uint64_t> sendingRequest(const Request& request);

/**
 * Builds the trace context string from the pair of integers we received over
 * the wire.
 *
 * @param serializedTraceIds  The serialized trace ids we received from
 *                            the server.
 *
 * @return  The trace context string, to be attached to the reply.
 */
std::string getReplyTraceContext(
    std::pair<uint64_t, uint64_t> serializedTraceIds);

/**
 * Marks the reply as received.
 *
 * @param requestTraceContext   The trace context of the request for which we
 *                              received the reply.
 * @param reply                 The reply we just received from the
 *                              upstream service.
 */
template <class Reply>
void replyReceived(const std::string& requestTraceContext, const Reply& reply);

/**
 * To be called when we are about to send a reply back to the client.
 * If the reply has a traceContext(), serializes it and returns.
 * Otherwise, returns {0, 0}.
 *
 * @param reply   The reply we are about to send.
 *
 * @return        The serialized traceId, that we need to send with the reply.
 */
template <class Reply>
std::pair<uint64_t, uint64_t> sendingReply(const Reply& reply);

} // namespace tracing
} // namespace carbon

#include "Artillery-inl.h"
