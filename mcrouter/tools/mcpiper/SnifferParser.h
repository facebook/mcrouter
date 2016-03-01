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

#include "mcrouter/lib/Operation.h"
#include "mcrouter/tools/mcpiper/ClientServerMcParser.h"

namespace facebook { namespace memcache {

/**
 * Wrapper around ClientServerMcParser that also tracks of information
 * useful for sniffer (e.g. socket addresses, keys for replies).
 *
 * @param Callback  Callback containing two functions:
 *                  void requestReady(msgId, request, fromAddress, toAddress);
 *                  void replyReady(msgId, reply, key, fromAddress, toAddress);
 */
template <class Callback>
class SnifferParser {
 public:
  explicit SnifferParser(Callback& cb) noexcept;

  ClientServerMcParser<SnifferParser>& parser() {
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
  Callback& callback_;
  // The parser itself.
  ClientServerMcParser<SnifferParser> parser_;
  // Addresses of current message.
  folly::SocketAddress fromAddress_;
  folly::SocketAddress toAddress_;
  // Map (msgId -> key) of messages that haven't been paired yet.
  std::unordered_map<uint64_t, Item> msgs_;
  // Keeps an in-order list of what should be invalidated.
  folly::IntrusiveList<Item, &Item::listHook> evictionQueue_;

  void evictOldItems(TimePoint now);

  // ClientServerMcParser callbacks
  template <class Request>
  void requestReady(uint64_t msgId, Request request);
  template <class Request>
  void replyReady(uint64_t msgId, ReplyT<Request> reply);

  friend class ClientServerMcParser<SnifferParser>;
};

}} // facebook::memcache

#include "SnifferParser-inl.h"
