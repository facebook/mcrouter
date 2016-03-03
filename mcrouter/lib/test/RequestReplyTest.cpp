/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;

using std::string;
using std::unordered_map;
using std::vector;

static unordered_map<mc_msg_t*, int> increfs, decrefs;
static void mockIncref(mc_msg_t* msg) {
  ++increfs[msg];
}
static void mockDecref(mc_msg_t* msg) {
  ++decrefs[msg];
}

struct TestRefPolicy {
  static mc_msg_t* increfOrNull(mc_msg_t* msg) {
    if (msg != nullptr) {
      mockIncref(msg);
    }
    return msg;
  }

  static mc_msg_t* decref(mc_msg_t* msg) {
    if (msg != nullptr) {
      mockDecref(msg);
    }
    return nullptr;
  }
};

typedef Ref<mc_msg_t, TestRefPolicy> TestMcMsgRef;

static void test_assign(mc_msg_t* a, mc_msg_t* b) {
  increfs.clear();
  decrefs.clear();
  {
    TestMcMsgRef a_ref = TestMcMsgRef::cloneRef(a);
    EXPECT_EQ(increfs[a], 1);
    EXPECT_EQ(decrefs[a], 0);

    TestMcMsgRef b_ref = TestMcMsgRef::cloneRef(b);
    EXPECT_EQ(increfs[b], 1);
    EXPECT_EQ(decrefs[b], 0);

    a_ref = b_ref.clone();
    EXPECT_EQ(increfs[a], 1);
    EXPECT_EQ(decrefs[a], 1);
    EXPECT_EQ(increfs[b], 2);
    EXPECT_EQ(decrefs[b], 0);
  }
  EXPECT_EQ(increfs[a], 1);
  EXPECT_EQ(decrefs[a], 1);
  EXPECT_EQ(increfs[b], 2);
  EXPECT_EQ(decrefs[b], 2);
}

static void test_move(mc_msg_t* a, mc_msg_t* b) {
  increfs.clear();
  decrefs.clear();
  {
    TestMcMsgRef a_ref = TestMcMsgRef::cloneRef(a);
    EXPECT_EQ(increfs[a], 1);
    EXPECT_EQ(decrefs[a], 0);

    TestMcMsgRef b_ref = TestMcMsgRef::cloneRef(b);
    EXPECT_EQ(increfs[b], 1);
    EXPECT_EQ(decrefs[b], 0);

    a_ref = std::move(b_ref);
    EXPECT_EQ(increfs[a], 1);
    EXPECT_EQ(decrefs[a], 1);
    EXPECT_EQ(increfs[b], 1);
    EXPECT_EQ(decrefs[b], 0);
  }
  EXPECT_EQ(increfs[a], 1);
  EXPECT_EQ(decrefs[a], 1);
  EXPECT_EQ(increfs[b], 1);
  EXPECT_EQ(decrefs[b], 1);
}

static void test_copy_ctor(mc_msg_t* a) {
  increfs.clear();
  decrefs.clear();
  {
    TestMcMsgRef a_ref = TestMcMsgRef::cloneRef(a);
    EXPECT_EQ(increfs[a], 1);
    EXPECT_EQ(decrefs[a], 0);

    TestMcMsgRef b_ref = a_ref.clone();
    EXPECT_EQ(increfs[a], 2);
    EXPECT_EQ(decrefs[a], 0);
  }
  EXPECT_EQ(increfs[a], 2);
  EXPECT_EQ(decrefs[a], 2);
}

static void test_move_ctor(mc_msg_t* a) {
  increfs.clear();
  decrefs.clear();
  {
    TestMcMsgRef a_ref = TestMcMsgRef::cloneRef(a);
    EXPECT_EQ(increfs[a], 1);
    EXPECT_EQ(decrefs[a], 0);

    TestMcMsgRef b_ref(std::move(a_ref));
    EXPECT_EQ(increfs[a], 1);
    EXPECT_EQ(decrefs[a], 0);
  }
  EXPECT_EQ(increfs[a], 1);
  EXPECT_EQ(decrefs[a], 1);
}

static void test_move_ctor_mc_msg(mc_msg_t* a) {
  increfs.clear();
  decrefs.clear();
  {
    TestMcMsgRef a_ref = TestMcMsgRef::moveRef(a);
    EXPECT_EQ(increfs[a], 0);
    EXPECT_EQ(decrefs[a], 0);
  }
  EXPECT_EQ(increfs[a], 0);
  EXPECT_EQ(decrefs[a], 1);
}

static void test_move_mc_msg(mc_msg_t* a) {
  increfs.clear();
  decrefs.clear();
  {
    TestMcMsgRef a_ref;
    a_ref = TestMcMsgRef::moveRef(a);
    EXPECT_EQ(increfs[a], 0);
    EXPECT_EQ(decrefs[a], 0);
  }
  EXPECT_EQ(increfs[a], 0);
  EXPECT_EQ(decrefs[a], 1);
}

