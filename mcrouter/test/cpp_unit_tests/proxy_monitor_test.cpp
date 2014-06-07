#include <gtest/gtest.h>

#include "folly/Memory.h"
#include "folly/io/async/EventBase.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/_router.h"
#include "mcrouter/config.h"
#include "mcrouter/options.h"
#include "mcrouter/proxy.h"
#include "mcrouter/router.h"

using namespace facebook::memcache::mcrouter;

using facebook::memcache::McrouterOptions;

int up_count, response_count, down_count, may_send_count, remove_count;

void on_up(proxy_client_monitor_t *mon, ProxyDestination* pdstn) {
  ++up_count;
}

void on_response(proxy_client_monitor_t *mon, ProxyDestination* pdstn,
                 proxy_request_t *preq, mc_msg_t *req, mc_msg_t *reply,
                 mc_res_t result) {
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
  &on_up, &on_response, &on_down, &may_send, &remove_client };
proxy_client_monitor_t angry_monitor = {
  &on_up, &on_response, &on_down, &may_not_send, &remove_client };


void reply_ready(proxy_request_t *preq) {
  mc_msg_t *reply = mc_msg_incref(preq->reply);
  *(mc_msg_t**)preq->context = reply;
}

void run_lifecycle_test(proxy_client_monitor_t *monitor, bool allow_failover,
                        mc_res_t expected_result, int expected_response_count,
                        int expected_down_count) {
  up_count = response_count = down_count = may_send_count = remove_count = 0;

  auto opts = defaultTestOptions();
  opts.config_file = "mcrouter/test/test_ascii.json";
  opts.disable_dynamic_stats = true;
  opts.miss_on_get_errors = false;

  auto router = new mcrouter_t(opts);
  folly::EventBase eventBase;
  auto proxy = new proxy_t(router, &eventBase, opts, false);
  router->proxy_threads.push_back(folly::make_unique<ProxyThread>(proxy));
  EXPECT_NE(router_configure(router), 0);
  proxy_set_monitor(proxy, monitor);

  mc_msg_t *reply = nullptr;

  mc_msg_t *req = mc_msg_new(0);
  req->key = NSTRING_LIT("tmo:monitor:unit_test:missingkey");
  req->op = mc_op_get;
  proxy_request_t *preq = new proxy_request_t(proxy, req, reply_ready, &reply);
  preq->failover_disabled = allow_failover ? 0 : 1;

  proxy->dispatchRequest(preq);
  proxy_request_decref(preq);

  while (reply == nullptr) {
    mcrouterLoopOnce(proxy->eventBase);
  }

  if (!allow_failover) {
    EXPECT_EQ(reply->result, expected_result);
  }

  mc_msg_decref(req);
  mc_msg_decref(reply);
  delete proxy;
  delete router;

  if (!allow_failover) {
    EXPECT_EQ(up_count, 0);
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
