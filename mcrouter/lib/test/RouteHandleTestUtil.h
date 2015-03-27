/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <folly/Memory.h>

#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/fibers/SimpleLoopController.h"
#include "mcrouter/lib/fibers/WhenN.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/test/TestRouteHandle.h"

namespace facebook { namespace memcache {

template <class RouteHandleIf>
struct RecordingRoute;

typedef TestRouteHandle<RecordingRoute<TestRouteHandleIf>> RecordingRouteHandle;

struct GetRouteTestData {
  mc_res_t result_;
  std::string value_;
  int64_t flags_;

  GetRouteTestData() :
    result_(mc_res_unknown), value_(std::string()), flags_(0) {
  }

  GetRouteTestData(
      mc_res_t result, const std::string& value, int64_t flags = 0) :
    result_(result), value_(value), flags_(flags) {
  }
};

struct UpdateRouteTestData {
  mc_res_t result_;
  uint64_t flags_;

  UpdateRouteTestData() :
    result_(mc_res_unknown), flags_(0) {
  }

  explicit UpdateRouteTestData(mc_res_t result,
                      uint64_t flags = 0) :
    result_(result), flags_(flags) {
  }
};

struct DeleteRouteTestData {
  mc_res_t result_;

  explicit DeleteRouteTestData(mc_res_t result = mc_res_unknown) :
    result_(result) {
  }
};

struct TestHandle {
  std::shared_ptr<RecordingRouteHandle> rh;

  std::vector<std::string> saw_keys;

  std::vector<std::string> sawValues;

  std::vector<uint32_t> sawExptimes;

  std::vector<mc_op_t> sawOperations;

  bool isTko;

  bool isPaused;

  folly::Optional<FiberPromise<void>> promise_;

  explicit TestHandle(GetRouteTestData td)
      : rh(std::make_shared<RecordingRouteHandle>(
            td, UpdateRouteTestData(), DeleteRouteTestData(), this)
        ),
        isTko(false),
        isPaused(false) {
  }

  explicit TestHandle(UpdateRouteTestData td)
      : rh(std::make_shared<RecordingRouteHandle>(
            GetRouteTestData(), td, DeleteRouteTestData(), this)
        ),
        isTko(false),
        isPaused(false) {
  }

  explicit TestHandle(DeleteRouteTestData td)
      : rh(std::make_shared<RecordingRouteHandle>(
            GetRouteTestData(), UpdateRouteTestData(), td, this)
        ),
        isTko(false),
        isPaused(false) {
  }

  TestHandle(GetRouteTestData g_td,
             UpdateRouteTestData u_td,
             DeleteRouteTestData d_td)
      : rh(std::make_shared<RecordingRouteHandle>(g_td, u_td, d_td, this)),
        isTko(false),
        isPaused(false) {
  }

  void setTko() {
    isTko = true;
  }

  void unsetTko() {
    isTko = false;
  }

  void pause() {
    isPaused = true;
  }

  void unpause() {
    fiber::addTask([this]() {
        promise_->setValue();
      });
  }

  void wait() {
    assert(isPaused);
    fiber::await([this](FiberPromise<void> promise) {
        promise_ = std::move(promise);
      });
    isPaused = false;
  }
};

/* Records all the keys we saw */
template <class RouteHandleIf>
struct RecordingRoute {
  using ContextPtr = typename RouteHandleIf::ContextPtr;
  using StackContext = typename RouteHandleIf::StackContext;

  static std::string routeName() { return "test"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {
    return {};
  }

  GetRouteTestData dataGet_;
  UpdateRouteTestData dataUpdate_;
  DeleteRouteTestData dataDelete_;
  TestHandle* h_;

  RecordingRoute(GetRouteTestData g_td,
                 UpdateRouteTestData u_td,
                 DeleteRouteTestData d_td,
                 TestHandle* h)
      : dataGet_(g_td), dataUpdate_(u_td), dataDelete_(d_td), h_(h) {}

  template <int M, class Request>
  typename ReplyType<McOperation<M>, Request>::type route(
    const Request& req, McOperation<M>, const ContextPtr& ctx,
    StackContext sctx) {

    if (h_->isTko) {
      return McReply(TkoReply);
    }

    if (h_->isPaused) {
      h_->wait();
    }

    {
      auto key = req.key().clone();
      h_->saw_keys.push_back(coalesceAndGetRange(key).str());
    }
    h_->sawOperations.push_back((mc_op_t) M);
    h_->sawExptimes.push_back(req.exptime());
    if (GetLike<McOperation<M>>::value) {
      auto msg = createMcMsgRef(req.fullKey(), dataGet_.value_);
      msg->flags = dataGet_.flags_;
      return McReply(dataGet_.result_, std::move(msg));
    }
    if (UpdateLike<McOperation<M>>::value) {
      auto val = req.value().clone();
      folly::StringPiece sp_value = coalesceAndGetRange(val);
      h_->sawValues.push_back(sp_value.str());
      auto msg = createMcMsgRef(req.fullKey(), sp_value);
      msg->flags = dataUpdate_.flags_;
      return McReply(dataUpdate_.result_, std::move(msg));
    }
    if (DeleteLike<McOperation<M>>::value) {
      auto msg = createMcMsgRef(req.fullKey());
      return McReply(dataDelete_.result_, std::move(msg));
    }
    return McReply(DefaultReply, McOperation<M>());
  }
};

inline std::vector<std::shared_ptr<TestRouteHandleIf>> get_route_handles(
  const std::vector<std::shared_ptr<TestHandle>>& hs) {

  std::vector<std::shared_ptr<TestRouteHandleIf>> r;
  for (auto& h : hs) {
    r.push_back(h->rh);
  }

  return r;
}

class TestFiberManager {
 public:
  TestFiberManager()
      : fm_(folly::make_unique<SimpleLoopController>()) {}

  void run(std::function<void()>&& fun) {
    runAll({std::move(fun)});
  }

  void runAll(std::vector<std::function<void()>>&& fs) {
    auto& fm = fm_;
    auto& loopController =
      dynamic_cast<SimpleLoopController&>(fm_.loopController());
    fm.addTask(
      [&fs, &loopController]() {
        fiber::whenAll(fs.begin(), fs.end());
        loopController.stop();
      });

    loopController.loop([](){});
  }

  FiberManager& getFiberManager() {
    return fm_;
  }

 private:
  FiberManager fm_;
};

inline std::string toString(const folly::IOBuf& buf) {
  auto b = buf.clone();
  return coalesceAndGetRange(b).str();
}

template <class Rh>
std::string replyFor(Rh& rh, const std::string& key) {
  auto reply = rh.routeSimple(McRequest(key), McOperation<mc_op_get>());
  return toString(reply.value());
}

}}  // facebook::memcache
