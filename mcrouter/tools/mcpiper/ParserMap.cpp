/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ParserMap.h"

namespace facebook { namespace memcache {

ParserMap::ParserMap(ClientServerMcParser::CallbackFn callback)
    : callback_(std::move(callback)) {
}

ClientServerMcParser& ParserMap::fetch(uint64_t id) {
  auto it = parsers_.find(id);
  if (it == parsers_.end()) {
    it = parsers_.emplace(std::piecewise_construct,
                          std::forward_as_tuple(id),
                          std::forward_as_tuple(callback_)).first;
  }
  return it->second;
}

}} // facebook::memcache
