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

// Flags helpers
template <class Message>
typename std::enable_if<Message::hasFlags>::type setFlags(
    Message& message,
    uint64_t flags) {
  message.flags() = flags;
}
template <class Message>
typename std::enable_if<!Message::hasFlags>::type setFlags(Message&, uint64_t) {
}

// Exptime helpers
template <class Message>
typename std::enable_if<Message::hasExptime>::type setExptime(
    Message& message,
    int32_t exptime) {
  message.exptime() = exptime;
}
template <class Message>
typename std::enable_if<!Message::hasExptime>::type setExptime(
    Message&,
    int32_t) {}

// Value helpers
template <class Message>
typename std::enable_if<Message::hasValue>::type setValue(
    Message& message,
    folly::IOBuf&& buf) {
  message.value() = std::move(buf);
}
template <class Message>
typename std::enable_if<!Message::hasValue>::type setValue(
    Message&,
    folly::IOBuf&&) {}
}
} // facebook::memcache
