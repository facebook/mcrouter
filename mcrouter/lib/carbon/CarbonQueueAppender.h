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

#include <sys/uio.h>

#include <type_traits>
#include <utility>

#include <folly/Optional.h>
#include <folly/Varint.h>
#include <folly/io/IOBuf.h>
#include <folly/lang/Bits.h>

namespace folly {
class IOBuf;
} // folly

namespace carbon {

class CarbonQueueAppenderStorage {
 public:
  CarbonQueueAppenderStorage() {
    iovs_[0] = {storage_, 0};
  }

  CarbonQueueAppenderStorage(const CarbonQueueAppenderStorage&) = delete;
  CarbonQueueAppenderStorage& operator=(const CarbonQueueAppenderStorage&) =
      delete;

  void append(const folly::IOBuf& buf) {
    // IOBuf copy is a very expensive procedure (64 bytes object + atomic
    // operation), avoid incuring that cost for small buffers.
    if (!buf.isChained() && buf.length() <= kInlineIOBufLen &&
        storageIdx_ + buf.length() <= sizeof(storage_)) {
      push(buf.data(), buf.length());
      return;
    }

    finalizeLastIovec();

    if (nIovsUsed_ == kMaxIovecs) {
      coalesce();
    }

    assert(nIovsUsed_ < kMaxIovecs);

    struct iovec* nextIov = iovs_ + nIovsUsed_;
    const auto nFilled = buf.fillIov(nextIov, kMaxIovecs - nIovsUsed_);

    if (nFilled > 0) {
      nIovsUsed_ += nFilled;
      if (!head_) {
        head_ = buf;
      } else {
        head_->prependChain(buf.clone());
      }
    } else {
      if (buf.empty()) {
        return;
      }
      appendSlow(buf);
    }

    // If a push() comes after, it should not use the iovec we just filled in
    canUsePreviousIov_ = false;
  }

  void push(const uint8_t* buf, size_t len) {
    if (nIovsUsed_ == kMaxIovecs) {
      // In this case, it would be possible to use the last iovec if
      // canUsePreviousIov_ is true, but we simplify logic by foregoing this
      // optimization.
      coalesce();
    }

    assert(nIovsUsed_ < kMaxIovecs);

    if (storageIdx_ + len <= sizeof(storage_)) {
      if (!canUsePreviousIov_) {
        // Note, we will be updating iov_len once we're done with this iovec,
        // i.e. in finalizeLastIovec()
        iovs_[nIovsUsed_++].iov_base = &storage_[storageIdx_];

        // If the next push() comes before the next append(), and if we still
        // have room left in storage_,  then we can just extend the last iovec
        // used since we will write to storage_ where we left off.
        canUsePreviousIov_ = true;
      }

      std::memcpy(&storage_[storageIdx_], buf, len);
      storageIdx_ += len;
    } else {
      appendNoInline(folly::IOBuf(folly::IOBuf::COPY_BUFFER, buf, len));
    }
  }

  void coalesce();

  void reset() {
    storageIdx_ = kMaxHeaderLength;
    head_.clear();
    // Reserve first element of iovs_ for header, which won't be filled in
    // until after body data is serialized.
    iovs_[0] = {storage_, 0};
    nIovsUsed_ = 1;
    canUsePreviousIov_ = false;
    headerOverlap_ = 0;
  }

  std::pair<const struct iovec*, size_t> getIovecs() {
    finalizeLastIovec();
    return iovs_[0].iov_len == 0 ? std::make_pair(iovs_ + 1, nIovsUsed_ - 1)
                                 : std::make_pair(iovs_, nIovsUsed_);
  }

  size_t computeBodySize() {
    finalizeLastIovec();
    size_t bodySize = 0;
    // Skip iovs_[0], which refers to message header
    for (size_t i = 1; i < nIovsUsed_; ++i) {
      bodySize += iovs_[i].iov_len;
    }
    return bodySize - headerOverlap_;
  }

  // Hack: we expose headerBuf_ so users can write directly to it.
  // It is the responsibility of the user to report how much data was written
  // via reportHeaderSize().
  uint8_t* getHeaderBuf() {
    assert(iovs_[0].iov_base == storage_);
    return storage_;
  }

