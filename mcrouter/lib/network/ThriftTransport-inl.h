/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <type_traits>

#include <folly/ExceptionWrapper.h>

#ifndef LIBMC_FBTRACE_DISABLE
#include "contextprop/cpp/serde/SerDeHelper.h"
#include "contextprop/if/gen-cpp2/ContextpropConstants_constants.h"
#endif

namespace facebook {
namespace memcache {

template <class ThriftClient>
std::optional<ThriftClient> ThriftTransportBase::createThriftClient() {
  std::optional<ThriftClient> client;
  channel_ = createChannel();
  if (!channel_) {
    return std::nullopt;
  }
  client = ThriftClient(channel_);
  // Avoid any static default-registered event handlers.
  client->clearEventHandlers();
  return client;
}

template <class T>
FOLLY_NOINLINE auto ThriftTransportBase::makeError(
    const folly::exception_wrapper& ew) {
  T reply;
  if (ew.with_exception([&](const apache::thrift::transport::
                                TTransportException& tex) {
        carbon::Result res;
        switch (tex.getType()) {
          case apache::thrift::transport::TTransportException::NOT_OPEN:
          case apache::thrift::transport::TTransportException::ALREADY_OPEN:
          case apache::thrift::transport::TTransportException::END_OF_FILE:
          case apache::thrift::transport::TTransportException::SSL_ERROR:
          case apache::thrift::transport::TTransportException::COULD_NOT_BIND:
            if (connectionState_ == ConnectionState::Error &&
                connectionTimedOut_) {
              res = carbon::Result::CONNECT_TIMEOUT;
            } else {
              res = carbon::Result::CONNECT_ERROR;
            }
            break;
          case apache::thrift::transport::TTransportException::TIMED_OUT:
            res = carbon::Result::TIMEOUT;
            break;
          case apache::thrift::transport::TTransportException::NETWORK_ERROR:
            res = carbon::Result::LOCAL_ERROR;
            break;
          case apache::thrift::transport::TTransportException::INTERRUPTED:
            res = carbon::Result::ABORTED;
            break;
          default:
            // Using local_error as the default error now for a lack of better
            // options.
            res = carbon::Result::LOCAL_ERROR;
        }
        setReplyResultAndMessage(reply, res, tex.what());
      })) {
  } else if (ew.with_exception([&](const std::exception& e) {
               setReplyResultAndMessage(
                   reply, carbon::Result::LOCAL_ERROR, e.what());
             })) {
  }
  return reply;
}

template <class F>
auto ThriftTransportBase::sendSyncImpl(F&& sendFunc) {
  auto tryReply = sendFunc();

  if (LIKELY(tryReply.hasValue() && tryReply->response.hasValue())) {
    return std::move(*tryReply->response);
  }

  return makeError<typename std::result_of_t<F()>::element_type::response_type>(
      tryReply.hasException() ? tryReply.exception()
                              : tryReply->response.exception());
}

#ifndef LIBMC_FBTRACE_DISABLE
template <class Response>
void ThriftTransportUtil::traceResponse(
    const carbon::MessageCommon& request,
    folly::Try<apache::thrift::RpcResponseComplete<Response>>& reply) {
  if (UNLIKELY(
          !request.traceContext().empty() && reply.hasValue() &&
          reply->response.hasValue() &&
          reply->responseContext.headers.find(
              facebook::contextprop::ContextpropConstants_constants::
                  artillery_trace_ids_header_) !=
              reply->responseContext.headers.end())) {
    folly::fibers::runInMainContext([&]() {
      traceResponseImpl(*reply->response, reply->responseContext.headers);
    });
  }
}
#endif

} // namespace memcache
} // namespace facebook
