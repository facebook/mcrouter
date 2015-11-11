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

#include <unordered_map>

#include "mcrouter/tools/mcpiper/ClientServerMcParser.h"

namespace facebook { namespace memcache {

class ParserMap {
 public:
  explicit ParserMap(ClientServerMcParser::CallbackFn cb);

  ClientServerMcParser& fetch(uint64_t id);

 private:
  ClientServerMcParser::CallbackFn callback_{nullptr};
  std::unordered_map<uint64_t, ClientServerMcParser> parsers_;
};

}} // facebook::memcache
