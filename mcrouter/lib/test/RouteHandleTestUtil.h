/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/experimental/fibers/SimpleLoopController.h>
#include <folly/experimental/fibers/WhenN.h>
#include <folly/Memory.h>

#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook { namespace memcache {

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

template <class RouteHandleIf>
struct RecordingRoute;

template <class RouteHandleIf>
struct TestHandleImpl {
  std::shared_ptr<RouteHandleIf> rh;

  std::vector<std::string> saw_keys;

  std::vector<std::string> sawValues;

  std::vector<uint32_t> sawExptimes;

  std::vector<std::string> sawOperations;

  bool isTko;

  bool isPaused;

  std::vector<folly::fibers::Promise<void>> promises_;

  explicit TestHandleImpl(GetRouteTestData td)
      : rh(makeRouteHandle<RouteHandleIf, RecordingRoute>(
            td, UpdateRouteTestData(), DeleteRouteTestData(), this)
        ),
        isTko(false),
        isPaused(false) {
  }

  explicit TestHandleImpl(UpdateRouteTestData td)
      : rh(makeRouteHandle<RouteHandleIf, RecordingRoute>(
            GetRouteTestData(), td, DeleteRouteTestData(), this)
        ),
        isTko(false),
        isPaused(false) {
  }

  explicit TestHandleImpl(DeleteRouteTestData td)
      : rh(makeRouteHandle<RouteHandleIf, RecordingRoute>(
            GetRouteTestData(), UpdateRouteTestData(), td, this)
        ),
        isTko(false),
        isPaused(false) {
  }

  TestHandleImpl(GetRouteTestData g_td,
                 UpdateRouteTestData u_td,
                 DeleteRouteTestData d_td)
      : rh(makeRouteHandle<RouteHandleIf, RecordingRoute>(
            g_td, u_td, d_td, this)
        ),
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
    folly::fibers::addTask([this]() {
        for (auto& promise: promises_) {
          promise.setValue();
        }
        promises_.clear();
      });
  }

  void wait() {
    assert(isPaused);
    folly::fibers::await([this](folly::fibers::Promise<void> promise) {
        promises_.push_back(std::move(promise));
      });
    isPaused = false;
  }
};

/* Records all the keys we saw */
template <class RouteHandleIf>
struct RecordingRoute {
  static std::string routeName() { return "test"; }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const { }

  GetRouteTestData dataGet_;
  UpdateRouteTestData dataUpdate_;
  DeleteRouteTestData dataDelete_;
  TestHandleImpl<RouteHandleIf>* h_;

  RecordingRoute(GetRouteTestData g_td,
                 UpdateRouteTestData u_td,
                 DeleteRouteTestData d_td,
                 TestHandleImpl<RouteHandleIf>* h)
      : dataGet_(g_td), dataUpdate_(u_td), dataDelete_(d_td), h_(h) {}

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    using Reply = ReplyT<Request>;
    ReplyT<Request> reply;

    if (h_->isTko) {
      return Reply(TkoReply);
    }

    if (h_->isPaused) {
      h_->wait();
    }

    h_->saw_keys.push_back(req.fullKey().str());
    h_->sawOperations.push_back(Request::name);
    h_->sawExptimes.push_back(req.exptime());
    if (GetLike<Request>::value) {
      reply.setResult(dataGet_.result_);
      reply.setValue(dataGet_.value_);
      reply.setFlags(dataGet_.flags_);
      return reply;
    }
    if (UpdateLike<Request>::value) {
      assert(req.valuePtrUnsafe() != nullptr);
      auto val = req.valuePtrUnsafe()->clone();
      folly::StringPiece sp_value = coalesceAndGetRange(val);
      h_->sawValues.push_back(sp_value.str());
      reply.setResult(dataUpdate_.result_);
      reply.setFlags(dataUpdate_.flags_);
      return reply;
    }
    if (DeleteLike<Request>::value) {
      reply.setResult(dataDelete_.result_);
      return reply;
    }
    return Reply(DefaultReply, req);
  }
};

template <class RouteHandleIf>
inline std::vector<std::shared_ptr<RouteHandleIf>> get_route_handles(
  const std::vector<std::shared_ptr<TestHandleImpl<RouteHandleIf>>>& hs) {

  std::vector<std::shared_ptr<RouteHandleIf>> r;
  for (auto& h : hs) {
    r.push_back(h->rh);
  }

  return r;
}

class TestFiberManager {
 public:
  TestFiberManager()
      : fm_(folly::make_unique<folly::fibers::SimpleLoopController>()) {}

  template <class LocalType>
  explicit TestFiberManager(LocalType t)
      : fm_(t, folly::make_unique<folly::fibers::SimpleLoopController>()) {}

  void run(std::function<void()>&& fun) {
    runAll({std::move(fun)});
  }

  void runAll(std::vector<std::function<void()>>&& fs) {
    auto& fm = fm_;
    auto& loopController =
      dynamic_cast<folly::fibers::SimpleLoopController&>(fm_.loopController());
    fm.addTask(
      [&fs, &loopController]() {
        folly::fibers::collectAll(fs.begin(), fs.end());
        loopController.stop();
      });

    loopController.loop([](){});
  }

  folly::fibers::FiberManager& getFiberManager() {
    return fm_;
  }

 private:
  folly::fibers::FiberManager fm_;
};

inline std::string toString(const folly::IOBuf& buf) {
  auto b = buf.clone();
  return coalesceAndGetRange(b).str();
}

template <class Rh>
std::string replyFor(Rh& rh, const std::string& key) {
  auto reply = rh.route(McRequestWithMcOp<mc_op_get>(key));
  return toString(reply.value());
}

}}  // facebook::memcache
