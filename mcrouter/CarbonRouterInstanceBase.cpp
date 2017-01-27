/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CarbonRouterInstanceBase.h"

#include <boost/filesystem/operations.hpp>

#include <folly/Memory.h>
#include <folly/ThreadName.h>

#include "mcrouter/AsyncWriter.h"
#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

CarbonRouterInstanceBase::CarbonRouterInstanceBase(McrouterOptions inputOptions)
    : opts_(std::move(inputOptions)),
      pid_(getpid()),
      configApi_(createConfigApi(opts_)),
      statsLogWriter_(
          folly::make_unique<AsyncWriter>(opts_.stats_async_queue_length)),
      asyncWriter_(folly::make_unique<AsyncWriter>()),
      rtVarsData_(std::make_shared<ObservableRuntimeVars>()),
      leaseTokenMap_(folly::make_unique<LeaseTokenMap>(evbAuxiliaryThread_)) {
  evbAuxiliaryThread_.getEventBase()->runInEventBaseThread(
      [] { folly::setThreadName("CarbonAux"); });
}

void CarbonRouterInstanceBase::setUpCompressionDictionaries(
    std::unordered_map<uint32_t, CodecConfigPtr>&& codecConfigs) noexcept {
  if (codecConfigs.empty() || compressionCodecManager_ != nullptr) {
    return;
  }
  compressionCodecManager_ = folly::make_unique<const CompressionCodecManager>(
      std::move(codecConfigs));
}

void CarbonRouterInstanceBase::addStartupOpts(
    std::unordered_map<std::string, std::string> additionalOpts) {
  additionalStartupOpts_.insert(additionalOpts.begin(), additionalOpts.end());
}

std::unordered_map<std::string, std::string>
CarbonRouterInstanceBase::getStartupOpts() const {
  constexpr size_t kMaxOptionValueLength = 256;

  auto result = opts_.toDict();
  result.insert(additionalStartupOpts_.begin(), additionalStartupOpts_.end());
  result.emplace("version", MCROUTER_PACKAGE_STRING);
  for (auto& it : result) {
    it.second = shorten(it.second, kMaxOptionValueLength);
  }
  return result;
}

size_t CarbonRouterInstanceBase::nextProxyIndex() {
  std::lock_guard<std::mutex> guard(nextProxyMutex_);
  assert(nextProxy_ < opts().num_proxies);
  size_t res = nextProxy_;
  nextProxy_ = (nextProxy_ + 1) % opts().num_proxies;
  return res;
}
}
}
} // facebook::memcache::mcrouter
