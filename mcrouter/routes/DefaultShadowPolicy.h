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

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Default shadow policy: send exactly the same request to shadow
 * as the original; send out shadow requests right away.
 */
class DefaultShadowPolicy {
 public:
  template <class Request>
  static Request updateRequestForShadowing(const Request& req) {
    return req.clone();
  }

  template <class Request>
  static bool shouldDelayShadow(const Request& req) {
    return false;
  }
};

}}}  // facebook::memcache::mcrouter
