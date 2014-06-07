/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/McMsgRef.h"

namespace folly {
class IOBuf;
}

namespace facebook { namespace memcache {

/**
 * Represents a string that's used for various parts of the request.
 * Holds a reference to backing storage and provides accessors
 * to the data.
 *
 * An instance of McStringData is immutable, but it's possible
 * to create another McStringData with e.g. a different dataRange()
 * of the same underlying storage.
 */
class McStringData {
 public:
  enum SaveMsgKey_t { SaveMsgKey };
  enum SaveMsgValue_t { SaveMsgValue };

  McStringData();

  /**
   * Use the full msg's key as data.
   * McStringData will hold the moved in reference.
   */
  McStringData(SaveMsgKey_t, McMsgRef&& msg);

  /**
   * Use the full msg's value as data.
   * McStringData will hold the moved in reference.
   */
  McStringData(SaveMsgValue_t, McMsgRef&& msg);

  /**
   * Use the string as data.
   * TODO(avani): remove this constructor by replacing its occurrece
   * with McStringData(const std::unique_ptr<folly::IOBuf>& fromBuf)
   */
  explicit McStringData(const std::string& fromString);

  /**
   * Use the IOBuf as data.
   */
  explicit McStringData(std::unique_ptr<folly::IOBuf> fromBuf);

  /**
   * Note: if we're copying from an McStringData that was
   * created with an ExternalMsg, we will save the reference in
   * the new object (SaveMsg behaviour).
   */
  McStringData(const McStringData&);
  McStringData& operator=(const McStringData&);

  McStringData(McStringData&&) noexcept;
  McStringData& operator=(McStringData&&) noexcept;

  ~McStringData() noexcept;

  /**
   * dataRange() is the actual data represented
   * which is always a subset of the underlying backing storage.
   */
  folly::StringPiece dataRange() const noexcept;

  /**
   * length of actual value.
   * Prefer this function to get length over dataRange().size().
   */
  size_t length() const;

  /**
   * return actual data as IOBuf
   */
  std::unique_ptr<folly::IOBuf> asIOBuf() const;

  /**
   * copy of actual data in dst_buffer
   * return true if copy successful, false otherwise
   */
  void dataCopy(char* dst_buffer) const;

  /**
   * Is the memory region same for actual data and passed argument?
   */
  bool hasSameMemoryRegion(folly::StringPiece sp) const;
  bool hasSameMemoryRegion(const McStringData& other) const;

  /**
   * Returns a new McStringData object with its internal buffer as a part of
   * buffer of this object with arg s_pos as starting position and arg length as
   * size.
   */
  McStringData substr(size_t s_pos = 0,
      size_t length = std::string::npos) const;

 private:
  static void msgFreeFunction(void* buf, void* userData);

  union {
    McMsgRef savedMsg_;
    std::unique_ptr<folly::IOBuf> savedBuf_;
  };

  enum StorageType {
    EMPTY,
    SAVED_MSG_KEY,
    SAVED_MSG_VALUE,
    SAVED_BUF,
  };

  StorageType storageType_;

  struct RelativeRange {
    RelativeRange(size_t begin_, size_t length);
    RelativeRange substr(size_t begin, size_t length = std::string::npos) const;
    size_t length() const;
    size_t begin() const;

   private:
    size_t begin_;
    size_t length_;
  };

  RelativeRange relativeRange_;

  /**
   * determine if backing store is a chained buffer
   */
  bool isChained() const;
};

/**
 * Returns a new McStringData object with its value as concatenation of value
 * of vector elements according to their sequence.
 */
template <typename InputIterator>
McStringData concatAll(InputIterator begin, InputIterator end) {
  if (begin == end) {
    return McStringData();
  }

  auto buf_head = begin->asIOBuf();
  ++begin;
  while(begin != end) {
    buf_head->prependChain(std::move(begin->asIOBuf()));
    ++begin;
  }

  return McStringData(std::move(buf_head));
}

}}  // facebook::memcache
