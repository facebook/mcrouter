/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <memory>
#include <unordered_map>
#include <vector>

#include <folly/fibers/FiberManager.h>
#include <gtest/gtest.h>

#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/network/ServerLoad.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/lib/test/TestRouteHandle.h"
#include "mcrouter/routes/HashRouteFactory.h"
#include "mcrouter/routes/LoadBalancerRoute.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

namespace {
template <class Reply>
typename std::enable_if<Reply::hasValue, void>::type setReplyValue(
    Reply& reply,
    std::unordered_map<std::string, double>& mymap,
    const std::string& val) {
  auto it = mymap.find(val);

  if (it != mymap.end()) {
    mcrouter::fiber_local<TestRouterInfo>::setServerLoad(
        ServerLoad(ServerLoad::fromPercentLoad(it->second)));
  }

  reply.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, val);
}
template <class Reply>
typename std::enable_if<!Reply::hasValue, void>::type setReplyValue(
    Reply&,
    std::unordered_map<std::string, double>&,
    const std::string& /* val */) {}
} // anonymous namespace

template <class RouteHandleIf>
class TestRoute {
 private:
 public:
  std::string name_;
  std::unordered_map<std::string, double> map_;
  explicit TestRoute(
      std::string name,
      std::unordered_map<std::string, double>& mymap)
      : name_(name), map_(mymap) {}
  template <class Request>
  void traverse(const Request&, const RouteHandleTraverser<RouteHandleIf>&)
      const {}
  template <class Request>
  ReplyT<Request> route(const Request& req) {
    ReplyT<Request> reply;
    if (carbon::GetLike<Request>::value) {
      setReplyValue(reply, map_, name_);
      return reply;
    }
    return createReply(DefaultReply, req);
  }
  static std::string routeName() {
    return "test-route";
  }
};

template <class RouteHandleIf>
inline std::vector<std::shared_ptr<RouteHandleIf>> getRouteHandles(
    const std::vector<std::shared_ptr<TestRoute<RouteHandleIf>>>& hs) {
  std::vector<std::shared_ptr<RouteHandleIf>> r;
  for (auto& h : hs) {
    r.push_back(makeRouteHandle<RouteHandleIf, TestRoute>(h->name_, h->map_));
  }

  return r;
}

using TestHandle = TestRoute<TestRouteHandleIf>;

TEST(CpuLoadBalancerRouteTest, basic) {
  std::unordered_map<std::string, double> mymap;
  mymap.emplace("cpua", 25);
  mymap.emplace("cpub", 50);
  mymap.emplace("cpuc", 75);
  std::vector<std::shared_ptr<TestHandle>> testHandles{
      std::make_shared<TestHandle>("cpua", mymap),
      std::make_shared<TestHandle>("cpub", mymap),
      std::make_shared<TestHandle>("cpuc", mymap)};

  TestRouteHandle<LoadBalancerRoute<TestRouteHandleIf>> rh(
      getRouteHandles(testHandles),
      "",
      std::chrono::milliseconds(100),
      ServerLoad::fromPercentLoad(50));

  std::unordered_map<std::string, size_t> cmap;
  for (int i = 0; i < 100; i++) {
    auto reply = rh.route(McGetRequest("0" + std::to_string(i)));
    std::string v = carbon::valueRangeSlow(reply).str();
    auto it = cmap.find(v);
    if (it != cmap.end()) {
      cmap[std::string(v)]++;
    } else {
      cmap.emplace(std::string(v), 1);
    }
  }
  LOG(INFO) << cmap["cpua"] << " " << cmap["cpub"] << " " << cmap["cpuc"];
  EXPECT_TRUE((cmap["cpua"] >= 56) && (cmap["cpua"] <= 65));
  EXPECT_TRUE((cmap["cpub"] >= 25) && (cmap["cpub"] <= 30));
  EXPECT_TRUE((cmap["cpuc"] >= 10) && (cmap["cpuc"] <= 15));
}

