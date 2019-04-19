/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#pragma once

#include <memory>

namespace facebook {
namespace memcache {

template <class ThriftClient>
std::unique_ptr<ThriftClient> ThriftTransportBase::createThriftClient() {
  auto channel = createChannel();
  if (!channel) {
    return nullptr;
  }
  return std::make_unique<ThriftClient>(std::move(channel));
}

template <class F>
std::result_of_t<F()> ThriftTransportBase::sendSyncImpl(F&& sendFunc) {
  std::result_of_t<F()> reply;
  try {
    reply = sendFunc();
  } catch (const apache::thrift::transport::TTransportException& tex) {
    carbon::Result res;
    switch (tex.getType()) {
      case apache::thrift::transport::TTransportException::NOT_OPEN:
      case apache::thrift::transport::TTransportException::ALREADY_OPEN:
      case apache::thrift::transport::TTransportException::END_OF_FILE:
      case apache::thrift::transport::TTransportException::SSL_ERROR:
      case apache::thrift::transport::TTransportException::COULD_NOT_BIND:
        if (connectionState_ == ConnectionState::Error && connectionTimedOut_) {
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
  } catch (const std::exception& e) {
    setReplyResultAndMessage(reply, carbon::Result::LOCAL_ERROR, e.what());
  }
  return reply;
}

} // namespace memcache
} // namespace facebook