TEST(requestReply, ref) {
  mc_msg_track_num_outstanding(1);

  mc_msg_t* a = mc_msg_new(0);
  a->key.str = (char*)"a";
  a->key.len = 1;
  mc_msg_t* b = mc_msg_new(0);
  b->key.str = (char*)"b";
  b->key.len = 1;

  {
    // Make sure non-mock RefPolicy compiles
    McMsgRef a_ref(McMsgRef::cloneRef(a));
    McMsgRef b_ref(McMsgRef::cloneRef(b));
  }

  test_assign(a, b);
  test_move(a, b);
  test_copy_ctor(a);
  test_move_ctor(a);
  test_move_ctor_mc_msg(a);
  test_move_mc_msg(a);

  mc_msg_decref(a);
  mc_msg_decref(b);

  EXPECT_TRUE(mc_msg_num_outstanding() == 0);
}

TEST(requestReply, dependentRef) {
  mc_msg_track_num_outstanding(1);

  mc_msg_t* a = mc_msg_new(0);
  a->key.str = (char*)"a";
  a->key.len = 1;

  {
    McMsgRef aRef = McMsgRef::cloneRef(a);
    McMsgRef depCopy = dependentMcMsgRef(aRef);
    EXPECT_TRUE(aRef.get() != depCopy.get());
    EXPECT_TRUE(aRef->key.str == depCopy->key.str);
    EXPECT_TRUE(aRef->value.str == depCopy->value.str);
  }

  mc_msg_decref(a);
  EXPECT_TRUE(mc_msg_num_outstanding() == 0);
}

/**
 * Check various ways to create the request with the key
 * and run the given method on each request
 */
void checkKey(const std::string& key,
              std::function<void(McRequest&)> run) {

  {
    mc_msg_t* msg = mc_msg_new(0);
    msg->key.str = (char*)key.data();
    msg->key.len = key.size();
    msg->op = mc_op_get;

    McRequest req(McMsgRef::cloneRef(msg));
    run(req);

    mc_msg_decref(msg);
  }

  {
    McRequest req(key);
    run(req);
  }
}

TEST(requestReply, requestWithoutRoute) {
  mc_msg_track_num_outstanding(1);

  checkKey(
    "somekey:blah|#|non:hashed:part",
    [] (McRequest& req) {
      EXPECT_TRUE(req.routingPrefix() == "");
      EXPECT_TRUE(req.routingKey() == "somekey:blah");
      EXPECT_TRUE(req.keyWithoutRoute() == "somekey:blah|#|non:hashed:part");
    });

  EXPECT_TRUE(mc_msg_num_outstanding() == 0);
}

TEST(requestReply, requestWithRoute) {
  mc_msg_track_num_outstanding(1);

  checkKey(
    "/region/cluster/somekey:blah|#|non:hashed:part",
    [] (McRequest& req) {
      EXPECT_TRUE(req.routingPrefix() == "/region/cluster/");
      EXPECT_TRUE(req.routingKey() == "somekey:blah");
      EXPECT_TRUE(req.keyWithoutRoute() == "somekey:blah|#|non:hashed:part");
    });

  EXPECT_TRUE(mc_msg_num_outstanding() == 0);
}

TEST(requestReply, replyBasic) {
  mc_msg_track_num_outstanding(1);
  {
    McRequest req("test");
    McMsgRef msg;
    {
      McReply reply(mc_res_found);
      msg = reply.releasedMsg(mc_op_get);
      EXPECT_EQ(to<string>(msg->value), "");
    /* msg should survive after reply destruction */
    }

    EXPECT_TRUE(msg.get() != nullptr);
    EXPECT_TRUE(msg->op == mc_op_get);
    EXPECT_TRUE(msg->result == mc_res_found);

    {
      McReply reply(mc_res_found, "value");
      EXPECT_TRUE(reply.result() == mc_res_found);
      EXPECT_TRUE(toString(reply.value()) == "value");
      msg = reply.releasedMsg(mc_op_get);
      EXPECT_EQ(to<string>(msg->value), "value");
    }
    EXPECT_TRUE(msg.get() != nullptr);
    EXPECT_TRUE(msg->op == mc_op_get);
    EXPECT_TRUE(msg->result == mc_res_found);
    EXPECT_EQ(to<string>(msg->value), "value");
  }

  {
    std::unique_ptr<folly::IOBuf> buf = folly::IOBuf::copyBuffer("testing ");
    buf->prependChain(folly::IOBuf::copyBuffer("releasedMsg"));
    EXPECT_TRUE(buf->isChained());

    McReply reply(mc_res_found, std::move(*buf));
    auto msg = reply.releasedMsg(mc_op_get);
    EXPECT_TRUE(msg.get() != nullptr);
    EXPECT_EQ(msg->op, mc_op_get);
    EXPECT_EQ(msg->result, mc_res_found);
    EXPECT_EQ(to<string>(msg->value), "testing releasedMsg");
  }

  EXPECT_TRUE(mc_msg_num_outstanding() == 0);
}

