/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>

#include <folly/File.h>
#include <folly/Range.h>

namespace facebook {
namespace memcache {

struct AccessPoint;
class McrouterOptions;

namespace mcrouter {

class AsyncLog {
 public:
  explicit AsyncLog(const McrouterOptions& options);

  /**
   * Appends a 'delete' request entry to the asynclog.
   * This call blocks until the entry is written to the file
   * or an error occurs.
   */
  void writeDelete(
      const AccessPoint& ap,
      folly::StringPiece key,
      folly::StringPiece poolName);

 private:
  const McrouterOptions& options_;
  std::unique_ptr<folly::File> file_;
  time_t spoolTime_{0};

  /**
   * Open async log file.
   *
   * @return True if the file is ready to use. False otherwise.
   */
  bool openFile();
};
}
}
} // facebook::memcache::mcrouter
