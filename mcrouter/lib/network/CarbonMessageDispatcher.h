/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/carbon/CarbonProtocolReader.h"
#include "mcrouter/lib/carbon/CarbonProtocolWriter.h"
#include "mcrouter/lib/carbon/CarbonQueueAppender.h"
#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/network/CaretHeader.h"
#include "mcrouter/lib/network/TypedMsg.h"

namespace facebook {
namespace memcache {

class McServerRequestContext;

/*
 * Takes a Carbon struct and serializes it to an IOBuf
 * @param msg: The typed structure to serialize
 * @return a unique pointer to the IOBuf
 */
template <class Message>
void serializeCarbonStruct(
    const Message& msg,
    carbon::CarbonQueueAppenderStorage& storage) {
  carbon::CarbonProtocolWriter writer(storage);
  msg.serialize(writer);
}

template <class Request>
void serializeCarbonRequest(
    const Request& req,
    carbon::CarbonQueueAppenderStorage& storage) {
  if (req.isBufferDirty()) {
    serializeCarbonStruct(req, storage);
  } else {
    const auto& buf = *req.serializedBuffer();
    storage.setFullBuffer(buf);
  }
}

/**
 * A dispatcher for binary protol serialized Carbon structs.
 *
 * Given a type id and an IOBuf, unserializes the corresponding Carbon struct
 * and calls Proc::onTypedMessage(reqBuffer, reqBufferHeaderSize, M&&, args...)
 *
 * @param MessageList  List of supported Carbon messages: List<M, ...>
 *                     All Ms in the list must be Carbon struct types.
 * @param Proc         Derived processor class, may provide
 *                       void onTypedMessage(const folly::IOBuf& reqBuffer,
 *                                           size_t reqBufferHeaderSize,
 *                                           M&& msg,
 *                                           args...).
 *                     If not provided, default implementation that forwards to
 *                       void onRequest(McServerRequestContext&& context,
 *                                      M&& req,
 *                                      const folly::IOBuf& reqBuffer,
 *                                      reqBufferHeaderSize);
 *                     will be used.
 *                     Overloaded for every Carbon struct in MessageList.
 * @param Args         Additional arguments to pass through to onTypedMessage.
 *
 * WARNING: Using CarbonMsgDispatcher with multiple inheritance is not
 *          recommended.
 */
template <class MessageList, class Proc, class... Args>
class CarbonMessageDispatcher {
 public:
  /**
   * @return true iff headerInfo.typeId corresponds to a message in MessageList
   */
  bool dispatchTypedRequest(
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& buffer,
      Args&&... args) {
    return dispatcher_.dispatch(
        headerInfo.typeId,
        *this,
        headerInfo,
        buffer,
        std::forward<Args>(args)...);
  }

  // Default onTypedMessage() implementation
  template <class M>
  void onTypedMessage(
      const folly::IOBuf& reqBuffer,
      size_t reqBufferHeaderSize,
      M&& req,
      McServerRequestContext&& ctx) {
    static_cast<Proc&>(*this).onRequest(
        std::move(ctx), std::move(req), reqBuffer, reqBufferHeaderSize);
  }

  template <class M>
  static void processMsg(
      CarbonMessageDispatcher& me,
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& reqBuf,
      Args&&... args) {
    folly::io::Cursor cur(&reqBuf);
    cur += headerInfo.headerSize;
    carbon::CarbonProtocolReader reader(cur);
    M req;
    req.setTraceId(headerInfo.traceId);
    req.deserialize(reader);
    static_cast<Proc&>(me).onTypedMessage(
        reqBuf,
        headerInfo.headerSize,
        std::move(req),
        std::forward<Args>(args)...);
  }

 private:
  CallDispatcher<
      MessageList,
      CarbonMessageDispatcher,
      const UmbrellaMessageInfo&,
      const folly::IOBuf&,
      Args...>
      dispatcher_;
};
} // namespace memcache
} // namespace facebook
