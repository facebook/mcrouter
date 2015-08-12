/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FileObserver.h"

#include <memory>
#include <vector>

#include <glog/logging.h>

#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>

#include "mcrouter/FileDataProvider.h"
#include "mcrouter/McrouterLogFailure.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

void checkAndExecuteFallbackOnError(std::function<void()> fallbackOnError) {
  if (fallbackOnError) {
    LOG(INFO) << "Calling the fallbackOnError function";
    fallbackOnError();
  }
}

struct FileObserverData {
  FileObserverData(std::shared_ptr<FileDataProvider> provider__,
                   std::function<void(std::string)> onUpdate__,
                   std::function<void()> fallbackOnError__,
                   uint32_t pollPeriodMs__,
                   uint32_t sleepBeforeUpdateMs__)
      : provider(std::move(provider__)),
        onUpdate(std::move(onUpdate__)),
        fallbackOnError(std::move(fallbackOnError__)),
        pollPeriodMs(pollPeriodMs__),
        sleepBeforeUpdateMs(sleepBeforeUpdateMs__) {}
  std::shared_ptr<FileDataProvider> provider;
  std::function<void(std::string)> onUpdate;
  std::function<void()> fallbackOnError;
  uint32_t pollPeriodMs;
  uint32_t sleepBeforeUpdateMs;
};

void scheduleObserveFile(folly::EventBase& evb, FileObserverData data) {
  evb.runAfterDelay([&evb, data = std::move(data)]() {
    bool hasUpdate;
    try {
      hasUpdate = data.provider->hasUpdate();
    } catch (...) {
      checkAndExecuteFallbackOnError(std::move(data.fallbackOnError));
      LOG_FAILURE("mcrouter", failure::Category::kOther,
                  "Error while observing file for update");
      return;
    }

    if (hasUpdate) {
      evb.runAfterDelay([&evb, data = std::move(data)]() {
        try {
          data.onUpdate(data.provider->load());
          scheduleObserveFile(evb, std::move(data));
        } catch (...) {
          checkAndExecuteFallbackOnError(std::move(data.fallbackOnError));
          LOG_FAILURE("mcrouter", failure::Category::kOther,
                      "Error while observing file for update");
        }
      }, data.sleepBeforeUpdateMs);
    } else {
      scheduleObserveFile(evb, std::move(data));
    }
  }, data.pollPeriodMs);
}

} // anonymous namespace

bool startObservingFile(const std::string& filePath,
                        folly::EventBase& evb,
                        uint32_t pollPeriodMs,
                        uint32_t sleepBeforeUpdateMs,
                        std::function<void(std::string)> onUpdate,
                        std::function<void()> fallbackOnError) {

  std::shared_ptr<FileDataProvider> provider;
  try {
    provider = std::make_shared<FileDataProvider>(filePath);

    onUpdate(provider->load());
  } catch (const std::exception& e) {
    VLOG(0) << "Can not start watching " << filePath <<
               " for modifications: " << e.what();
    checkAndExecuteFallbackOnError(std::move(fallbackOnError));
    return false;
  }

  VLOG(0) << "Watching " << filePath << " for modifications.";
  FileObserverData data(std::move(provider),
                        std::move(onUpdate),
                        std::move(fallbackOnError),
                        pollPeriodMs,
                        sleepBeforeUpdateMs);
  return evb.runInEventBaseThread([&evb, data = std::move(data)]() {
    scheduleObserveFile(evb, std::move(data));
  });
}

}}} // namespace
