/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/IOBuf.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>

#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/TypedMsg.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

class McServerRequestContext;
template <class ThriftStruct>
class TypedThriftMessage;
template <class ThriftStruct>
class TypedThriftReply;
template <class ThriftStruct>
class TypedThriftRequest;

/*
 * Takes a Thrift struct and serializes it to an IOBuf
 * @param thriftStruct: The Typed Thrift Struct
 * @return a unique pointer to the IOBuf
 */
template <class ThriftType>
std::unique_ptr<folly::IOBuf> serializeThriftStruct(
    const TypedThriftMessage<ThriftType>& thriftStruct) {

  apache::thrift::CompactProtocolWriter writer(
      apache::thrift::SHARE_EXTERNAL_BUFFER);
  folly::IOBufQueue queue;
  writer.setOutput(&queue);
  thriftStruct.get()->write(&writer);
  return queue.move();
}

/**
 * A dispatcher for binary protol serialized Thrift structs.
 *
 * Given a type id and an IOBuf, unserializes the corresponding Thrift struct
 * and calls Proc::onTypedMessage(TypedThriftMessage<Struct>&&, args...)
 *
 * @param TMList  List of supported typed messages: List<TypedMsg<Id, M>, ...>
 *                All Ms in the list must be Thrift struct types.
 * @param Proc    Derived processor class, may provide
 *                  void onTypedMessage(TypedThriftMessage<M>&&, args...).
 *                If not provided, default implementation that forwards to
 *                  void onRequest(McServerRequestContext&&,
 *                                 TypedThriftMessage<M>&& req)
 *                will be used.
 *                Overloaded for every Thrift struct in TMList.
 * @param Args    Additional arguments to pass through to onTypedMessage.
 *
 * WARNING: Using ThriftMsgDispatcher with multiple inheritance is not
 *          recommended.
 */
template <class TMList, class Proc, class... Args>
class ThriftMsgDispatcher {
 public:
  /**
   * @return true iff typeId is present in TMList
   */
  bool dispatchTypedRequest(const UmbrellaMessageInfo& headerInfo,
                            const folly::IOBuf& reqBody,
                            Args&&... args) {
    return dispatcher_.dispatch(headerInfo.typeId, *this, headerInfo, reqBody,
                                std::forward<Args>(args)...);
  }

  // Default onTypedMessage() implementation
  template <class M>
  void onTypedMessage(TypedThriftRequest<M>&& req,
                      McServerRequestContext&& ctx) {
    static_cast<Proc&>(*this).onRequest(std::move(ctx), std::move(req));
  }

  // CallDispatcher callback for requests
  template <class M>
  static typename std::enable_if<ThriftMsgIsRequest<M>::value, void>::type
  processMsg(ThriftMsgDispatcher& me, const UmbrellaMessageInfo& headerInfo,
             const folly::IOBuf& reqBody, Args&&... args) {
    apache::thrift::CompactProtocolReader reader;
    reader.setInput(&reqBody);
    TypedThriftRequest<M> req;
    req.setTraceId(headerInfo.traceId);
    req.read(&reader);
    static_cast<Proc&>(me)
        .onTypedMessage(std::move(req), std::forward<Args>(args)...);
  }

  // CallDispatcher callback for replies
  template <class M>
  static typename std::enable_if<!ThriftMsgIsRequest<M>::value, void>::type
  processMsg(ThriftMsgDispatcher& me, const UmbrellaMessageInfo&,
             const folly::IOBuf& reqBody, Args&&... args) {
    apache::thrift::CompactProtocolReader reader;
    reader.setInput(&reqBody);
    TypedThriftReply<M> reply;
    reply.read(&reader);
    static_cast<Proc&>(me)
        .onTypedMessage(std::move(reply), std::forward<Args>(args)...);
  }

 private:
  CallDispatcher<TMList, ThriftMsgDispatcher, const UmbrellaMessageInfo&,
                 const folly::IOBuf&, Args...> dispatcher_;
};

}}  // facebook::memcache
