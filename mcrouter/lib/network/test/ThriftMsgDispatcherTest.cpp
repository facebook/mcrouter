/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <cstring>

#include <gtest/gtest.h>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"

using namespace facebook::memcache;

using ThriftMsgList =
  List<
    TypedMsg<1, cpp2::McGetRequest>,
    TypedMsg<3, cpp2::McSetRequest>
  >;

// using facebook::memcache::ThriftMsgDispatcher; will break the GCC build
// until https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59815 is fixed
struct TestCallback
    : public facebook::memcache::ThriftMsgDispatcher<ThriftMsgList,
                                                     TestCallback> {

  std::function<void(TypedThriftRequest<cpp2::McGetRequest>&&)> onGet_;
  std::function<void(TypedThriftRequest<cpp2::McSetRequest>&&)> onSet_;

  template <class F, class B>
  TestCallback(F&& onGet, B&& onSet)
      : onGet_(std::move(onGet)),
        onSet_(std::move(onSet)) {
  }

  void onTypedMessage(TypedThriftRequest<cpp2::McGetRequest>&& treq) {
    onGet_(std::move(treq));
  }

  void onTypedMessage(TypedThriftRequest<cpp2::McSetRequest>&& treq) {
    onSet_(std::move(treq));
  }
};

TEST(ThriftMsg, basic) {
  /* construct a request */
  cpp2::McGetRequest get;
  get.key = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "12345");
  get.__isset.key = true;

  /* serialize into an iobuf */
  apache::thrift::CompactProtocolWriter writer;
  folly::IOBufQueue queue;
  writer.setOutput(&queue);
  get.write(&writer);
  auto body = queue.move();

  UmbrellaMessageInfo headerInfo1;
  UmbrellaMessageInfo headerInfo2;
  headerInfo1.typeId = 1;
  headerInfo2.typeId = 2;
  headerInfo1.bodySize = body->computeChainDataLength();
  headerInfo2.bodySize = headerInfo1.bodySize;

  folly::IOBuf requestBuf1(folly::IOBuf::CREATE, 1024);
  folly::IOBuf requestBuf2(folly::IOBuf::CREATE, 1024);

  headerInfo1.headerSize = caretPrepareHeader(
      headerInfo1, reinterpret_cast<char*>(requestBuf1.writableTail()));
  requestBuf1.append(headerInfo1.headerSize);
  headerInfo2.headerSize = caretPrepareHeader(
      headerInfo2, reinterpret_cast<char*>(requestBuf2.writableTail()));
  requestBuf2.append(headerInfo2.headerSize);

  requestBuf1.appendChain(body->clone());
  requestBuf2.appendChain(std::move(body));

  bool getCalled = false;
  bool setCalled = false;
  TestCallback cb(
      [&getCalled](TypedThriftRequest<cpp2::McGetRequest>&& treq) {
        /* check unserialized request is the same as sent */
        getCalled = true;
        EXPECT_EQ(0,
                  std::strcmp(reinterpret_cast<const char*>(treq->key.data()),
                              "12345"));
        EXPECT_TRUE(treq->__isset.key);
      },
      [&setCalled](TypedThriftRequest<cpp2::McSetRequest>&&) {
        setCalled = true;
      });

  bool ret;

  UmbrellaMessageInfo info;

  /* simulate receiving the iobuf over network with some type id */
  ret = cb.dispatchTypedRequest(headerInfo2, requestBuf2);
  /* there's no type id 2, expect false */
  EXPECT_FALSE(ret);
  EXPECT_FALSE(getCalled);
  EXPECT_FALSE(setCalled);

  ret = cb.dispatchTypedRequest(headerInfo1, requestBuf1);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(getCalled);
}