TEST(requestReply, mutableReply) {
  { // msg_ == nullptr
    McReply r(mc_res_found);
    r.setValue("dummy");
    EXPECT_EQ(toString(r.value()), "dummy");

    EXPECT_EQ(r.result(), mc_res_found);
    r.setResult(mc_res_notfound);
    EXPECT_EQ(r.result(), mc_res_notfound);
  }

  { // msg_ != nullptr
    auto msg = createMcMsgRef("key", "value");
    msg->flags = MC_MSG_FLAG_COMPRESSED;
    McReply r(mc_res_found, std::move(msg));
    EXPECT_EQ(toString(r.value()), "value");
    r.setValue("test");
    EXPECT_EQ(toString(r.value()), "test");
    EXPECT_EQ(r.flags(), MC_MSG_FLAG_COMPRESSED);
  }
}

TEST(requestReply, replyMcMsg) {
  mc_msg_track_num_outstanding(1);

  {
    McRequest req("test");
    string data = "value";
    mc_msg_t* mc_msg = mc_msg_new(0);
    mc_msg->op = mc_op_get;
    mc_msg->result = mc_res_found;
    mc_msg->value = to<nstring_t>(data);

    McReply reply(mc_res_found, McMsgRef::moveRef(mc_msg));
    EXPECT_TRUE(reply.result() == mc_res_found);
    EXPECT_TRUE(toString(reply.value()) == "value");
    auto msg = reply.releasedMsg(mc_op_get);
    auto msg_2 = reply.releasedMsg(mc_op_set);
    EXPECT_TRUE(msg.get() == mc_msg);
    EXPECT_TRUE(msg_2.get() != nullptr);
    EXPECT_TRUE(msg_2.get() != mc_msg);
    EXPECT_TRUE(to<string>(msg_2->value) == "value");

    /* check deep copy occurred */
    EXPECT_TRUE(msg_2->value.str != mc_msg->value.str);
  }

  EXPECT_TRUE(mc_msg_num_outstanding() == 0);
}

TEST(requestReply, replyReduce) {
  McRequest req("test");
  std::vector<McReply> v;
  v.emplace_back(McReply(mc_res_found));
  v.emplace_back(McReply(mc_res_remote_error));
  v.emplace_back(McReply(mc_res_found));

  auto it = McReply::reduce(v.begin(), v.end());
  EXPECT_TRUE(it->result() == mc_res_remote_error);
}

TEST(requestReply, mutableRequest) {
  mc_msg_track_num_outstanding(1);

  {
    /* Modify req_a into req_b, into req_c. Then kill req_a and req_b.
       Data referenced by req_c should still be kept alive. */
    McRequest req_c("dummy");
    {
      McRequest req_a("test");
      auto req_b = req_a.clone();
      req_b.setExptime(1);
      EXPECT_EQ(req_a.exptime(), 0);
      EXPECT_TRUE(req_a.fullKey() == "test");
      EXPECT_EQ(req_b.exptime(), 1);
      EXPECT_TRUE(req_b.fullKey() == "test");

      req_c = req_b.clone();
      req_c.setExptime(2);
    }

    EXPECT_EQ(req_c.exptime(), 2);
    EXPECT_TRUE(req_c.fullKey() == "test");
  }

  EXPECT_TRUE(mc_msg_num_outstanding() == 0);
}

TEST(requestReply, RequestMoveNoExcept) {

  McRequest req_a("dummy");
  auto req_b = req_a.clone();
  EXPECT_TRUE(noexcept(McRequest(std::move(req_a))));

  vector <McRequest> req_vec;
  req_vec.push_back(std::move(req_b));
}

TEST(requestReply, ReplyNoExcept) {
  EXPECT_TRUE(std::is_nothrow_move_constructible<McReply>::value);
  EXPECT_TRUE(std::is_nothrow_default_constructible<McReply>::value);
  EXPECT_TRUE(std::is_nothrow_move_assignable<McReply>::value);
  EXPECT_TRUE(std::is_nothrow_destructible<McReply>::value);

  // Constructors.
  EXPECT_TRUE(noexcept(McReply(mc_res_found)));
  EXPECT_TRUE(noexcept(McReply(DefaultReply, McOperation<mc_op_get>())));
  EXPECT_TRUE(noexcept(McReply(ErrorReply)));
  EXPECT_TRUE(noexcept(McReply(TkoReply)));

  // Members.
  EXPECT_TRUE(
      noexcept(std::declval<McReply>().worseThan(std::declval<McReply>())));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isError()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isFailoverError()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isSoftTkoError()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isHardTkoError()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isTko()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isLocalError()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isConnectError()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isConnectTimeout()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isDataTimeout()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isRedirect()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isHit()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isMiss()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isHotMiss()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().isStored()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().result()));
  EXPECT_TRUE(noexcept(std::declval<McReply>().setResult(mc_res_ok)));
}
