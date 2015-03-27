/*
 *  Copyright (c) 2015, Facebook, Inc.
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
 * Check that mc_msg_t copies are created only when necessary
 */
void checkCopies(McRequest& req, mc_msg_t* msg) {
  auto get_route_msg = req.dependentMsg(mc_op_get);

  if (msg == nullptr) {
    EXPECT_TRUE(get_route_msg.get() != nullptr);
  } else {
    EXPECT_TRUE(get_route_msg.get() == msg);
  }


  auto set_route_msg = req.dependentMsg(mc_op_set);

  /* Wrong op, must create a copy */
  EXPECT_TRUE(set_route_msg.get() != nullptr);
  EXPECT_TRUE(set_route_msg.get() != get_route_msg.get());

  auto get_noroute_msg = req.dependentMsgStripRoutingPrefix(mc_op_get);

  EXPECT_TRUE(get_noroute_msg.get() != nullptr);
  if (!req.routingPrefix().empty()) {
    EXPECT_TRUE(get_noroute_msg.get() != get_route_msg.get());
  } else if (msg != nullptr) {
    /* Don't want routing prefix, but there isn't one anyway,
       so no copy required */
    EXPECT_TRUE(get_noroute_msg.get() == get_route_msg.get());
  }

  McRequest req_2 = req.clone();

  auto get_route_msg_2 = req_2.dependentMsg(mc_op_get);
  auto get_noroute_msg_2 = req_2.dependentMsgStripRoutingPrefix(mc_op_get);

  if (msg != nullptr) {
    /* Check no copy */
    EXPECT_TRUE(get_route_msg_2.get() == get_route_msg.get());
    if (!req.routingPrefix().empty()) {
      EXPECT_TRUE(get_noroute_msg_2.get() != get_noroute_msg.get());
    } else {
      EXPECT_TRUE(get_noroute_msg_2.get() == get_noroute_msg.get());
    }
  } else {
    EXPECT_TRUE(get_route_msg_2.get() != nullptr);
    EXPECT_TRUE(get_noroute_msg_2.get() != nullptr);
  }
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
    checkCopies(req, msg);

    mc_msg_decref(msg);
  }

  {
    McRequest req(key);
    run(req);
    checkCopies(req, nullptr);
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
      mc_msg_t* mc_msg_a = mc_msg_new_with_key("test");
      McRequest req_a(McMsgRef::moveRef(mc_msg_a));
      auto req_b = req_a.clone();
      req_b.setExptime(1);
      auto msg_a = req_a.dependentMsg(mc_op_get);
      auto msg_b = req_b.dependentMsg(mc_op_get);
      EXPECT_EQ(msg_a->exptime, 0);
      EXPECT_TRUE(to<string>(msg_a->key) == "test");
      EXPECT_EQ(msg_b->exptime, 1);
      EXPECT_TRUE(to<string>(msg_b->key) == "test");

      req_c = req_b.clone();
      req_c.setExptime(2);
    }

    auto msg_c = req_c.dependentMsg(mc_op_get);
    EXPECT_EQ(msg_c->exptime, 2);
    EXPECT_TRUE(to<string>(msg_c->key) == "test");
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
