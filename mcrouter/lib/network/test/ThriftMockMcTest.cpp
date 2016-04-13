/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include <folly/GroupVarint.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/test/ClientSocket.h"
#include "mcrouter/lib/network/test/ListenSocket.h"
#include "mcrouter/lib/network/test/MockMc.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/network/TypedMsg.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

using facebook::memcache::AsyncMcServer;
using facebook::memcache::AsyncMcServerWorker;

namespace facebook {
namespace memcache {

namespace {

struct TypedMockMcOnRequest
    : public ThriftMsgDispatcher<TRequestList,
                                 TypedMockMcOnRequest,
                                 McServerRequestContext&&> {
  MockMc& mc_;

  explicit TypedMockMcOnRequest(MockMc& mc) : mc_(mc) {}

  template <class Request>
  void onRequest(McServerRequestContext&& ctx, Request&&) {
    /* non-typed requests not supported */
    McServerRequestContext::reply(std::move(ctx), McReply(mc_res_client_error));
  }

  void onTypedMessage(TypedThriftRequest<cpp2::McGetRequest>&& treq,
                      McServerRequestContext&& ctx) {
    auto item = mc_.get(folly::StringPiece(treq->key.coalesce()));
    TypedThriftReply<cpp2::McGetReply> tres;
    if (!item) {
      tres->result = mc_res_notfound;
    } else {
      tres->result = mc_res_found;
      tres->__isset.value = true;
      tres->__isset.flags = true;
      tres->value = *item->value;
      tres->flags = item->flags;
    }
    McServerRequestContext::reply(std::move(ctx), std::move(tres));
  }

  void onTypedMessage(TypedThriftRequest<cpp2::McSetRequest>&& treq,
                      McServerRequestContext&& ctx) {
    mc_.set(folly::StringPiece(treq->key.coalesce()),
            MockMc::Item(treq->value,
                         (treq->__isset.exptime ? treq->exptime : 0),
                         (treq->__isset.flags ? treq->flags : 0)));
    TypedThriftReply<cpp2::McSetReply> tres;
    tres->result = mc_res_stored;
    McServerRequestContext::reply(std::move(ctx), std::move(tres));
  }

  void onTypedMessage(TypedThriftRequest<cpp2::McDeleteRequest>&& treq,
                      McServerRequestContext&& ctx) {
    TypedThriftReply<cpp2::McDeleteReply> tres;
    if (mc_.del(folly::StringPiece(treq->key.coalesce()))) {
      tres->result = mc_res_deleted;
    } else {
      tres->result = mc_res_notfound;
    }

    McServerRequestContext::reply(std::move(ctx), std::move(tres));
  }

  template <class T>
  void onTypedMessage(TypedThriftMessage<T>&&, McServerRequestContext&&) {
    LOG(INFO) << "Type Ignored for this test";
  }
};
} // anonymous
} // memcache
} // facebook

TEST(CaretMockMc, basic) {
  using namespace facebook::memcache;

  ListenSocket listenSock;

  AsyncMcServer::Options opts;
  opts.existingSocketFd = listenSock.getSocketFd();
  opts.numThreads = 1;

  MockMc mc;

  mc.set("key", MockMc::Item(folly::IOBuf::wrapBuffer("value", 5)));

  AsyncMcServer server(opts);
  server.spawn(
      [&mc](size_t, folly::EventBase& evb, AsyncMcServerWorker& worker) {
        TypedMockMcOnRequest cb(mc);
        worker.setOnRequest(std::move(cb));
        evb.loop();
      });

  ClientSocket clientSock(listenSock.getPort());

  facebook::memcache::cpp2::McGetRequest getreq;
  folly::StringPiece key("key");
  getreq.key = folly::IOBuf(folly::IOBuf::WRAP_BUFFER, key);

  char buffer[18];
  buffer[0] = '^';

  apache::thrift::CompactProtocolWriter writer;

  folly::IOBufQueue queue;
  writer.setOutput(&queue);
  size_t comp = getreq.write(&writer);
  auto iobuf = queue.move();

  uint32_t size = comp;
  uint32_t typeId = 1;
  uint32_t reqId = 100;
  uint32_t additionalFields = 0;

  folly::GroupVarint32::encode(
      buffer + 1, size, typeId, reqId, additionalFields);
  size_t encodedLength = folly::GroupVarint32::encodedSize(buffer + 1);

  iobuf->reserve(encodedLength + 1, 0);
  iobuf->prepend(encodedLength + 1);

  memcpy(iobuf->writableData(), buffer, encodedLength + 1);

  auto dataSp = getRange(iobuf);
  auto reply = clientSock.sendRequest(dataSp, 18);
  EXPECT_EQ('^', reply[0]);

  UmbrellaMessageInfo info;

  caretParseHeader((uint8_t*)reply.data(), reply.size(), info);

  EXPECT_EQ(info.reqId, 100);
  EXPECT_EQ(info.typeId, 2);
  auto readBuf = folly::IOBuf::wrapBuffer(reply.data() + info.headerSize,
                                          info.bodySize);
  apache::thrift::CompactProtocolReader reader;
  reader.setInput(&*readBuf);
  facebook::memcache::cpp2::McGetReply treply;
  treply.read(&reader);

  EXPECT_EQ(mc_res_found, treply.result);

  folly::StringPiece resultVal(treply.value.coalesce());

  EXPECT_EQ("value", resultVal.str());
  server.shutdown();
  server.join();
}
