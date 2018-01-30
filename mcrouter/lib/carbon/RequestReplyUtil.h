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

#include <type_traits>

#include <folly/Optional.h>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/protocol.h"

namespace facebook {
namespace memcache {
class McServerRequestContext;
} // memcache
} // facebook

namespace carbon {

namespace detail {

template <typename T, typename = std::string&>
struct HasMessage : std::false_type {};

template <typename T>
struct HasMessage<T, decltype(std::declval<T>().message())> : std::true_type {};

template <class Request, class = bool&>
struct HasFailover : public std::false_type {};

template <class Request>
struct HasFailover<Request, decltype(std::declval<Request>().failover())>
    : public std::true_type {};

template <class Request>
typename std::enable_if<HasFailover<Request>::value, void>::type
setRequestFailover(Request& req) {
  req.failover() = true;
}

template <class Request>
typename std::enable_if<!HasFailover<Request>::value, void>::type
setRequestFailover(Request& req) {
  if (!req.key().hasHashStop()) {
    return;
  }
  constexpr folly::StringPiece kFailoverTag = ":failover=1";
  auto keyWithFailover =
      folly::to<std::string>(req.key().fullKey(), kFailoverTag);
  /* It's always safe to not append a failover tag */
  if (keyWithFailover.size() <= MC_KEY_MAX_LEN) {
    req.key() = std::move(keyWithFailover);
  }
}

template <class RequestList>
struct GetRequestReplyPairsImpl;

template <class Request, class... Requests>
struct GetRequestReplyPairsImpl<List<Request, Requests...>> {
  using type = facebook::memcache::PrependT<
      facebook::memcache::Pair<Request, typename Request::reply_type>,
      typename GetRequestReplyPairsImpl<List<Requests...>>::type>;
};

template <>
struct GetRequestReplyPairsImpl<List<>> {
  using type = List<>;
};

} // detail

template <class RequestList>
using GetRequestReplyPairs =
    typename detail::GetRequestReplyPairsImpl<RequestList>::type;

template <typename Reply>
typename std::enable_if_t<detail::HasMessage<Reply>::value> setMessageIfPresent(
    Reply& reply,
    std::string msg) {
  reply.message() = std::move(msg);
}

template <typename Reply>
typename std::enable_if_t<!detail::HasMessage<Reply>::value>
setMessageIfPresent(Reply&, std::string) {}

template <typename Reply>
typename std::enable_if_t<detail::HasMessage<Reply>::value, folly::StringPiece>
getMessage(const Reply& reply) {
  return reply.message();
}

template <typename Reply>
typename std::enable_if_t<!detail::HasMessage<Reply>::value, folly::StringPiece>
getMessage(const Reply&) {
  return folly::StringPiece{};
}

namespace detail {
inline folly::IOBuf* bufPtr(folly::Optional<folly::IOBuf>& buf) {
  return buf.get_pointer();
}
inline folly::IOBuf* bufPtr(folly::IOBuf& buf) {
  return &buf;
}
} // detail

template <class R>
typename std::enable_if<R::hasValue, const folly::IOBuf*>::type valuePtrUnsafe(
    const R& requestOrReply) {
  return detail::bufPtr(const_cast<R&>(requestOrReply).value());
}
template <class R>
typename std::enable_if<R::hasValue, folly::IOBuf*>::type valuePtrUnsafe(
    R& requestOrReply) {
  return detail::bufPtr(requestOrReply.value());
}
template <class R>
typename std::enable_if<!R::hasValue, folly::IOBuf*>::type valuePtrUnsafe(
    const R& /* requestOrReply */) {
  return nullptr;
}

template <class R>
typename std::enable_if<R::hasValue, folly::StringPiece>::type valueRangeSlow(
    R& requestOrReply) {
  auto* buf = detail::bufPtr(requestOrReply.value());
  return buf ? folly::StringPiece(buf->coalesce()) : folly::StringPiece();
}

template <class R>
typename std::enable_if<!R::hasValue, folly::StringPiece>::type valueRangeSlow(
    R& /* requestOrReply */) {
  return folly::StringPiece();
}

// Helper class to determine whether a type is a Carbon request.
template <class Msg>
class IsRequestTrait {
  template <class T>
  static std::true_type check(typename T::reply_type*);
  template <class T>
  static std::false_type check(...);

