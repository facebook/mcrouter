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

#include <iostream>

#include <boost/regex.hpp>
#include <folly/IPAddress.h>
#include <folly/SocketAddress.h>

#include "mcrouter/tools/mcpiper/AnsiColorCodeStream.h"
#include "mcrouter/tools/mcpiper/PrettyFormat.h"
#include "mcrouter/tools/mcpiper/SnifferParser.h"
#include "mcrouter/tools/mcpiper/StyledString.h"
#include "mcrouter/tools/mcpiper/ValueFormatter.h"

namespace facebook { namespace memcache {

/**
 * Class responsible for formatting and printing requests and replies.
 */
class MessagePrinter {
 public:
  /**
   * Format settings
   */
  struct Options {
    // Function responsible for printing the time of the messages.
    std::function<std::string(const struct timeval& ts)> printTimeFn{nullptr};

    // Number of messages to show after a match is found.
    uint32_t numAfterMatch{0};

    // If true, does not print values.
    bool quiet{false};

    // Number of messages to print before exiting (i.e. calling stopRunningFn).
    // 0 to disable.
    uint32_t maxMessages{0};

    // Disable nice coloring.
    bool disableColor{false};

    // Callback that will be called when application should stop
    // sending messages to MessagePrinter.
    std::function<void(const MessagePrinter&)> stopRunningFn =
      [](const MessagePrinter&) { exit(0); };
  };

  /**
   * Filter to be applied to requests/replies
   */
  struct Filter {
    folly::IPAddress host;
    uint16_t port{0};
    uint32_t valueMinSize{0};
    std::unique_ptr<boost::regex> pattern;
    bool invertMatch{false};
  };

  /**
   * Builds a message printer.
   *
   * @param options         General options.
   * @param filter          Message filter.
   * @param valueFormatter  Class used to format the values of messages.
   */
  MessagePrinter(Options options,
                 Filter filter,
                 std::unique_ptr<ValueFormatter> valueFormatter);

  /**
   * Return status of message printer.
   *
   * @return  A pair: (numMessagesReceives, numMessagesPrinted).
   */
  inline std::pair<uint64_t, uint64_t> getStats() const noexcept {
    return std::make_pair(totalMessages_, printedMessages_);
  }

  template <class Message>
  void printMessage(uint64_t msgId,
                    Message message,
                    const std::string& key,
                    mc_op_t op,
                    mc_res_t result,
                    const folly::SocketAddress& from,
                    const folly::SocketAddress& to);

 private:
  const Options options_;
  const Filter filter_;
  const PrettyFormat format_{}; // Default constructor = default coloring.

  AnsiColorCodeStream targetOut_{std::cout};
  std::unique_ptr<ValueFormatter> valueFormatter_;
  uint64_t totalMessages_{0};
  uint64_t printedMessages_{0};
  uint32_t afterMatchCount_{0};

  // SnifferParser Callbacks
  template <class Request>
  void requestReady(uint64_t msgId,
                    Request request,
                    const folly::SocketAddress& from,
                    const folly::SocketAddress& to);
  template <class Request>
  void replyReady(uint64_t msgId,
                  ReplyT<Request> reply,
                  std::string key,
                  const folly::SocketAddress& from,
                  const folly::SocketAddress& to);
  friend class SnifferParser<MessagePrinter>;

  /**
   * Tells whether a message matches the ip/port filter.
   *
   * @param from  Address that sent the message.
   * @param to    Address to where the message was sent.
   *
   * @return      True if the addresses matches the specified filters
   *              (and thus should be printed). False otherwise.
   */
  bool matchAddress(const folly::SocketAddress& from,
                    const folly::SocketAddress& to) const;

  /**
   * Matches all the occurences of "pattern" in "text"
   *
   * @return A vector of pairs containing the index and size (respectively)
   *         of all ocurrences.
   */
  std::vector<std::pair<size_t, size_t>> matchAll(
      folly::StringPiece text, const boost::regex& pattern) const;

  std::string serializeAddresses(const folly::SocketAddress& from,
                                 const folly::SocketAddress& to);

  std::string serializeMessageHeader(mc_op_t op,
                                     mc_res_t result,
                                     const std::string& key);
};

}} // facebook::memcache

#include "MessagePrinter-inl.h"
