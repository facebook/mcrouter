/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <arpa/inet.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace detail {

template <class Request>
class CallbackBase<List<Request>> {
 public:
  virtual ~CallbackBase() = default;
  virtual void multiOpEnd() noexcept = 0;
  virtual void onRequest(Request&& req, bool noreply = false) noexcept = 0;
};

template <class Request, class... Requests>
class CallbackBase<List<Request, Requests...>>
    : public CallbackBase<List<Requests...>> {
 public:
  using CallbackBase<List<Requests...>>::onRequest;

  virtual void onRequest(Request&& req, bool noreply = false) noexcept = 0;
};

template <class Callback, class Requests>
class CallbackWrapper;

template <class Callback, class Request>
class CallbackWrapper<Callback, List<Request>>
    : public CallbackBase<ThriftRequestList> {
 public:
  explicit CallbackWrapper(Callback& callback) : callback_(callback) {}

  void multiOpEnd() noexcept override final { callback_.multiOpEnd(); }

  using CallbackBase<ThriftRequestList>::onRequest;

  void onRequest(Request&& req, bool noreply = false) noexcept override final {
    callback_.onRequest(std::move(req), noreply);
  }

 protected:
  Callback& callback_;
};

template <class Callback, class Request, class... Requests>
class CallbackWrapper<Callback, List<Request, Requests...>>
    : public CallbackWrapper<Callback, List<Requests...>> {
 public:
  explicit CallbackWrapper(Callback& callback)
      : CallbackWrapper<Callback, List<Requests...>>(callback) {}

  using CallbackWrapper<Callback, List<Requests...>>::onRequest;

  void onRequest(Request&& req, bool noreply = false) noexcept override final {
    this->callback_.onRequest(std::move(req), noreply);
  }
};

} // detail

template <class T>
T McClientAsciiParser::getReply() {
  assert(state_ == State::COMPLETE);
  state_ = State::UNINIT;
  return std::move(currentMessage_.get<T>());
}

// Forward-declare initializers.
template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_get>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McGetRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_gets>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McGetsRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_lease_get>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McLeaseGetRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_set>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McSetRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_add>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McAddRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_replace>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McReplaceRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_lease_set>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McLeaseSetRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_cas>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McCasRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_incr>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McIncrRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_decr>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McDecrRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_version>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McVersionRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_delete>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McDeleteRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_touch>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McTouchRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_metaget>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McMetagetRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_flushall>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McFlushAllRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_append>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McAppendRequest>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_prepend>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McPrependRequest>>();

template <class Request>
void McClientAsciiParser::initializeReplyParser() {
  throwLogic(
      "Unexpected call to McAsciiParser::initializeReplyParser "
      "with template arguments [Request = {}]",
      typeid(Request).name());
}

/**
 * Append piece of IOBuf in range [posStart, posEnd) to destination IOBuf.
 */
inline void McAsciiParserBase::appendKeyPiece(const folly::IOBuf& from,
                                              folly::IOBuf& to,
                                              const char* posStart,
                                              const char* posEnd) {
  // No need to process empty piece.
  if (UNLIKELY(posEnd == posStart)) {
    return;
  }

  if (LIKELY(to.length() == 0)) {
    from.cloneOneInto(to);
    trimIOBufToRange(to, posStart, posEnd);
  } else {
    auto nextPiece = from.cloneOne();
    trimIOBufToRange(*nextPiece, posStart, posEnd);
    to.prependChain(std::move(nextPiece));
  }
}

/**
 * Trim IOBuf to reference only data from range [posStart, posEnd).
 */
inline void McAsciiParserBase::trimIOBufToRange(folly::IOBuf& buffer,
                                                const char* posStart,
                                                const char* posEnd) {
  buffer.trimStart(posStart - reinterpret_cast<const char*>(buffer.data()));
  buffer.trimEnd(buffer.length() - (posEnd - posStart));
}

template <class Callback>
McServerAsciiParser::McServerAsciiParser(Callback& callback)
    : callback_(folly::make_unique<
          detail::CallbackWrapper<Callback, ThriftRequestList>>(callback)) {
}

// Only used for McReply to fill in 'value' field for error messages or
// IP addresses.
// TypedThriftReply never uses 'value' field for such purposes.
inline void McClientAsciiParser::stringValueHelper(const folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();

  if (message.hasValue()) {
    // Append to the last IOBuf in chain.
    appendCurrentCharTo(buffer, *message.valueData_, p_);
  } else {
    // Emplace IOBuf.
    // TODO: allocate IOBuf and clone it in one operation.
    message.valueData_.emplace();
    initFirstCharIOBuf(buffer, message.valueData_.value(), p_);
  }
}

