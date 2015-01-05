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

#include <string>

#include <folly/File.h>

namespace facebook { namespace memcache { namespace mcrouter {
/**
 * DataProvider that works with file: loads data from file and checks
 * if the file has changed.
 */
class FileDataProvider {
 public:
  /**
   * Registers inotify watch to check for file changes
   *
   * @param filePath path to file or link
   * @throw runtime_error if watch can not be created
   */
  explicit FileDataProvider(std::string filePath);

  /**
   * @return contents of file
   * @throw runtime_error if file can not be opened
   */
  std::string load() const;

  /**
   * Polls inotify watch to check if file has changed. Also recreates the
   * inotify watch in case file link changed/file was deleted and created again.
   *
   * @return true if file has changed since last hasUpdate call, false otherwise
   * @throw runtime_error if inotify watch can not be checked or recreated
   */
  bool hasUpdate();
 private:
  const std::string filePath_;
  folly::File inotify_;

  /**
   * Updates the inotify watch.
   * Provides strong guarantee: if exception is thrown, state won't change.
   */
  void updateInotifyWatch();
};

}}} // facebook::memcache::mcrouter