TEST(CpuLoadBalancerRouteTest, oneFullyLoaded) {
  std::unordered_map<std::string, double> mymap;
  mymap.emplace("cpua", 50);
  mymap.emplace("cpub", 50);
  mymap.emplace("cpuc", 100);
  std::vector<std::shared_ptr<TestHandle>> testHandles{
      std::make_shared<TestHandle>("cpua", mymap),
      std::make_shared<TestHandle>("cpub", mymap),
      std::make_shared<TestHandle>("cpuc", mymap)};

  TestRouteHandle<LoadBalancerRoute<TestRouteHandleIf>> rh(
      getRouteHandles(testHandles),
      "SALT-STRING",
      std::chrono::milliseconds(100),
      ServerLoad::fromPercentLoad(50));

  std::unordered_map<std::string, size_t> cmap;
  for (int i = 0; i < 100; i++) {
    auto reply = rh.route(McGetRequest("0" + std::to_string(i)));
    std::string v = carbon::valueRangeSlow(reply).str();
    auto it = cmap.find(v);
    if (it != cmap.end()) {
      cmap[std::string(v)]++;
    } else {
      cmap.emplace(std::string(v), 1);
    }
  }
  LOG(INFO) << cmap["cpua"] << " " << cmap["cpub"] << " " << cmap["cpuc"];
  EXPECT_TRUE((cmap["cpua"] >= 40) && (cmap["cpua"] <= 60));
  EXPECT_TRUE((cmap["cpub"] >= 40) && (cmap["cpub"] <= 60));
  EXPECT_TRUE((cmap["cpuc"] >= 0) && (cmap["cpuc"] <= 1));
}

TEST(CpuLoadBalancerRouteTest, oneZeroLoad) {
  std::unordered_map<std::string, double> mymap;
  mymap.emplace("cpua", 0);
  mymap.emplace("cpub", 50);
  mymap.emplace("cpuc", 50);
  std::vector<std::shared_ptr<TestHandle>> testHandles{
      std::make_shared<TestHandle>("cpua", mymap),
      std::make_shared<TestHandle>("cpub", mymap),
      std::make_shared<TestHandle>("cpuc", mymap)};

  TestRouteHandle<LoadBalancerRoute<TestRouteHandleIf>> rh(
      getRouteHandles(testHandles),
      "TEST-SALT",
      std::chrono::milliseconds(100),
      ServerLoad::fromPercentLoad(50));

  std::unordered_map<std::string, size_t> cmap;
  for (int i = 0; i < 100; i++) {
    auto reply = rh.route(McGetRequest("0" + std::to_string(i)));
    std::string v = carbon::valueRangeSlow(reply).str();
    auto it = cmap.find(v);
    if (it != cmap.end()) {
      cmap[std::string(v)]++;
    } else {
      cmap.emplace(std::string(v), 1);
    }
  }
  LOG(INFO) << cmap["cpua"] << " " << cmap["cpub"] << " " << cmap["cpuc"];
  EXPECT_TRUE((cmap["cpua"] >= 40) && (cmap["cpua"] <= 60));
  EXPECT_TRUE((cmap["cpub"] >= 20) && (cmap["cpub"] <= 35));
  EXPECT_TRUE((cmap["cpuc"] >= 20) && (cmap["cpuc"] <= 35));
}

TEST(CpuLoadBalancerRouteTest, AllFullyLoaded) {
  std::unordered_map<std::string, double> mymap;
  mymap.emplace("cpua", 100);
  mymap.emplace("cpub", 100);
  mymap.emplace("cpuc", 100);
  std::vector<std::shared_ptr<TestHandle>> testHandles{
      std::make_shared<TestHandle>("cpua", mymap),
      std::make_shared<TestHandle>("cpub", mymap),
      std::make_shared<TestHandle>("cpuc", mymap)};

  TestRouteHandle<LoadBalancerRoute<TestRouteHandleIf>> rh(
      getRouteHandles(testHandles),
      "TEST-SALT",
      std::chrono::milliseconds(100),
      ServerLoad::fromPercentLoad(50));

  std::unordered_map<std::string, size_t> cmap;
  for (int i = 0; i < 100; i++) {
    auto reply = rh.route(McGetRequest("0" + std::to_string(i)));
    std::string v = carbon::valueRangeSlow(reply).str();
    auto it = cmap.find(v);
    if (it != cmap.end()) {
      cmap[std::string(v)]++;
    } else {
      cmap.emplace(std::string(v), 1);
    }
  }
  LOG(INFO) << cmap["cpua"] << " " << cmap["cpub"] << " " << cmap["cpuc"];
  EXPECT_TRUE((cmap["cpua"] >= 25) && (cmap["cpua"] <= 45));
  EXPECT_TRUE((cmap["cpub"] >= 25) && (cmap["cpub"] <= 45));
  EXPECT_TRUE((cmap["cpuc"] >= 25) && (cmap["cpuc"] <= 45));
}

