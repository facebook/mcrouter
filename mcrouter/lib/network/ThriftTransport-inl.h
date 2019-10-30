/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <type_traits>

namespace facebook {
namespace memcache {

template <class ThriftClient>
std::unique_ptr<ThriftClient> ThriftTransportBase::createThriftClient() {
  channel_ = createChannel();
  if (!channel_) {
    return nullptr;
  }
  auto ret = std::make_unique<ThriftClient>(channel_);
  // Avoid any static default-registered event handlers.
  ret->clearEventHandlers();
  return ret;
}

template <class F>
auto ThriftTransportBase::sendSyncImpl(F&& sendFunc) {
  typename std::result_of_t<F()>::element_type::response_type reply;
  auto tryReply = sendFunc();

  if (tryReply.hasValue() && tryReply->response.hasValue()) {
    reply = std::move(*tryReply->response);
    return reply;
  }

  // TODO Double-check there's no copy here
  const auto& ew = tryReply.hasException() ? tryReply.exception()
                                           : tryReply->response.exception();
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

} // namespace memcache
} // namespace facebook
