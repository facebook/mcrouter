/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include <arpa/inet.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/IOBufUtil.h"

namespace facebook {
namespace memcache {
namespace detail {

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
    : public CallbackBase<McRequestList> {
 public:
  explicit CallbackWrapper(Callback& callback) : callback_(callback) {}

  void multiOpEnd() noexcept final {
    callback_.multiOpEnd();
  }

  using CallbackBase<McRequestList>::onRequest;

  void onRequest(Request&& req, bool noreply = false) noexcept final {
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

  void onRequest(Request&& req, bool noreply = false) noexcept final {
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
void McClientAsciiParser::initializeReplyParser<McGetRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McGetsRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McLeaseGetRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McSetRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McAddRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McReplaceRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McLeaseSetRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McCasRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McIncrRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McDecrRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McVersionRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McDeleteRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McTouchRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McMetagetRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McFlushAllRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McAppendRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McPrependRequest>();

template <class Request>
void McClientAsciiParser::initializeReplyParser() {
  throwLogic(
      "Unexpected call to McAsciiParser::initializeReplyParser "
      "with template arguments [Request = {}]",
      typeid(Request).name());
}

template <class Callback>
McServerAsciiParser::McServerAsciiParser(Callback& callback)
    : callback_(
          std::make_unique<detail::CallbackWrapper<Callback, McRequestList>>(
              callback)) {}

template <class Reply>
void McClientAsciiParser::consumeErrorMessage(const folly::IOBuf&) {
  auto& message = currentMessage_.get<Reply>();

  message.message().push_back(*p_);
}

template <class Reply>
void McClientAsciiParser::consumeVersion(const folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Reply>();

  appendCurrentCharTo(buffer, message.value(), p_);
}

template <class Reply>
void McClientAsciiParser::consumeIpAddr(const folly::IOBuf& /* buffer */) {
  auto& message = currentMessage_.get<Reply>();
  char inAddrBuf[sizeof(struct in6_addr)];
  // Max ip address length is INET6_ADDRSTRLEN - 1 chars.
  if (message.ipAddress().size() < INET6_ADDRSTRLEN) {
    char addr[INET6_ADDRSTRLEN] = {0};
    memcpy(addr, message.ipAddress().data(), message.ipAddress().size());
    if (strchr(addr, ':') == nullptr) {
      if (inet_pton(AF_INET, addr, inAddrBuf) > 0) {
        message.ipv() = 4;
      }
    } else {
      if (inet_pton(AF_INET6, addr, inAddrBuf) > 0) {
        message.ipv() = 6;
      }
    }
  }
}

template <class Reply>
void McClientAsciiParser::consumeIpAddrHelper(const folly::IOBuf&) {
  auto& message = currentMessage_.get<Reply>();

  message.ipAddress().push_back(*p_);
}

inline void McClientAsciiParser::initFirstCharIOBuf(
    const folly::IOBuf& from,
    folly::IOBuf& to,
    const char* pos) {
  // Copy current IOBuf.
  from.cloneOneInto(to);
  trimIOBufToRange(to, pos, pos + 1);
}

inline void McClientAsciiParser::appendCurrentCharTo(
    const folly::IOBuf& from,
    folly::IOBuf& to,
    const char* pos) {
  // If it is just a next char in the same memory chunk, just append it.
  // Otherwise we need to append new IOBuf.
  if (to.prev()->data() + to.prev()->length() ==
          reinterpret_cast<const void*>(pos) &&
      to.prev()->tailroom() > 0) {
    to.prev()->append(1);
  } else {
    auto nextPiece = from.cloneOne();
    trimIOBufToRange(*nextPiece, pos, pos + 1);
    to.prependChain(std::move(nextPiece));
  }
}

template <class Reply>
void McClientAsciiParser::resetErrorMessage(Reply& message) {
  message.message().clear();
}
}
} // facebook::memcache
