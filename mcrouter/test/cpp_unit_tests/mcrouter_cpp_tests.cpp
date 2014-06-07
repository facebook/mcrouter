#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include "folly/FileUtil.h"
#include "folly/experimental/TestUtil.h"
#include "folly/io/async/EventBase.h"
#include "mcrouter/config.h"
#include "mcrouter/options.h"
#include "mcrouter/router.h"

using namespace boost::filesystem;
using namespace facebook::memcache::mcrouter;
using namespace std;

using facebook::memcache::McrouterOptions;

const std::string kAlreadyRepliedConfig =
  "mcrouter/test/cpp_unit_tests/files/already_replied.json";

#define MEMCACHE_CONFIG "mcrouter/test/test_ascii.json"
#define MEMCACHE_ROUTE "/././"

mc_msg_t *new_get_req(const char *key) {
  mc_msg_t *msg = mc_msg_new(strlen(key)+1);
  msg->op = mc_op_get;
  strcpy(reinterpret_cast<char*>(&msg[1]), key);
  msg->key = nstring_of(reinterpret_cast<char*>(&msg[1]));
  return msg;
}

mc_msg_t *new_del_req(const char *key) {
  mc_msg_t *msg = mc_msg_new(strlen(key)+1);
  msg->op = mc_op_delete;
  strcpy(reinterpret_cast<char*>(&msg[1]), key);
  msg->key = nstring_of(reinterpret_cast<char*>(&msg[1]));
  return msg;
}

void clean_msg_vectors(vector<mc_msg_t*> &msgs) {
  for (vector<mc_msg_t*>::iterator it = msgs.begin(); it != msgs.end(); it++) {
    mc_msg_decref(*it);
  }
  msgs.clear();
}

void on_reply(mcrouter_client_t *client,
              mcrouter_msg_t *router_req,
              void* context) {
  mcrouter_msg_t *r_msg = static_cast<mcrouter_msg_t*>(router_req->context);
  mc_msg_incref(router_req->reply);
  r_msg->reply = router_req->reply;
}

void mcrouter_send_helper(mcrouter_client_t *client,
                          const vector<mc_msg_t*>& reqs,
                          vector<mc_msg_t*> &replies) {
  vector<mc_msg_t*> ret;
  int n = reqs.size();
  replies.clear();

  mcrouter_msg_t *r_msgs = new mcrouter_msg_t[n];
  for (int i = 0; i < n; i++) {
    r_msgs[i].req = reqs[i];
    r_msgs[i].reply = nullptr;
    r_msgs[i].context = &r_msgs[i];
  }
  mcrouter_send(client, r_msgs, n);
  int i = 0;
  folly::EventBase* eventBase = mcrouter_client_get_base(client);
  while (i < n) {
    mcrouterLoopOnce(eventBase);
    while (i < n) {
      if (r_msgs[i].reply) {
        replies.push_back(r_msgs[i].reply);
        i++;
      } else break;
    }
  }
  delete [] r_msgs;
}

TEST(mcrouter, start_and_stop) {
  for (int i = 0; i < 2; i++) {
    auto opts = defaultTestOptions();
    opts.config_file = MEMCACHE_CONFIG;
    opts.default_route = MEMCACHE_ROUTE;
    mcrouter_t *router = mcrouter_new(opts);
    EXPECT_FALSE(router == nullptr);

    folly::EventBase eventBase;
    mcrouter_client_t *client = mcrouter_client_new(
      router,
      &eventBase,
      (mcrouter_client_callbacks_t){nullptr, nullptr},
      nullptr,
      0, false);
    EXPECT_FALSE(client == nullptr);

    mcrouter_client_disconnect(client);
    mcrouter_free(router);
  }
}

void on_disconnect(void* context) {
  sem_post((sem_t*)context);
}

void test_disconnect_callback(bool thread_safe_callbacks) {
  sem_t sem_disconnect;
  sem_init(&sem_disconnect, 0, 0);

  auto opts = defaultTestOptions();
  opts.config_file = MEMCACHE_CONFIG;
  opts.default_route = MEMCACHE_ROUTE;
  mcrouter_t *router = mcrouter_new(opts);
  EXPECT_FALSE(router == nullptr);

  folly::EventBase eventBase;
  mcrouter_client_t *client = mcrouter_client_new(
    router,
    thread_safe_callbacks ? nullptr : &eventBase,
    (mcrouter_client_callbacks_t){nullptr, on_disconnect},
    &sem_disconnect,
    0, false);
  EXPECT_FALSE(client == nullptr);

  const char test_key[] = "test_key_disconnect";
  vector<mcrouter_msg_t> reqs(1);
  reqs.back().req = new_get_req(test_key);
  reqs.back().reply = nullptr;

  mcrouter_send(client, reqs.data(), reqs.size());

  EXPECT_NE(0, sem_trywait(&sem_disconnect));
  mcrouter_client_disconnect(client);
  EXPECT_EQ(0, sem_wait(&sem_disconnect));

  mcrouter_free(router);
}