TEST(CpuLoadBalancerRouteTest, AllZeroLoads) {
  std::unordered_map<std::string, double> mymap;
  mymap.emplace("cpua", 0);
  mymap.emplace("cpub", 0);
  mymap.emplace("cpuc", 0);
  std::vector<std::shared_ptr<TestHandle>> testHandles{
      std::make_shared<TestHandle>("cpua", mymap),
      std::make_shared<TestHandle>("cpub", mymap),
      std::make_shared<TestHandle>("cpuc", mymap)};

  TestRouteHandle<LoadBalancerRoute<TestRouteHandleIf>> rh(
      getRouteHandles(testHandles),
      "TEST-SALT",
      std::chrono::milliseconds(100),
      ServerLoad::fromPercentLoad(50));

  std::unordered_map<std::string, size_t> cmap;
  for (int i = 0; i < 100; i++) {
    auto reply = rh.route(McGetRequest("0" + std::to_string(i)));
    std::string v = carbon::valueRangeSlow(reply).str();
    auto it = cmap.find(v);
    if (it != cmap.end()) {
      cmap[std::string(v)]++;
    } else {
      cmap.emplace(std::string(v), 1);
    }
  }
  LOG(INFO) << cmap["cpua"] << " " << cmap["cpub"] << " " << cmap["cpuc"];
  EXPECT_TRUE((cmap["cpua"] >= 25) && (cmap["cpua"] <= 45));
  EXPECT_TRUE((cmap["cpub"] >= 25) && (cmap["cpub"] <= 45));
  EXPECT_TRUE((cmap["cpuc"] >= 25) && (cmap["cpuc"] <= 45));
}

TEST(CpuLoadBalancerRouteTest, LoadsWithWait) {
  std::unordered_map<std::string, double> mymap;
  mymap.emplace("cpua", 100);
  mymap.emplace("cpub", 50);
  mymap.emplace("cpuc", 50);
  std::vector<std::shared_ptr<TestHandle>> testHandles{
      std::make_shared<TestHandle>("cpua", mymap),
      std::make_shared<TestHandle>("cpub", mymap),
      std::make_shared<TestHandle>("cpuc", mymap)};

  TestRouteHandle<LoadBalancerRoute<TestRouteHandleIf>> rh(
      getRouteHandles(testHandles),
      "TEST-SALT",
      std::chrono::milliseconds(100),
      ServerLoad::fromPercentLoad(50));

  std::unordered_map<std::string, size_t> cmap;
  for (int i = 0; i < 100; i++) {
    auto reply = rh.route(McGetRequest("0" + std::to_string(i)));
    std::string v = carbon::valueRangeSlow(reply).str();
    auto it = cmap.find(v);
    if (it != cmap.end()) {
      cmap[std::string(v)]++;
    } else {
      cmap.emplace(std::string(v), 1);
    }
    // sleep here to cause server load of 'cpua' to expire
    if (i > 25) {
      /* sleep override */
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  LOG(INFO) << cmap["cpua"] << " " << cmap["cpub"] << " " << cmap["cpuc"];
  EXPECT_TRUE((cmap["cpua"] >= 10) && (cmap["cpua"] <= 20));
  EXPECT_TRUE((cmap["cpub"] >= 25) && (cmap["cpub"] <= 45));
  EXPECT_TRUE((cmap["cpuc"] >= 25) && (cmap["cpuc"] <= 45));
}
