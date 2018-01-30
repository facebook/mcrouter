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

namespace facebook {
namespace memcache {

namespace detail {

// Timeout to evict unused matching messages' key.
const std::chrono::milliseconds kMatchingKeyTimeout{5000};

} // detail namespace

template <class Callback, class RequestList>
SnifferParser<Callback, RequestList>::SnifferParser(Callback& cb) noexcept
    : SnifferParserBase<Callback>(cb), parser_(*this) {}

template <class Callback>
void SnifferParserBase<Callback>::evictOldItems(TimePoint now) {
  TimePoint oldest = now - detail::kMatchingKeyTimeout;
  auto cur = evictionQueue_.begin();
  while (cur != evictionQueue_.end() && cur->created <= oldest) {
    auto reqId = cur->reqId;
    cur = evictionQueue_.erase(cur);
    msgs_.erase(reqId);
  }
}

template <class Callback>
template <class Request>
void SnifferParserBase<Callback>::requestReady(
    uint64_t msgId,
    Request&& request) {
  TimePoint now = Clock::now();
  evictOldItems(now);

  if (msgId != 0) {
    auto msgIt = msgs_.emplace(
        msgId,
        Item(
            msgId, request.key().fullKey().str(), currentMsgStartTimeUs_, now));
    evictionQueue_.push_back(msgIt.first->second);
  }
  callback_.requestReady(
      msgId, std::move(request), fromAddress_, toAddress_, getParserProtocol());
}

template <class Callback>
template <class Reply>
void SnifferParserBase<Callback>::replyReady(
    uint64_t msgId,
    Reply&& reply,
    ReplyStatsContext replyStatsContext) {
  std::string key;
  int64_t latency = 0;
  if (msgId != 0) {
    auto pairMsgIt = msgs_.find(msgId);
    if (pairMsgIt != msgs_.end()) {
      key = std::move(pairMsgIt->second.key);
      latency = currentMsgStartTimeUs_ - pairMsgIt->second.msgStartTimeUs;
      msgs_.erase(pairMsgIt->first);
    }
  }
  callback_.replyReady(
      msgId,
      std::move(reply),
      std::move(key),
      fromAddress_,
      toAddress_,
      getParserProtocol(),
      latency,
      replyStatsContext);
}
}
} // facebook::memcache
