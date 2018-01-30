/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "RouteHandleTestUtil.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

CarbonRouterInstance<McrouterRouterInfo>* getTestRouter() {
  McrouterOptions opts = defaultTestOptions();
  opts.config = "{ \"route\": \"NullRoute\" }";
  return CarbonRouterInstance<McrouterRouterInfo>::init("test", opts);
}

std::shared_ptr<ProxyRequestContextWithInfo<McrouterRouterInfo>>
getTestContext() {
  return ProxyRequestContextWithInfo<McrouterRouterInfo>::createRecording(
      *getTestRouter()->getProxy(0), nullptr);
}

void mockFiberContext() {
  std::shared_ptr<ProxyRequestContextWithInfo<McrouterRouterInfo>> ctx;
  folly::fibers::runInMainContext([&ctx]() { ctx = getTestContext(); });
  fiber_local<McrouterRouterInfo>::setSharedCtx(std::move(ctx));
}
}
}
} // facebook::memcache::mcrouter