template <class Reply>
void McClientAsciiParser::consumeErrorMessage(const folly::IOBuf&) {
  auto& message = currentMessage_.get<Reply>();

  message->message.push_back(*p_);
  message->__isset.message = true;
}

template <>
inline void McClientAsciiParser::consumeErrorMessage<McReply>(
    const folly::IOBuf& buffer) {

  stringValueHelper(buffer);
}

template <class Reply>
void McClientAsciiParser::consumeVersion(const folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Reply>();

  appendCurrentCharTo(buffer, message->value, p_);
  message->__isset.value = true;
}

template <>
inline void McClientAsciiParser::consumeVersion<McReply>(
    const folly::IOBuf& buffer) {

  stringValueHelper(buffer);
}

template <class Reply>
void McClientAsciiParser::consumeIpAddr(const folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Reply>();
  char inAddrBuf[sizeof(struct in6_addr)];
  // Max ip address length is INET6_ADDRSTRLEN - 1 chars.
  if (message->ipAddress.size() < INET6_ADDRSTRLEN) {
    char addr[INET6_ADDRSTRLEN] = {0};
    memcpy(addr, message->ipAddress.data(), message->ipAddress.size());
    if (strchr(addr, ':') == nullptr) {
      if (inet_pton(AF_INET, addr, inAddrBuf) > 0) {
        message->set_ipv(4);
      }
    } else {
      if (inet_pton(AF_INET6, addr, inAddrBuf) > 0) {
        message->set_ipv(6);
      }
    }
  }
}

template <>
inline void McClientAsciiParser::consumeIpAddr<McReply>(
    const folly::IOBuf& buffer) {

  auto& message = currentMessage_.get<McReply>();
  mc_msg_t* mcMsgT = const_cast<mc_msg_t*>(message.msg_.get());

  // Max ip address length is INET6_ADDRSTRLEN - 1 chars.
  if (message.valueData_->computeChainDataLength() < INET6_ADDRSTRLEN) {
    char addr[INET6_ADDRSTRLEN] = {0};
    message.valueData_->coalesce();
    memcpy(addr, message.valueData_->data(), message.valueData_->length());
    mcMsgT->ipv = 0;
    if (strchr(addr, ':') == nullptr) {
      if (inet_pton(AF_INET, addr, &mcMsgT->ip_addr) > 0) {
        mcMsgT->ipv = 4;
      }
    } else {
      if (inet_pton(AF_INET6, addr, &mcMsgT->ip_addr) > 0) {
        mcMsgT->ipv = 6;
      }
    }
  }
}

template <class Reply>
void McClientAsciiParser::consumeIpAddrHelper(const folly::IOBuf&) {
  auto& message = currentMessage_.get<Reply>();

  message->ipAddress.push_back(*p_);
  message->__isset.ipAddress = true;
}

template <>
inline void McClientAsciiParser::consumeIpAddrHelper<McReply>(
    const folly::IOBuf& buffer) {

  stringValueHelper(buffer);
}

inline void McClientAsciiParser::initFirstCharIOBuf(
    const folly::IOBuf& from, folly::IOBuf& to, const char* pos) {

  // Copy current IOBuf.
  from.cloneOneInto(to);
  trimIOBufToRange(to, pos, pos + 1);
}

inline void McClientAsciiParser::appendCurrentCharTo(
    const folly::IOBuf& from, folly::IOBuf& to, const char* pos) {

  // If it is just a next char in the same memory chunk, just append it.
  // Otherwise we need to append new IOBuf.
  if (to.prev()->data() + to.prev()->length() ==
        reinterpret_cast<const void*>(pos) && to.prev()->tailroom() > 0) {
    to.prev()->append(1);
  } else {
    auto nextPiece = from.cloneOne();
    trimIOBufToRange(*nextPiece, pos, pos + 1);
    to.prependChain(std::move(nextPiece));
  }
}

/**
 * McReply needs to clear its existing value before reusing the value field
 * for an error message. This is not needed for TypedThriftReply since
 * we use a different field ('message') for error messages.
 */
template <class Reply>
void McClientAsciiParser::resetErrorMessage(Reply& message) {
  message->__isset.message = false;
  message->message.clear();
}

template <>
inline void McClientAsciiParser::resetErrorMessage<McReply>(McReply& message) {
  message.valueData_.clear();
}

}} // facebook::memcache
