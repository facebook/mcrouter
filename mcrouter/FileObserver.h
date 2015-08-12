/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <functional>
#include <string>

namespace folly {
class EventBase;
}

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Starts a periodic thread that watches the given file path for changes.
 *
 * @param filePath path to the file to watch (can be a symlink)
 * @param pollPeriodMs how much to wait between asking inotify if
 *        any updates happened
 * @param sleepAfterUpdateMs how much to wait before calling onUpdate
 *        once an inotify event happens (as a crude protection against
 *        partial writes race condition).
 * @param onUpdate callback function to call when there is a update seen
 * @param fallbackOnError function to call if inotify calls fails
 */
bool startObservingFile(const std::string& filePath,
                        folly::EventBase& evb,
                        uint32_t pollPeriodMs,
                        uint32_t sleepBeforeUpdateMs,
                        std::function<void(std::string)> onUpdate,
                        std::function<void()> fallbackOnError = nullptr);

}}} // namespace
