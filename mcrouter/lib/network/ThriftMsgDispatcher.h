/*
 *  Copyright (c) 2015, Facebook, Inc.
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

#include "mcrouter/lib/network/TypedMsg.h"

namespace facebook { namespace memcache {

/**
 * A thin wrapper for Thrift structs
 */
template <class M>
class TypedThriftMessage {
 public:
  using rawType = M;
        M& operator*()        { return  raw_; }
  const M& operator*()  const { return  raw_; }
        M* operator->()       { return &raw_; }
  const M* operator->() const { return &raw_; }
        M* get()              { return &raw_; }
  const M* get()        const { return &raw_; }

 private:
  M raw_;

  template <class Protocol>
  uint32_t read(Protocol* iprot) {
    return raw_.read(iprot);
  }

  template <class TMList, class Derived, class... Args>
  friend class ThriftMsgDispatcher;
};

/*
 * Takes a Thrift struct and serializes it to an IOBuf
 * @param thriftStruct: The Typed Thrift Struct
 * @return a unique pointer to the IOBuf
 */
template <class ThriftType>
std::unique_ptr<folly::IOBuf> serializeThriftStruct(
    TypedThriftMessage<ThriftType>&& thriftStruct) {
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
    apache::thrift::CompactProtocolReader reader;
    reader.setInput(&reqBody);
    TypedThriftMessage<M> treq;
    treq.read(&reader);
    static_cast<Proc&>(me)
        .onTypedMessage(std::move(treq), std::forward<Args>(args)...);
  }

 private:
  CallDispatcher<TMList, ThriftMsgDispatcher,
                 const folly::IOBuf&, Args...> dispatcher_;
};

}}  // facebook::memcache
