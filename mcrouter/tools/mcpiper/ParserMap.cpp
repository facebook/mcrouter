/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ParserMap.h"

#include <folly/Conv.h>

namespace facebook { namespace memcache {

namespace {

// Timeout to evict unused matching messages' key.
const std::chrono::milliseconds kMatchingKeyTimeout{5000};

} // anonymous namespace

ParserContext::ParserContext(
    const Callback& cb) noexcept
    : callback_(cb),
      parser_([this](uint64_t id, McMsgRef msg) {
        msgReady(id, std::move(msg));
      }) {
}

void ParserContext::msgReady(uint64_t id, McMsgRef msg) {
  TimePoint now = Clock::now();
  evictOldItems(now);

  std::string invKey;
  if (id != 0) {
    auto pairMsgIt = msgs_.find(id);
    if (pairMsgIt != msgs_.end()) {
      invKey = std::move(pairMsgIt->second.key);
      msgs_.erase(pairMsgIt->first);
    }
    if (msg->key.len > 0) {
      auto msgIt = msgs_.emplace(
          id,
          Item(id, to<std::string>(msg->key), now));
      evictionQueue_.push_back(msgIt.first->second);
    }
  }
  callback_(id, std::move(msg), std::move(invKey), fromAddress_, toAddress_);
}

void ParserContext::evictOldItems(TimePoint now) {
  TimePoint oldest = now - kMatchingKeyTimeout;
  auto cur = evictionQueue_.begin();
  while (cur != evictionQueue_.end() && cur->created <= oldest) {
    auto reqId = cur->reqId;
    cur = evictionQueue_.erase(cur);
    msgs_.erase(reqId);
  }
}

ParserMap::ParserMap(ParserContext::Callback callback) noexcept
    : callback_(std::move(callback)) {
}

ParserContext& ParserMap::fetch(uint64_t id) {
  auto it = parsers_.find(id);
  if (it == parsers_.end()) {
    it = parsers_.emplace(std::piecewise_construct,
                          std::forward_as_tuple(id),
                          std::forward_as_tuple(callback_)).first;
  }
  return it->second;
}

}} // facebook::memcache
