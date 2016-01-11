/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <chrono>
#include <unordered_map>

#include <folly/IntrusiveList.h>
#include <folly/SocketAddress.h>

#include "mcrouter/tools/mcpiper/ClientServerMcParser.h"

namespace facebook { namespace memcache {

/**
 * Wrapper around the parser that keeps context information
 * (e.g. map of messages to be paired).
 */
class ParserContext {
 public:
  using Callback = std::function<void(uint64_t reqId,
                                      McMsgRef msg,
                                      std::string matchingMsgKey,
                                      const folly::SocketAddress& fromAddress,
                                      const folly::SocketAddress& toAddress)>;

  explicit ParserContext(const Callback& cb) noexcept;

  ClientServerMcParser& parser() {
    return parser_;
  }

  void setAddresses(folly::SocketAddress fromAddress,
                    folly::SocketAddress toAddress) {
    fromAddress_ = std::move(fromAddress);
    toAddress_ = std::move(toAddress);
  }

 private:
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  // Holds the id of the request and the key of the matching message.
  struct Item {
    Item(uint64_t id, std::string k, TimePoint now)
        : reqId(id),
          key(std::move(k)),
          created(now) { }

    uint64_t reqId;
    std::string key;
    TimePoint created;

    folly::IntrusiveListHook listHook;
  };

  // Callback called when a message is ready
  const Callback& callback_;
  // The parser itself.
  ClientServerMcParser parser_;
  // Addresses of current message.
  folly::SocketAddress fromAddress_;
  folly::SocketAddress toAddress_;
  // Map (reqid -> key) of messages that haven't been paired yet.
  std::unordered_map<uint64_t, Item> msgs_;
  // Keeps an in-order list of what should be invalidated.
  folly::IntrusiveList<Item, &Item::listHook> evictionQueue_;

  void msgReady(uint64_t id, McMsgRef msg);
  void evictOldItems(TimePoint now);
};

/**
 * Map of parsers
 */
class ParserMap {
 public:
  explicit ParserMap(ParserContext::Callback cb) noexcept;

  ParserContext& fetch(uint64_t id);

 private:
  ParserContext::Callback callback_{nullptr};
  std::unordered_map<uint64_t, ParserContext> parsers_;
};

}} // facebook::memcache
