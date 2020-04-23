/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Optional.h>
#include <folly/io/IOBuf.h>
#include <thrift/lib/cpp2/FieldRef.h>

namespace facebook {
namespace memcache {

namespace detail {
// flags
template <typename Message>
void setFlagsImpl(std::true_type, Message& message, uint64_t flags) {
  message.flags() = flags;
}
template <typename Message>
void setFlagsImpl(std::false_type, Message&, uint64_t) {}
template <typename Message>
uint64_t getFlagsImpl(std::true_type, Message& message) {
  return message.flags();
}
template <typename Message>
uint64_t getFlagsImpl(std::false_type, Message&) {
  return 0;
}
// exptime
template <class Message>
void setExptimeImpl(std::true_type, Message& message, int32_t exptime) {
  message.exptime() = exptime;
}
template <class Message>
void setExptimeImpl(std::false_type, Message&, int32_t) {}
template <class Message>
int32_t getExptimeImpl(std::true_type, Message& message) {
  return message.exptime();
}
template <class Message>
int32_t getExptimeImpl(std::false_type, Message&) {
  return 0;
}
// value
template <class Message>
void setValueImpl(std::true_type, Message& message, folly::IOBuf&& buf) {
  message.value() = std::move(buf);
}
template <class Message>
void setValueImpl(std::false_type, Message&, folly::IOBuf&&) {}
} // namespace detail

// Flags helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasFlagsTrait : public std::false_type {};
template <typename Message>
class HasFlagsTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().flags()),
        std::uint64_t&>{}>> : public std::true_type {};
template <class Message>
void setFlags(Message& message, uint64_t flags) {
  detail::setFlagsImpl(HasFlagsTrait<Message>{}, message, flags);
}
// Return flags if it exists in Message and 0 otherwise.
template <class Message>
uint64_t getFlagsIfExist(Message& message) {
  return detail::getFlagsImpl(HasFlagsTrait<Message>{}, message);
}

// Exptime helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasExptimeTrait : public std::false_type {};
template <typename Message>
class HasExptimeTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().exptime()),
        std::int32_t&>{}>> : public std::true_type {};
template <class Message>
void setExptime(Message& message, int32_t exptime) {
  detail::setExptimeImpl(HasExptimeTrait<Message>{}, message, exptime);
}
// Return exptime if it exists in Message and 0 otherwise.
template <class Message>
int32_t getExptimeIfExist(Message& message) {
  return detail::getExptimeImpl(HasExptimeTrait<Message>{}, message);
}

// Value helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasValueTrait : public std::false_type {};
template <typename Message>
class HasValueTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().value()),
        folly::IOBuf&>{}>> : public std::true_type {};
template <typename Message>
class HasValueTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().value()),
        folly::Optional<folly::IOBuf>&>{}>> : public std::true_type {};
template <typename Message>
class HasValueTrait<
    Message,
    std::enable_if_t<std::is_same<
        decltype(std::declval<std::remove_const_t<Message>&>().value()),
        apache::thrift::optional_field_ref<folly::IOBuf&>>{}>>
    : public std::true_type {};
template <class Message>
void setValue(Message& message, folly::IOBuf&& buf) {
  detail::setValueImpl(
      HasValueTrait<Message>{}, message, std::forward<folly::IOBuf>(buf));
}

// Key helpers
template <typename Message, typename = std::enable_if_t<true>>
class HasKeyTrait : public std::false_type {};
template <typename Message>
class HasKeyTrait<
    Message,
    std::void_t<decltype(std::declval<std::decay_t<Message>&>().key())>>
    : public std::true_type {};
} // namespace memcache
} // namespace facebook
