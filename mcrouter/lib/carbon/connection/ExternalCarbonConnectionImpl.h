/*
 *  Copyright (c) 2017-present, Facebook, Inc.
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
#include <unordered_map>
#include <vector>

#include "mcrouter/lib/CacheClientStats.h"
#include "mcrouter/lib/carbon/connection/CarbonConnectionUtil.h"
#include "mcrouter/lib/fbi/counting_sem.h"
#include "mcrouter/lib/network/AsyncMcClient.h"

namespace carbon {

class ExternalCarbonConnectionImpl {
 public:
  struct Options {
    Options() {}

    size_t maxOutstanding{0};
    bool maxOutstandingError{false};
  };

  explicit ExternalCarbonConnectionImpl(
      facebook::memcache::ConnectionOptions connectionOptions,
      Options options = Options());

  ~ExternalCarbonConnectionImpl() = default;

  facebook::memcache::CacheClientCounters getStatCounters() const noexcept {
    // TODO: add real stats
    return {};
  }

  std::unordered_map<std::string, std::string> getConfigOptions() {
    // TODO:: add real options
    return std::unordered_map<std::string, std::string>();
  }

  bool healthCheck();

  template <class Request>
  void sendRequestOne(const Request& req, RequestCb<Request> cb);

  template <class Request>
  void sendRequestMulti(
      std::vector<std::reference_wrapper<const Request>>&& reqs,
      RequestCb<Request> cb);

  template <class T>
  std::unique_ptr<T> recreate() {
    LOG(FATAL)
        << "This should not be called, recreation is handled internally.";
    return nullptr; // unreachable, silence compiler errors
  }

 private:
  facebook::memcache::ConnectionOptions connectionOptions_;
  Options options_;

  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // carbon

#include "ExternalCarbonConnectionImpl-inl.h"
