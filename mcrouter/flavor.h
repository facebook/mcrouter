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

namespace folly {
  class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Reads the flavor file from disk.
 * Fills opts from "standalone_options" and "libmcrouter_options" field.
 *
 * Returns false on any errors.
 */
bool read_and_fill_from_standalone_flavor_file(
  const std::string& flavor_file,
  std::unordered_map<std::string, std::string>& libmcrouter_opts,
  std::unordered_map<std::string, std::string>& standalone_opts);

bool parse_json_options(const folly::dynamic& json,
                        const std::string& field_name,
                        std::unordered_map<std::string, std::string>& opts);

}}}  // facebook::memcache::mcrouter