 public:
  static constexpr bool value = decltype(check<Msg>(0))::value;
};

template <class R>
typename std::enable_if<R::hasFlags, uint64_t>::type getFlags(
    const R& requestOrReply) {
  return requestOrReply.flags();
}

template <class R>
typename std::enable_if<!R::hasFlags, uint64_t>::type getFlags(const R&) {
  return 0;
}

/**
 * Helper function to determine typeId by its name.
 *
 * @param name  Name of the struct as specified by Type::name
 *
 * @return  Type::typeId for matched Type if any. 0 if no match found.
 */
template <class TypeList>
inline size_t getTypeIdByName(folly::StringPiece name, TypeList);

template <>
inline size_t getTypeIdByName(folly::StringPiece /* name */, List<>) {
  return 0;
}

template <class T, class... Ts>
inline size_t getTypeIdByName(folly::StringPiece name, List<T, Ts...>) {
  return name == T::name ? T::typeId : getTypeIdByName(name, List<Ts...>());
}

template <class TypeList>
inline ssize_t getIndexInListByName(folly::StringPiece name, TypeList);

template <>
inline ssize_t getIndexInListByName(folly::StringPiece /* name */, List<>) {
  return -1;
}

template <class T, class... Ts>
inline ssize_t getIndexInListByName(folly::StringPiece name, List<T, Ts...>) {
  return name == T::name
      ? 0
      : (getIndexInListByName(name, List<Ts...>()) == -1
             ? -1
             : 1 + getIndexInListByName(name, List<Ts...>()));
}

namespace detail {
template <class List>
struct RequestListLimitsImpl;

template <>
struct RequestListLimitsImpl<List<>> {
  static constexpr size_t minTypeId = std::numeric_limits<size_t>::max();
  static constexpr size_t maxTypeId = std::numeric_limits<size_t>::min();
  static constexpr size_t typeIdRangeSize = 0;
};

template <class T, class... Ts>
struct RequestListLimitsImpl<List<T, Ts...>> {
  static constexpr size_t minTypeId =
      T::typeId <= RequestListLimitsImpl<List<Ts...>>::minTypeId
      ? T::typeId
      : RequestListLimitsImpl<List<Ts...>>::minTypeId;
  static constexpr size_t maxTypeId =
      T::typeId >= RequestListLimitsImpl<List<Ts...>>::maxTypeId
      ? T::typeId
      : RequestListLimitsImpl<List<Ts...>>::maxTypeId;
  static constexpr size_t typeIdRangeSize = maxTypeId - minTypeId + 1;
};
} // detail

/**
 * Limits (min, max and rangeSize) of a list of requests.
 */
template <class RequestList>
using RequestListLimits = detail::RequestListLimitsImpl<RequestList>;

/**
 * Map of type T, where the key is Request::typeId.
 *
 * @tparam RequestList  List of request.
 * @tparam T            Type of the elements of the map.
 */
template <class RequestList, class T>
class RequestIdMap {
 public:
  static constexpr size_t kMinId = RequestListLimits<RequestList>::minTypeId;
  static constexpr size_t kMaxId = RequestListLimits<RequestList>::maxTypeId;
  static constexpr size_t kSize =
      RequestListLimits<RequestList>::typeIdRangeSize;

  const T& getById(size_t id) const {
    facebook::memcache::checkLogic(
        kMinId <= id && id <= kMaxId,
        "Id {} is out of range [{}, {}]",
        id,
        kMinId,
        kMaxId);
    return container_[id - kMinId];
  }

  template <class Request>
  const T& getByRequestType() const {
    static_assert(
        ListContains<RequestList, Request>::value,
        "Supplied Request type is not in RequestList");
    return container_[Request::typeId - kMinId];
  }

  void set(size_t id, T&& val) {
    facebook::memcache::checkLogic(
        kMinId <= id && id <= kMaxId,
        "Id {} is out of range [{}, {}]",
        id,
        kMinId,
        kMaxId);
    container_[id - kMinId] = std::move(val);
  }

 private:
  std::array<T, kSize> container_;
};

template <class RequestList, class T>
constexpr size_t RequestIdMap<RequestList, T>::kMinId;

template <class RequestList, class T>
constexpr size_t RequestIdMap<RequestList, T>::kMaxId;

namespace detail {
// Utility class useful for checking whether a particular OnRequest handler
// class defines an onRequest() handler for Request.
class CanHandleRequest {
  template <class R, class O>
  static constexpr auto check(int) -> decltype(
      std::declval<O>().onRequest(
          std::declval<facebook::memcache::McServerRequestContext>(),
          std::declval<R>()),
      std::true_type()) {
    return {};
  }

  template <class R, class O>
  static constexpr std::false_type check(...) {
    return {};
  }

 public:
  template <class Request, class OnRequest>
  static constexpr auto value() -> decltype(check<Request, OnRequest>(0)) {
    return {};
  }
};
} // detail

} // carbon
