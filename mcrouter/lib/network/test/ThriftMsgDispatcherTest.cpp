/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"

#include "mcrouter/lib/network/gen-cpp2/mock_protocol_types.h"

using namespace facebook::memcache;

using ThriftMsgList =
  List<
    TypedMsg<1, cpp2::FooRequest>,
    TypedMsg<3, cpp2::BarRequest>
  >;

struct TestCallback :
    public ThriftMsgDispatcher<ThriftMsgList, TestCallback> {

  std::function<void(TypedThriftMessage<cpp2::FooRequest>&&)> onFoo_;
  std::function<void(TypedThriftMessage<cpp2::BarRequest>&&)> onBar_;

  template <class F, class B>
  TestCallback(F&& onFoo, B&& onBar)
      : onFoo_(std::move(onFoo)),
        onBar_(std::move(onBar)) {
  }

  void onTypedMessage(TypedThriftMessage<cpp2::FooRequest>&& treq) {
    onFoo_(std::move(treq));
  }

  void onTypedMessage(TypedThriftMessage<cpp2::BarRequest>&& treq) {
    onBar_(std::move(treq));
  }
};

TEST(ThriftMsg, basic) {
  /* construct a request */
  cpp2::FooRequest foo;
  foo.id = 12345;
  foo.__isset.data = true;
  foo.data = "abc";

  /* serialize into an iobuf */
  apache::thrift::CompactProtocolWriter writer;
  folly::IOBufQueue queue;
  writer.setOutput(&queue);
  foo.write(&writer);
  auto iobuf = queue.move();

  bool fooCalled = false;
  bool barCalled = false;
  TestCallback cb(
      [&fooCalled](TypedThriftMessage<cpp2::FooRequest>&& treq) {
        /* check unserialized request is the same as sent */
        fooCalled = true;
        EXPECT_TRUE(treq->id == 12345);
        EXPECT_TRUE(treq->__isset.id);
        EXPECT_TRUE(treq->data == "abc");
        EXPECT_TRUE(treq->__isset.data);
      },
      [&barCalled](TypedThriftMessage<cpp2::BarRequest>&& treq) {
        barCalled = true;
      });

  bool ret;

  /* simulate receiving the iobuf over network with some type id */
  ret = cb.dispatchTypedRequest(2, *iobuf);
  /* there's no type id 2, expect false */
  EXPECT_FALSE(ret);
  EXPECT_FALSE(fooCalled);
  EXPECT_FALSE(barCalled);

  ret = cb.dispatchTypedRequest(1, *iobuf);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(fooCalled);
}
