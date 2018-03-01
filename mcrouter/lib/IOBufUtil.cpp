/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "IOBufUtil.h"

#include <folly/Range.h>
#include <folly/io/IOBuf.h>

namespace facebook {
namespace memcache {

folly::StringPiece getRange(const std::unique_ptr<folly::IOBuf>& buf) {
  if (!buf) {
    return {};
  }
  return getRange(*buf);
}

folly::StringPiece getRange(const folly::IOBuf& buf) {
  assert(!buf.isChained());
  return {reinterpret_cast<const char*>(buf.data()), buf.length()};
}

folly::StringPiece coalesceAndGetRange(std::unique_ptr<folly::IOBuf>& buf) {
  if (!buf) {
    return {};
  }

  return coalesceAndGetRange(*buf);
}

folly::StringPiece coalesceAndGetRange(folly::IOBuf& buf) {
  buf.coalesce();
  return getRange(buf);
}

folly::StringPiece coalesceAndGetRange(folly::Optional<folly::IOBuf>& buf) {
  return buf.hasValue() ? coalesceAndGetRange(*buf) : folly::StringPiece();
}

bool hasSameMemoryRegion(const folly::IOBuf& buf, folly::StringPiece range) {
  return !buf.isChained() &&
      (buf.length() == 0 ||
       (range.begin() == reinterpret_cast<const char*>(buf.data()) &&
        range.size() == buf.length()));
}

bool hasSameMemoryRegion(const folly::IOBuf& a, const folly::IOBuf& b) {
  return !a.isChained() && !b.isChained() &&
      ((a.length() == 0 && b.length() == 0) ||
       (a.data() == b.data() && a.length() == b.length()));
}

void copyInto(char* raw, const folly::IOBuf& buf) {
  auto cur = &buf;
  auto next = cur->next();
  do {
    if (cur->data()) {
      ::memcpy(raw, cur->data(), cur->length());
      raw += cur->length();
    }
    cur = next;
    next = next->next();
  } while (cur != &buf);
}

namespace {
/**
 * Creating IOBuf from an iovec array.
 */
folly::IOBuf
coalesceSlow(const struct iovec* iov, size_t iovcnt, size_t destCapacity) {
  folly::IOBuf buffer(folly::IOBuf::CREATE, destCapacity);
  for (size_t i = 0; i < iovcnt; ++i) {
    std::memcpy(buffer.writableTail(), iov[i].iov_base, iov[i].iov_len);
    buffer.append(iov[i].iov_len);
  }
  assert(buffer.length() <= destCapacity);
  return buffer;
}
} // anonymous namespace

folly::IOBuf
coalesceIovecs(const struct iovec* iov, size_t iovcnt, size_t destCapacity) {
  if (iovcnt == 1) {
    return folly::IOBuf(
        folly::IOBuf::WRAP_BUFFER, iov[0].iov_base, iov[0].iov_len);
  }
  return coalesceSlow(iov, iovcnt, destCapacity);
}
}
} // facebook::memcache
