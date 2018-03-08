/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
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
