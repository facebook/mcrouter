/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "RouteHandleTestUtil.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterInstance* getTestRouter() {
  McrouterOptions opts = defaultTestOptions();
  opts.config_str = "{ \"route\": \"NullRoute\" }";
  return McrouterInstance::init("test", opts);
}

std::shared_ptr<ProxyRequestContext> getTestContext() {
  return ProxyRequestContext::createRecording(*getTestRouter()->getProxy(0),
                                              nullptr);
}

void mockFiberContext() {
  std::shared_ptr<ProxyRequestContext> ctx;
  folly::fibers::runInMainContext([&ctx](){
    ctx = getTestContext();
  });
  fiber_local::setSharedCtx(std::move(ctx));
}

}}} // facebook::memcache::mcrouter
