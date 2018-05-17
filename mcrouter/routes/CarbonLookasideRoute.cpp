/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include <memory>
#include <mutex>
#include <unordered_map>

#include <folly/MapUtil.h>
#include <folly/Range.h>

#include "mcrouter/CarbonRouterFactory.h"
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/routes/CarbonLookasideRoute.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook {
namespace memcache {
namespace mcrouter {
namespace detail {

namespace {

class CarbonLookasideManager {
 public:
  std::shared_ptr<CarbonRouterInstance<MemcacheRouterInfo>>
  createCarbonLookasideRouter(
      const std::string& persistenceId,
      folly::StringPiece flavorUri,
      std::unordered_map<std::string, std::string> optionOverrides) {
    std::lock_guard<std::mutex> ilg(initMutex_);
    std::shared_ptr<CarbonRouterInstance<MemcacheRouterInfo>> mcrouter =
        folly::get_default(mcroutersLookaside_, persistenceId).lock();
    if (!mcrouter) {
      mcrouter = createRouterFromFlavor<MemcacheRouterInfo>(
          flavorUri, optionOverrides);
      mcroutersLookaside_[persistenceId] = mcrouter;
    }
    return mcrouter;
  }

 private:
  std::mutex initMutex_;
  std::unordered_map<
      std::string,
      std::weak_ptr<CarbonRouterInstance<MemcacheRouterInfo>>>
      mcroutersLookaside_;
};

folly::Singleton<CarbonLookasideManager> gCarbonLookasideManager;
} // namespace

std::shared_ptr<CarbonRouterInstance<MemcacheRouterInfo>>
createCarbonLookasideRouter(
    const std::string& persistenceId,
    folly::StringPiece flavorUri,
    std::unordered_map<std::string, std::string> optionOverrides) {
  if (auto manager = gCarbonLookasideManager.try_get()) {
    return manager->createCarbonLookasideRouter(
        persistenceId, flavorUri, optionOverrides);
  }
  return nullptr;
}

} // namespace detail
} // namespace mcrouter
} // namespace memcache
} // namespace facebook
