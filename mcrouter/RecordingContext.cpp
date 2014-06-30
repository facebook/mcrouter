/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "RecordingContext.h"

#include "folly/IPAddress.h"
#include "folly/Memory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache { namespace mcrouter {

RecordingContext::RecordingContext()
    : recordedData_(folly::make_unique<RecordedData>()) {
}

RecordingContext::~RecordingContext() {
  if (promise_) {
    promise_->setValue(std::move(recordedData_));
  }
}

void RecordingContext::recordDestination(const ProxyClientCommon& destination) {
  std::string fullAddress;
  const auto& host = destination.ap.getHost();
  const auto& port = destination.ap.getPort();
  try {
    folly::IPAddress destIP(host);
    fullAddress = joinHostPort(destIP, port);
  } catch (const folly::IPAddressFormatException& e) {
    // host is not IP address (e.g. 'localhost')
    fullAddress = host + ":" + port;
  }
  recordedData_->destinations.push_back(fullAddress);
}

std::unique_ptr<RecordingContext::RecordedData>
RecordingContext::waitForRecorded(
  std::shared_ptr<RecordingContext>&& ctx) {

  if (ctx.unique()) {
    /* This was the last reference, nothing to do */
    return std::move(ctx->recordedData_);
  }

  /* Make sure we get notified on destruction */
  return
    fiber::await([&ctx](FiberPromise<std::unique_ptr<RecordedData>> promise) {
        ctx->promise_ = std::move(promise);

        /* Surrender the reference we hold */
        ctx.reset();
      });
}

}}}
