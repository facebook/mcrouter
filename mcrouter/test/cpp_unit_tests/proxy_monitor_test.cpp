/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <gtest/gtest.h>

#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>

#include "mcrouter/_router.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/options.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/router.h"

using namespace facebook::memcache::mcrouter;

using facebook::memcache::McMsgRef;
using facebook::memcache::McReply;
using facebook::memcache::McrouterOptions;

int response_count, down_count, may_send_count, remove_count;

void on_response(proxy_client_monitor_t *mon, ProxyDestination* pdstn,
                 const McReply& reply) {
  ++response_count;
}

void on_down(proxy_client_monitor_t *mon, ProxyDestination* pdstn) {
  ++down_count;
}

int may_send(proxy_client_monitor_t *mon, ProxyDestination* pdstn,
             mc_msg_t *req) {
  ++may_send_count;
  return 1;
}

int may_not_send(proxy_client_monitor_t *mon, ProxyDestination* pdstn,
                 mc_msg_t *req) {
  ++may_send_count;
  return 0;
}

void remove_client(proxy_client_monitor_t *mon, ProxyDestination* pdstn) {
  ++remove_count;
}

proxy_client_monitor_t happy_monitor = {
  &on_response, &on_down, &may_send, &remove_client };
proxy_client_monitor_t angry_monitor = {
  &on_response, &on_down, &may_not_send, &remove_client };


void reply_ready(proxy_request_t *preq) {
  *(McReply*)preq->context = std::move(preq->reply);
}

void run_lifecycle_test(proxy_client_monitor_t *monitor, bool allow_failover,
                        mc_res_t expected_result, int expected_response_count,
                        int expected_down_count) {
  response_count = down_count = may_send_count = remove_count = 0;

  auto opts = defaultTestOptions();
  opts.config_file = "mcrouter/test/test_ascii.json";
  opts.disable_dynamic_stats = true;
  opts.miss_on_get_errors = false;
  opts.num_proxies = 1;
  opts.standalone = true;

  auto router = mcrouter_new(opts);
  EXPECT_TRUE(router != nullptr);
  auto proxy = router->getProxy(0);
  folly::EventBase evb;
  proxy->attachEventBase(&evb);
  proxy_set_monitor(proxy, monitor);

  McReply reply(mc_res_unknown);

  mc_msg_t *req = mc_msg_new(0);
  req->key = NSTRING_LIT("tmo:monitor:unit_test:missingkey");
  req->op = mc_op_get;
  proxy_request_t *preq = new proxy_request_t(proxy, McMsgRef::moveRef(req),
                                              reply_ready, &reply);
  preq->failover_disabled = allow_failover ? 0 : 1;

  proxy->dispatchRequest(preq);
  proxy_request_decref(preq);

  while (reply.result() == mc_res_unknown) {
    mcrouterLoopOnce(&evb);
  }

  if (!allow_failover) {
    EXPECT_EQ(reply.result(), expected_result);
  }

  mcrouter_free(router);

  if (!allow_failover) {
    EXPECT_EQ(response_count, expected_response_count);
    EXPECT_EQ(down_count, expected_down_count);
    EXPECT_EQ(may_send_count, 1);
  }
  EXPECT_EQ(remove_count, 17);
}

TEST(proxy_monitor, miss_lifecycle) {
  run_lifecycle_test(&happy_monitor, false, mc_res_connect_timeout, 1, 1);
}

TEST(proxy_monitor, tko_lifecycle) {
  run_lifecycle_test(&angry_monitor, false, mc_res_tko, 0, 0);
}

TEST(proxy_monitor, tmo_failover) {
  run_lifecycle_test(&happy_monitor, true, mc_res_unknown, -1, -1);
}

TEST(proxy_monitor, unchanged_config) {
  auto opts = defaultTestOptions();
  opts.config_file = "mcrouter/test/test_ascii.json";
  opts.disable_dynamic_stats = true;
  opts.miss_on_get_errors = false;

  auto router = mcrouter_new(opts);
  EXPECT_TRUE(router != nullptr);

  // This second configure should return 1, but since the config has not
  // changed, the configure() code should handle it correctly.
  EXPECT_NE(router_configure(router), 0);

  mcrouter_free(router);
}
