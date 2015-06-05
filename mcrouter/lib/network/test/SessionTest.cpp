/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/lib/network/AsyncMcServerWorkerOptions.h"
#include "mcrouter/lib/network/test/SessionTestHarness.h"

using namespace facebook::memcache;

using std::string;
using std::vector;

TEST(Session, basic) {
  AsyncMcServerWorkerOptions opts;
  opts.versionString = "Test-1.0";
  SessionTestHarness t(opts);
  t.inputPackets(
    "get ke", "y\r\n",
    "version\r\n");

  EXPECT_EQ(
    vector<string>({"VALUE key 0 9\r\nkey_value\r\nEND\r\n"
                    "VERSION Test-1.0\r\n"}),
    t.flushWrites());

  t.closeSession();
}

TEST(Session, throttle) {
  AsyncMcServerWorkerOptions opts;
  opts.maxInFlight = 2;
  SessionTestHarness t(opts);

  /* Send 5 requests but don't reply them yet; only first 2 will be read */
  t.pause();
  t.inputPackets(
    "get key1\r\n",
    "get key2\r\n",
    "get key3\r\n",
    "get key4\r\n",
    "get key5\r\n");

  EXPECT_TRUE(t.flushWrites().empty());
  EXPECT_EQ(
    vector<string>({"key1", "key2"}),
    t.pausedKeys());

  /* Now reply to the first request; one more request will be read */
  t.resume(1);

  EXPECT_EQ(
    vector<string>({
        "VALUE key1 0 10\r\nkey1_value\r\nEND\r\n"}),
    t.flushWrites());
  EXPECT_EQ(
    vector<string>({"key2", "key3"}),
    t.pausedKeys());

  /* Finally reply to everything */
  t.resume();
  EXPECT_EQ(
    vector<string>({
        "VALUE key2 0 10\r\nkey2_value\r\nEND\r\nVALUE key3 0 10\r\nkey3_value"
          "\r\nEND\r\n",
        "VALUE key4 0 10\r\nkey4_value\r\nEND\r\n",
        "VALUE key5 0 10\r\nkey5_value\r\nEND\r\n"}),
    t.flushWrites());
  EXPECT_TRUE(t.pausedKeys().empty());

  t.closeSession();
}

TEST(Session, throttleBigPacket) {
  AsyncMcServerWorkerOptions opts;
  opts.maxInFlight = 2;
  SessionTestHarness t(opts);

  /* Network throttling only applies to new packets.
     If an unthrottled packet contains multiple requests,
     we will process them all even if it will push us over limit. */

  /* Send 5 requests in 3 packets.
     First 3 will go through even though maxInFlight is 2 */
  t.pause();
  t.inputPackets(
    "get key1\r\nget key2\r\nget key3\r\n",
    "get key4\r\n",
    "get key5\r\n");

  EXPECT_TRUE(t.flushWrites().empty());
  EXPECT_EQ(
    vector<string>({"key1", "key2", "key3"}),
    t.pausedKeys());

  /* Now reply to the first request; no more requests will be read
     since we're still at the limit */
  t.resume(1);

  EXPECT_EQ(
    vector<string>({
        "VALUE key1 0 10\r\nkey1_value\r\nEND\r\n"}),
    t.flushWrites());
  EXPECT_EQ(
    vector<string>({"key2", "key3"}),
    t.pausedKeys());

  /* Finally reply to everything */
  t.resume();
  EXPECT_EQ(
    vector<string>({
        "VALUE key2 0 10\r\nkey2_value\r\nEND\r\nVALUE key3 0 10\r\nkey3_value"
          "\r\nEND\r\n",
        "VALUE key4 0 10\r\nkey4_value\r\nEND\r\n",
        "VALUE key5 0 10\r\nkey5_value\r\nEND\r\n"}),
    t.flushWrites());
  EXPECT_TRUE(t.pausedKeys().empty());

  t.closeSession();
}

TEST(Session, quit) {
  AsyncMcServerWorkerOptions opts;
  SessionTestHarness t(opts);
  t.inputPackets(
    "get ke", "y\r\n",
    "quit\r\nget key2\r\n");

  /* First get should go through; then quit will close the connection
     and second get will be ignored */

  EXPECT_EQ(
    vector<string>({"VALUE key 0 9\r\nkey_value\r\nEND\r\n"}),
    t.flushWrites());
}

TEST(Session, closeBeforeReply) {
  struct Callbacks {
    void onWriteQuiescence(McServerSession& session) {
      EXPECT_EQ(state_, ACTIVE);
    }
    void onTerminate(McServerSession& session) {
      EXPECT_EQ(state_, ACTIVE);
      state_ = CLOSED;
    }
   private:
    enum State {
      ACTIVE,
      CLOSED
    };
    State state_{ACTIVE};
  } callbacks;

  using std::placeholders::_1;

  AsyncMcServerWorkerOptions opts;
  SessionTestHarness t(opts,
                       std::bind(&Callbacks::onWriteQuiescence, &callbacks, _1),
                       std::bind(&Callbacks::onTerminate, &callbacks, _1));

  // input packets, close session and then reply
  t.inputPackets("get key\r\n");
  t.closeSession();
  t.resume();
}
