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
#include <unordered_map>

#include <folly/dynamic.h>
#include <folly/Range.h>

namespace facebook { namespace memcache { namespace mcrouter {

class RuntimeVarsData {
 public:
  RuntimeVarsData() = default;
  explicit RuntimeVarsData(folly::StringPiece json);

  /**
   * Returns the value of the variable with key = name.
   *
   * @param name key of the data to be retrieved
   * @return Variable value, or null if key not found
   */
  folly::dynamic getVariableByName(const std::string& name) const;

 private:
  std::unordered_map<std::string, folly::dynamic> configData_;
};

}}} // facebook::memcache::mcrouter