  void reportHeaderSize(size_t headerSize) {
    // First iovec optimization.
    if (nIovsUsed_ > 1 && iovs_[1].iov_base == storage_ + kMaxHeaderLength) {
      iovs_[1].iov_base = storage_ + (kMaxHeaderLength - headerSize);
      memmove(iovs_[1].iov_base, storage_, headerSize);
      iovs_[1].iov_len += headerSize;
      iovs_[0].iov_len = 0;
      headerOverlap_ = headerSize;
    } else {
      iovs_[0].iov_len = headerSize;
    }
  }

 private:
  static constexpr size_t kMaxIovecs{32};
  static constexpr size_t kInlineIOBufLen{128};

  // Copied from UmbrellaProtocol.h, which will eventually die
  static constexpr size_t kMaxAdditionalFields = 3;
  static constexpr size_t kMaxHeaderLength = 1 /* magic byte */ +
      1 /* GroupVarint header (lengths of 4 ints) */ +
      4 * sizeof(uint32_t) /* body size, typeId, reqId, num addl fields */ +
      2 * kMaxAdditionalFields *
          folly::kMaxVarintLength64; /* key and value for additional fields */

  size_t storageIdx_{kMaxHeaderLength};
  size_t nIovsUsed_{1};
  size_t headerOverlap_{0};
  bool canUsePreviousIov_{false};

  // Buffer used for non-IOBuf data, e.g., ints, strings, and protocol
  // data
  uint8_t storage_[512 + kMaxHeaderLength];

  // The first iovec in iovs_ points to Caret message header data, and nothing
  // else. The remaining iovecs are used for the message body. Note that we do
  // not share iovs_[0] with body data, even if it would be possible, e.g., we
  // do not append the CT_STRUCT (struct beginning delimiter) to iovs_[0].
  struct iovec iovs_[kMaxIovecs];

  // Chain of IOBufs used for IOBuf fields, like key and value. Note that we
  // also maintain views into this data via iovs_.
  folly::Optional<folly::IOBuf> head_;

  FOLLY_NOINLINE void appendNoInline(const folly::IOBuf& buf) {
    append(buf);
  }

  FOLLY_NOINLINE void appendSlow(const folly::IOBuf& buf) {
    struct iovec* nextIov = iovs_ + nIovsUsed_;
    auto bufCopy = buf;
    bufCopy.coalesce();
    const auto nFilledRetry = bufCopy.fillIov(nextIov, kMaxIovecs - nIovsUsed_);
    assert(nFilledRetry == 1);
    (void)nFilledRetry;
    ++nIovsUsed_;
    if (!head_) {
      head_ = std::move(bufCopy);
    } else {
      head_->prependChain(bufCopy.clone());
    }
  }

  void finalizeLastIovec() {
    if (canUsePreviousIov_) {
      auto& iov = iovs_[nIovsUsed_ - 1];
      iov.iov_len =
          &storage_[storageIdx_] - static_cast<const uint8_t*>(iov.iov_base);
    }
  }
};

/**
 * Mcrouter's own implementation of folly's QueueAppender.  CarbonQueueAppender
 * implements the portion of the folly::io::QueueAppender interface needed by
 * carbon::CarbonProtocolWriter.
 * We have our own version of QueueAppender in order to support more efficient
 * memory management for mcrouter's use case.
 */
class CarbonQueueAppender {
 public:
  CarbonQueueAppender(CarbonQueueAppenderStorage* storage, uint64_t unused) {
    reset(storage, unused);
  }

  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value>::type write(T value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    push(bytes, sizeof(T));
  }

  template <class T>
  void writeBE(T value) {
    write(folly::Endian::big(value));
  }

  template <class T>
  void writeLE(T value) {
    write(folly::Endian::little(value));
  }

  void reset(CarbonQueueAppenderStorage* storage, uint64_t /* unused */) {
    storage_ = storage;
  }

  // The user is responsible for ensuring storage_ is valid before calling
  // push() or insert()
  void push(const uint8_t* buf, size_t len) {
    assert(storage_);
    storage_->push(buf, len);
  }

  void insert(std::unique_ptr<folly::IOBuf> buf) {
    assert(storage_);
    assert(buf);
    storage_->append(*buf);
  }

  void insert(const folly::IOBuf& buf) {
    assert(storage_);
    storage_->append(buf);
  }

 private:
  CarbonQueueAppenderStorage* storage_{nullptr};
};

} // carbon
