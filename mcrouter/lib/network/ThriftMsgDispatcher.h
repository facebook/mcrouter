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

namespace facebook { namespace memcache {

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
 * @param Proc    Process class, must provide
 *                  void onTypedMessage(TypedThriftMessage<M>&&, args...)
 *                overloaded for every Thrift struct in TMList
 * @param Args    Additional arguments to pass through to onTypedMessage.
 */
template <class TMList, class Proc, class... Args>
class ThriftMsgDispatcher {
 public:
  /**
   * @return true iff typeId is present in TMList
   */
  bool dispatchTypedRequest(size_t typeId,
                            const folly::IOBuf& reqBody,
                            Args... args) {
    return dispatcher_.dispatch(typeId, *this, reqBody,
                                std::forward<Args>(args)...);
  }

  /* CallDispatcher callback */
  template <class M>
  static void processMsg(ThriftMsgDispatcher& me,
                         const folly::IOBuf& reqBody, Args... args) {
    using TMsg = typename std::conditional<ThriftMsgIsRequest<M>::value,
                                           TypedThriftRequest<M>,
                                           TypedThriftReply<M>>::type;

    apache::thrift::CompactProtocolReader reader;
    reader.setInput(&reqBody);
    TMsg tmsg;
    tmsg.read(&reader);
    static_cast<Proc&>(me)
        .onTypedMessage(std::move(tmsg), std::forward<Args>(args)...);
  }

 private:
  CallDispatcher<TMList, ThriftMsgDispatcher,
                 const folly::IOBuf&, Args...> dispatcher_;
};

}}  // facebook::memcache
