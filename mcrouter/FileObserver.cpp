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

#include <glog/logging.h>

#include "mcrouter/FileDataProvider.h"
#include "mcrouter/PeriodicTaskScheduler.h"

namespace facebook { namespace memcache { namespace mcrouter {

static void checkAndExecuteFallbackOnError(
    std::function<void()> fallbackOnError) {
  if (fallbackOnError) {
    LOG(INFO) << "Calling the fallbackOnError function";
    fallbackOnError();
  }
}

bool FileObserver::startObserving(const std::string& filePath,
                                  PeriodicTaskScheduler& taskScheduler,
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
    checkAndExecuteFallbackOnError(fallbackOnError);
    return false;
  }

  VLOG(0) << "Watching " << filePath << " for modifications.";
  taskScheduler.scheduleTask(
    /* tmo_ms = */ pollPeriodMs,
    [onUpdate, fallbackOnError, sleepBeforeUpdateMs, provider]
    (PeriodicTaskScheduler& scheduler) {
      try {
        if (!provider->hasUpdate()) {
          return;
        }

        scheduler.sleepThread(sleepBeforeUpdateMs);
        onUpdate(provider->load());
      } catch (...) {
        checkAndExecuteFallbackOnError(fallbackOnError);
        throw;
      }
    }
  );
  return true;
}

}}} // namespace
