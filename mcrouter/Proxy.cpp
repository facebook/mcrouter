/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Proxy.h"

#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <chrono>

#include <boost/regex.hpp>

#include <folly/DynamicConverter.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/Range.h>
#include <folly/ThreadName.h>

#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyConfigBuilder.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyRequestContextTyped.h"
#include "mcrouter/RuntimeVarsData.h"
#include "mcrouter/ServiceInfo.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/options.h"
#include "mcrouter/routes/RateLimiter.h"
#include "mcrouter/routes/ShardSplitter.h"
#include "mcrouter/stats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

std::shared_ptr<ShadowSettings> ShadowSettings::create(
    const folly::dynamic& json,
    CarbonRouterInstanceBase& router) {
  auto result = std::shared_ptr<ShadowSettings>(new ShadowSettings());
  try {
    checkLogic(json.isObject(), "json is not an object");

    if (auto jKeyFractionRange = json.get_ptr("key_fraction_range")) {
      checkLogic(
          jKeyFractionRange->isArray(), "key_fraction_range is not an array");
      auto ar = folly::convertTo<std::vector<double>>(*jKeyFractionRange);
      checkLogic(ar.size() == 2, "key_fraction_range size is not 2");
      result->setKeyRange(ar[0], ar[1]);
    }
    if (auto jIndexRange = json.get_ptr("index_range")) {
      checkLogic(jIndexRange->isArray(), "index_range is not an array");
      auto ar = folly::convertTo<std::vector<size_t>>(*jIndexRange);
      checkLogic(ar.size() == 2, "index_range size is not 2");
      checkLogic(ar[0] <= ar[1], "index_range start > end");
      result->startIndex_ = ar[0];
      result->endIndex_ = ar[1];
    }
    if (auto jKeyFractionRangeRv = json.get_ptr("key_fraction_range_rv")) {
      checkLogic(
          jKeyFractionRangeRv->isString(),
          "key_fraction_range_rv is not a string");
      result->keyFractionRangeRv_ = jKeyFractionRangeRv->getString();
    }
    if (auto jKeysToShadow = json.get_ptr("keys_to_shadow")) {
      checkLogic(jKeysToShadow->isArray(), "keys_to_shadow is not an array");
      // Configs cannot mix usage of keys_to_shadow with either index_range,
      // key_fraction_range, or key_fraction_range_rv.
      const auto keysToShadow =
          folly::convertTo<std::vector<std::string>>(*jKeysToShadow);
      checkLogic(
          keysToShadow.empty() || (!json.get_ptr("index_range") &&
                                   !json.get_ptr("key_fraction_range") &&
                                   !json.get_ptr("key_fraction_range_rv")),
          "Cannot mix nonempty keys_to_shadow array with index_range,"
          " key_fraction_range, or key_fraction_range_rv");
      result->setKeysToShadow(keysToShadow);
    }
  } catch (const std::logic_error& e) {
    MC_LOG_FAILURE(
        router.opts(),
        failure::Category::kInvalidConfig,
        "ShadowSettings: {}",
        e.what());
    return nullptr;
  }

  result->registerOnUpdateCallback(router);

  return result;
}

void ShadowSettings::setKeyRange(double start, double end) {
  checkLogic(
      0 <= start && start <= end && end <= 1,
      "invalid key_fraction_range [{}, {}]",
      start,
      end);
  uint64_t keyStart = start * std::numeric_limits<uint32_t>::max();
  uint64_t keyEnd = end * std::numeric_limits<uint32_t>::max();
  keyRange_ = (keyStart << 32UL) | keyEnd;
}

ShadowSettings::~ShadowSettings() {
  /* We must unregister from updates before starting to destruct other
     members, like variable name strings */
  handle_.reset();
}

void ShadowSettings::registerOnUpdateCallback(
    CarbonRouterInstanceBase& router) {
  handle_ = router.rtVarsData().subscribeAndCall([this](
      std::shared_ptr<const RuntimeVarsData> oldVars,
      std::shared_ptr<const RuntimeVarsData> newVars) {
    if (!newVars || keyFractionRangeRv_.empty()) {
      return;
    }
    auto val = newVars->getVariableByName(keyFractionRangeRv_);
    if (val != nullptr) {
      checkLogic(
          val.isArray(),
          "runtime vars: {} is not an array",
          keyFractionRangeRv_);
      checkLogic(
          val.size() == 2,
          "runtime vars: size of {} is not 2",
          keyFractionRangeRv_);
      checkLogic(
          val[0].isNumber(),
          "runtime vars: {}#0 is not a number",
          keyFractionRangeRv_);
      checkLogic(
          val[1].isNumber(),
          "runtime vars: {}#1 is not a number",
          keyFractionRangeRv_);
      setKeyRange(val[0].asDouble(), val[1].asDouble());
    }
  });
}
}
}
} // facebook::memcache::mcrouter
