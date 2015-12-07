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

#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/MessageStorage.h"

namespace facebook { namespace memcache {

class McAsciiParserBase {
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

  McAsciiParserBase() = default;

  McAsciiParserBase(const McAsciiParserBase&) = delete;
  McAsciiParserBase& operator=(const McAsciiParserBase&) = delete;

  State getCurrentState() const noexcept { return state_; }

  /**
   * Check if McAsciiParser already has its own buffer.
   * @return  true iff we already have our own buffer that we can read into.
   */
  bool hasReadBuffer() const noexcept;

  std::pair<void*, size_t> getReadBuffer() noexcept;

  void readDataAvailable(size_t length);

  /**
   * Get a human readable description of error cause (e.g. received ERROR
   * reply, or failed to parse some data.)
   */
  folly::StringPiece getErrorDescription() const;
 protected:
  void handleError(folly::IOBuf& buffer);
  /**
   * Read value data.
   * It uses remainingIOBufLength_ to determine how much we need to read. It
   * will also update that variable and currentIOBuf_ accordingly.
   *
   * @return true iff the value was completely read.
   */
  bool readValue(folly::IOBuf& buffer, folly::IOBuf& to);

  std::string currentErrorDescription_;

  uint64_t currentUInt_{0};

  folly::IOBuf* currentIOBuf_{nullptr};
  size_t remainingIOBufLength_{0};
  State state_{State::UNINIT};
  bool negative_{false};

  // Variables used by ragel.
  int savedCs_;
  int errorCs_;
  const char* p_{nullptr};
  const char* pe_{nullptr};
};

class McClientAsciiParser : public McAsciiParserBase {
 public:
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
 private:
  void initializeCommon();

  void initializeArithmReplyCommon();
  void initializeStorageReplyCommon();

  void consumeArithmReplyCommon(folly::IOBuf& buffer);
  void consumeStorageReplyCommon(folly::IOBuf& buffer);

  template<class Msg, class Op>
  void consumeMessage(folly::IOBuf& buffer);

  MessageStorage<List<McReply>> currentMessage_;

  using ConsumerFunPtr = void (McClientAsciiParser::*)(folly::IOBuf&);
  ConsumerFunPtr consumer_{nullptr};
};

namespace detail {
using SupportedReqs = List<Pair<McOperation<mc_op_get>, McRequest>,
                           Pair<McOperation<mc_op_gets>, McRequest>,
                           Pair<McOperation<mc_op_lease_get>, McRequest>,
                           Pair<McOperation<mc_op_metaget>, McRequest>,
                           Pair<McOperation<mc_op_set>, McRequest>,
                           Pair<McOperation<mc_op_add>, McRequest>,
                           Pair<McOperation<mc_op_replace>, McRequest>,
                           Pair<McOperation<mc_op_append>, McRequest>,
                           Pair<McOperation<mc_op_prepend>, McRequest>,
                           Pair<McOperation<mc_op_cas>, McRequest>,
                           Pair<McOperation<mc_op_lease_set>, McRequest>,
                           Pair<McOperation<mc_op_delete>, McRequest>,
                           Pair<McOperation<mc_op_shutdown>, McRequest>,
                           Pair<McOperation<mc_op_incr>, McRequest>,
                           Pair<McOperation<mc_op_decr>, McRequest>,
                           Pair<McOperation<mc_op_version>, McRequest>,
                           Pair<McOperation<mc_op_quit>, McRequest>,
                           Pair<McOperation<mc_op_stats>, McRequest>,
                           Pair<McOperation<mc_op_exec>, McRequest>,
                           Pair<McOperation<mc_op_flushre>, McRequest>,
                           Pair<McOperation<mc_op_flushall>, McRequest>,
                           Pair<McOperation<mc_op_touch>, McRequest>>;

template <class OpReqList> class CallbackBase;
}  // detail

class McServerAsciiParser : public McAsciiParserBase {
 public:
  template <class Callback>
  explicit McServerAsciiParser(Callback& cb);

  /**
   * Consume given IOBuf.
   *
   * Should be called only in case hasReadBuffer() returned false.
   *
   * @param buffer  data to consume.
   * @return  new parser state.
   */
  State consume(folly::IOBuf& buffer);
 private:
  void opTypeConsumer(folly::IOBuf& buffer);

  // Get-like.
  template <class Operation, class Request>
  void initGetLike();
  template <class Operation, class Request>
  void consumeGetLike(folly::IOBuf& buffer);

  // Update-like.
  template <class Operation, class Request>
  void initSetLike();
  template <class Operation, class Request>
  void consumeSetLike(folly::IOBuf& buffer);
  void consumeCas(folly::IOBuf& buffer);
  void consumeLeaseSet(folly::IOBuf& buffer);

  void consumeDelete(folly::IOBuf& buffer);
  void consumeTouch(folly::IOBuf& buffer);

  void consumeShutdown(folly::IOBuf& buffer);

  // Arithmetic.
  template <class Operation, class Request>
  void initArithmetic();
  template <class Operation, class Request>
  void consumeArithmetic(folly::IOBuf& buffer);

  void consumeStats(folly::IOBuf& buffer);
  void consumeExec(folly::IOBuf& buffer);

  // Flush.
  void consumeFlushRe(folly::IOBuf& buffer);
  void consumeFlushAll(folly::IOBuf& buffer);

  void finishReq();

  std::unique_ptr<detail::CallbackBase<detail::SupportedReqs>> callback_;

  const char* keyPieceStart_{nullptr};
  folly::IOBuf currentKey_;
  bool noreply_{false};
  MessageStorage<List<McRequest>> currentMessage_;

  using ConsumerFunPtr = void (McServerAsciiParser::*)(folly::IOBuf&);
  ConsumerFunPtr consumer_{nullptr};
};

}}  // facebook::memcache

#include "McAsciiParser-inl.h"
