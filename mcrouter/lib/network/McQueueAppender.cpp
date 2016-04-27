/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/network/McQueueAppender.h"

#include <cstring>

#include <folly/io/IOBuf.h>

namespace facebook { namespace memcache {

void McQueueAppenderStorage::append(const folly::IOBuf& buf) {
  if (nIovsUsed_ == kMaxIovecs) {
    coalesce();
  }

  assert(nIovsUsed_ < kMaxIovecs);

  struct iovec* nextIov = iovs_ + nIovsUsed_;
  const auto nFilled = buf.fillIov(nextIov, kMaxIovecs - nIovsUsed_);

  if (nFilled > 0 || buf.length() == 0) {
    nIovsUsed_ += nFilled;
  } else {
    auto bufCopy = buf;
    bufCopy.coalesce();
    const auto nFilledRetry = bufCopy.fillIov(nextIov, kMaxIovecs - nIovsUsed_);
    assert(nFilledRetry == 1);
    ++nIovsUsed_;
  }

  if (head_.empty()) {
    head_ = buf;
  } else {
    head_.prependChain(buf.clone());
  }

  // If a push() comes after, it should not use the iovec we just filled in
  canUsePreviousIov_ = false;
}

void McQueueAppenderStorage::push(const uint8_t* buf, size_t len) {
  struct iovec* iov;

  if (nIovsUsed_ == kMaxIovecs) {
    // In this case, it would be possible to use the last iovec if
    // canUsePreviousIov_ is true, but we simplify logic by foregoing this
    // optimization.
    coalesce();
  }

  assert(nIovsUsed_ < kMaxIovecs);

  if (storageIdx_ + len <= sizeof(storage_)) {
    if (canUsePreviousIov_) {
      assert(nIovsUsed_ > 1); // iovs_[0] cannot be used again
      iov = &iovs_[nIovsUsed_ - 1];
    } else {
      iov = &iovs_[nIovsUsed_++];
      iov->iov_base = &storage_[storageIdx_];
      iov->iov_len = 0;
    }

    std::memcpy(&storage_[storageIdx_], buf, len);
    iov->iov_len += len;
    storageIdx_ += len;

    // If the next push() comes before the next append(), and if we still
    // have room left in storage_,  then we can just extend the last iovec
    // used since we will write to storage_ where we left off.
    canUsePreviousIov_ = true;
  } else {
    append(folly::IOBuf(folly::IOBuf::COPY_BUFFER, buf, len));
  }
}

void McQueueAppenderStorage::coalesce() {
  VLOG(4) << "Out of iovecs, coalescing in Caret message serialization";
  assert(nIovsUsed_ == kMaxIovecs);

  canUsePreviousIov_ = false;
  nIovsUsed_ = 1;  // headerBuf_ always considered used
  storageIdx_ = 0;

  size_t newCapacity = 0;
  // coalesce() should always be triggered before writing header
  for (size_t i = 1; i < kMaxIovecs; ++i) {
    newCapacity += iovs_[i].iov_len;
  }

  auto newBuf = folly::IOBuf(folly::IOBuf::CREATE, newCapacity);

  for (size_t i = 1; i < kMaxIovecs; ++i) {
    std::memcpy(newBuf.writableTail(), iovs_[i].iov_base, iovs_[i].iov_len);
    newBuf.append(iovs_[i].iov_len);
  }

  // Release old IOBufs and reset head to new large buffer
  head_ = std::move(newBuf);
  ++nIovsUsed_;
  iovs_[1] = {const_cast<uint8_t*>(head_.data()), head_.length()};
}

}} // facebook::memcache
