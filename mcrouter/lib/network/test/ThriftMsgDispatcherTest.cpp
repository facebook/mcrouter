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

#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"

using namespace facebook::memcache;

using ThriftMsgList =
  List<
    TypedMsg<1, cpp2::McGetRequest>,
    TypedMsg<3, cpp2::McSetRequest>
  >;

struct TestCallback :
    public ThriftMsgDispatcher<ThriftMsgList, TestCallback> {

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
  auto iobuf = queue.move();

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

  /* simulate receiving the iobuf over network with some type id */
  ret = cb.dispatchTypedRequest(2, *iobuf);
  /* there's no type id 2, expect false */
  EXPECT_FALSE(ret);
  EXPECT_FALSE(getCalled);
  EXPECT_FALSE(setCalled);

  ret = cb.dispatchTypedRequest(1, *iobuf);
  EXPECT_TRUE(ret);
  EXPECT_TRUE(getCalled);
}