TEST(mcrouter, test_zeroreqs_mcroutersend) {
  sem_t sem_disconnect;
  sem_init(&sem_disconnect, 0, 0);
  auto opts = defaultTestOptions();
  opts.config_file = MEMCACHE_CONFIG;
  opts.default_route = MEMCACHE_ROUTE;
  mcrouter_t *router = mcrouter_new(opts);
  mcrouter_client_t *client = mcrouter_client_new(
    router,
    nullptr,
    (mcrouter_client_callbacks_t){nullptr, on_disconnect},
    &sem_disconnect,
    0, false);

  vector<mcrouter_msg_t> reqs(0);

  mcrouter_send(client, reqs.data(), reqs.size());
  mcrouter_send(client, nullptr, 0);

  mcrouter_client_disconnect(client);

  mcrouter_free(router);
}

TEST(mcrouter, disconnect_callback) {
  test_disconnect_callback(false);
}

TEST(mcrouter, disconnect_callback_ts_callbacks) {
  test_disconnect_callback(true);
}

TEST(mcrouter, fork) {
  const char persistence_id[] = "fork";
  auto opts = defaultTestOptions();
  opts.config_file = MEMCACHE_CONFIG;
  opts.default_route = MEMCACHE_ROUTE;
  mcrouter_t *router = mcrouter_init(persistence_id, opts);
  EXPECT_NE(static_cast<mcrouter_t*>(nullptr), router);

  auto eventBase = std::make_shared<folly::EventBase>();
  mcrouter_client_t *client = mcrouter_client_new(
    router,
    eventBase.get(),
    (mcrouter_client_callbacks_t){on_reply, nullptr},
    nullptr,
    0, false);
  EXPECT_NE(static_cast<mcrouter_client_t*>(nullptr), client);

  const char parent_key[] = "libmcrouter_test:fork:parent";
  const char child_key[] = "libmcrouter_test:fork:child";

  vector<mc_msg_t*> preqs, preplies;
  preqs.push_back(new_get_req(parent_key));
  mcrouter_send_helper(client, preqs, preplies);

  free_all_libmcrouters();

  int fds[2];
  pipe(fds);
  pid_t pid = fork();

  router = mcrouter_init(persistence_id, opts);
  EXPECT_NE(static_cast<mcrouter_t*>(nullptr), router);

  eventBase = std::make_shared<folly::EventBase>();
  client = mcrouter_client_new(
    router,
    eventBase.get(),
    (mcrouter_client_callbacks_t){on_reply, nullptr},
    nullptr,
    0, false);
  EXPECT_NE(static_cast<mcrouter_client_t*>(nullptr), client);

  vector<mc_msg_t*> reqs, replies;
  if (pid) { // parent
    close(fds[1]);
    reqs.push_back(new_get_req(parent_key));
    mcrouter_send_helper(client, reqs, replies);
  } else {
    router = mcrouter_get(persistence_id);
    reqs.push_back(new_get_req(child_key));
    mcrouter_send_helper(client, reqs, replies);
  }
  clean_msg_vectors(reqs);
  clean_msg_vectors(replies);
  mcrouter_client_disconnect(client);

  mcrouter_free(router);

  if (pid) { // parent
    char buf[100];
    read(fds[0], buf, sizeof(buf));
    close(fds[0]);
    waitpid(pid, nullptr, 0);
  } else {
    write(fds[1], "done", 4);
    close(fds[1]);
    exit(0);
  }
}

TEST(mcrouter, already_replied_failed_delete) {
  folly::test::TemporaryDirectory tmpdir("already_replied_failed_delete");
  mcrouter_client_callbacks_t cb = {
    .on_reply = &on_reply,
    .on_disconnect = nullptr
  };
  auto opts = defaultTestOptions();
  std::string configStr;
  EXPECT_TRUE(folly::readFile(kAlreadyRepliedConfig.data(), configStr));
  opts.config_str = configStr;
  opts.async_spool = tmpdir.path().string();
  mcrouter_t *router = mcrouter_new(opts);
  EXPECT_TRUE(router != nullptr);

  folly::EventBase eventBase;
  mcrouter_client_t *client = mcrouter_client_new(router, &eventBase, cb,
      nullptr, 0, false);
  EXPECT_TRUE(client != nullptr);

  vector<mc_msg_t*> reqs, replies;
  reqs.push_back(new_del_req("abc"));
  mcrouter_send_helper(client, reqs, replies);

  // Give mcrouter a chance to write the log -- we don't really know when
  // the write completes because we receive the response right away.
  sleep(1);

  mcrouter_client_disconnect(client);
  mcrouter_free(router);

  // Verify the existance and contents of the file.
  vector<string> files;
  string contents;
  string prefix(R"!(["AS1.0",)!");
  string suffix(R"!(,"C",["127.0.0.1",4242,"delete abc\r\n"]])!" "\n");

  for (recursive_directory_iterator it(tmpdir.path());
       it != recursive_directory_iterator();
       ++it) {
    if (!is_directory(it->symlink_status())) {
      files.push_back(it->path().string());
    }
  }
  EXPECT_EQ(files.size(), 1);

  folly::readFile(files[0].data(), contents);
  EXPECT_GT(contents.size(), prefix.size());
  EXPECT_GT(contents.size(), suffix.size());

  auto p = mismatch(prefix.begin(), prefix.end(), contents.begin());
  EXPECT_TRUE(p.first == prefix.end());

  auto s = mismatch(suffix.rbegin(), suffix.rend(), contents.rbegin());
  EXPECT_TRUE(s.first == suffix.rend());
}
