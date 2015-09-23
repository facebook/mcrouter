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

#include <exception>
#include <typeindex>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/MessageStorage.h"

namespace facebook { namespace memcache {

class McRequest;

class McAsciiParser {
 public:
  enum class State {
    // The parser is not initialized to parse any messages.
    UNINIT,
    // Have partial message, and need more data to complete it.
    PARTIAL,
    // There was an error on the protocol level.
    ERROR,
    // Complete message had been parsed and ready to be returned.
    COMPLETE,
  };

  McAsciiParser();

  McAsciiParser(const McAsciiParser&) = delete;
  McAsciiParser(McAsciiParser&&) = delete;
  McAsciiParser& operator=(const McAsciiParser&) = delete;
  McAsciiParser& operator=(McAsciiParser&&) = delete;

  /**
   * Consume given IOBuf.
   *
   * Should be called only in case hasReadBuffer() returned false.
   *
   * @param buffer  data to consume.
   * @return  new parser state.
   */
  State consume(folly::IOBuf& buffer);

  /**
   * Prepares parser for parsing reply for given request type and operation.
   */
  template<class Operation, class Request>
  void initializeReplyParser();

  /**
   * Obtain the message that was parsed.
   *
   * Should be called by user to obtain reply after consume() returns
   * State::COMPLETE.
   *
   * @tparam T  type of expected reply.
   */
  template<class T>
  T getReply();

  State getCurrentState() { return state_; }

  /**
   * Check if McAsciiParser already has its own buffer.
   * @return  true iff we already have our own buffer that we can read into.
   */
  bool hasReadBuffer() const;

  std::pair<void*, size_t> getReadBuffer();

  void readDataAvailable(size_t length);

  /**
   * Get a human readable description of error cause (e.g. received ERROR
   * reply, or failed to parse some data.)
   */
  folly::StringPiece getErrorDescription() const;
 private:
  void appendCurrentCharTo(folly::IOBuf& from, folly::IOBuf& to);
  void handleError(folly::IOBuf& buffer);

  void initializeCommon();

  void initializeArithmReplyCommon();
  void initializeStorageReplyCommon();

  void consumeArithmReplyCommon(folly::IOBuf& buffer);
  void consumeStorageReplyCommon(folly::IOBuf& buffer);

  template<class Msg, class Op>
  void consumeMessage(folly::IOBuf& buffer);

  std::string currentErrorDescription_;
  uint64_t currentUInt_{0};
  folly::IOBuf* currentIOBuf_{nullptr};
  size_t remainingIOBufLength_{0};
  MessageStorage<List<McReply>> currentMessage_;
  State state_{State::UNINIT};

  // Variables used by ragel.
  int savedCs_;
  int errorCs_;
  const char* p_{nullptr};
  const char* pe_{nullptr};
  const char* eof_{nullptr};
  // Used solely for proper error messages parsing.
  bool stripped_{false};

  using ConsumerFunPtr = void (McAsciiParser::*)(folly::IOBuf&);
  ConsumerFunPtr consumer_{nullptr};
};

template<class T>
T McAsciiParser::getReply() {
  assert(state_ == State::COMPLETE);
  state_ = State::UNINIT;
  return std::move(currentMessage_.get<T>());
}

// Forward-declare initializers.
template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_get>, McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_gets>, McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_lease_get>,
                                          McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_set>, McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_add>, McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_replace>,
                                          McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_lease_set>,
                                          McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_cas>, McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_incr>, McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_decr>, McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_version>,
                                          McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_delete>,
                                          McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_metaget>,
                                          McRequest>();

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_flushall>,
                                          McRequest>();

template<class Operation, class Request>
void McAsciiParser::initializeReplyParser() {
  throw std::logic_error(
    folly::sformat("Unexpected call to McAsciiParser::initializeReplyParser "
                   "with template arguments [Operation = {}, Request = {}]",
                   typeid(Operation).name(), typeid(Request).name()));
}

}}  // facebook::memcache
