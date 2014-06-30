/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "flavor.h"

#include <string>
#include <unordered_map>

#include "folly/FileUtil.h"
#include "folly/Range.h"
#include "folly/json.h"
#include "mcrouter/lib/fbi/debug.h"

using std::string;
using std::unordered_map;

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

folly::StringPiece flavorNameFromFile(folly::StringPiece file) {
  auto lastSlash = file.rfind('/');
  if (lastSlash != string::npos) {
    file.advance(lastSlash + 1);
  }
  file.removeSuffix(".json");
  return file;
}

}

bool parse_json_options(const folly::dynamic& json,
                        const string& field_name,
                        unordered_map<string, string>& opts) {
  auto it = json.find(field_name);
  if (it == json.items().end()) {
    return false;
  }

  auto jopts = it->second;
  if (jopts.type() != folly::dynamic::Type::OBJECT) {
    LOG(ERROR) << "Error parsing flavor config: " << field_name <<
                  " is not an object";
    return false;
  }

  for (auto& jiter: jopts.items()) {
    opts[jiter.first.asString().toStdString()] =
      jiter.second.asString().toStdString();
  }

  return true;
}

bool read_and_fill_from_standalone_flavor_file(
    const string& flavor_file,
    unordered_map<string, string>& libmcrouter_opts,
    unordered_map<string, string>& standalone_opts) {
  try {
    string flavor_config_json;
    if (!folly::readFile(flavor_file.data(), flavor_config_json)) {
      return false;
    }

    auto json = folly::parseJson(flavor_config_json);
    if (!parse_json_options(json, "standalone_options", standalone_opts)) {
      return false;
    }
    if (!parse_json_options(json, "libmcrouter_options", libmcrouter_opts)) {
      return false;
    }
    libmcrouter_opts["router_name"] = flavorNameFromFile(flavor_file).str();

    return true;
  } catch (...) {
    return false;
  }
}

}}}  // facebook::memcache::mcrouter
