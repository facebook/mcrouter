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

#include <memory>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/McMsgRef.h"

namespace folly {
class IOBuf;
template <class T> class Range;
using StringPiece = Range<const char*>;
}

namespace facebook { namespace memcache {

folly::StringPiece getRange(const std::unique_ptr<folly::IOBuf>& buf);
folly::StringPiece getRange(const folly::IOBuf& buf);

folly::StringPiece coalesceAndGetRange(std::unique_ptr<folly::IOBuf>& buf);
folly::StringPiece coalesceAndGetRange(folly::IOBuf& buf);

/**
 * Create an IOBuf that will incref the msg, wrap the key/value of the msg
 * and decref the msg on destruction.
 *
 * @param returnEmpty  If true, will return an empty IOBuf object if
 *                     the key/value is missing or zero length.
 *                     Otherwise would return nullptr in that case.
 */
std::unique_ptr<folly::IOBuf> makeMsgKeyIOBuf(const McMsgRef& msg,
                                              bool returnEmpty = false);

std::unique_ptr<folly::IOBuf> makeMsgValueIOBuf(const McMsgRef& msg,
                                                bool returnEmpty = false);

folly::IOBuf makeMsgKeyIOBufStack(const McMsgRef& msg);
folly::IOBuf makeMsgValueIOBufStack(const McMsgRef& msg);

bool hasSameMemoryRegion(const folly::IOBuf& buf,
                         folly::StringPiece range);

bool hasSameMemoryRegion(const folly::IOBuf& a, const folly::IOBuf& b);

void copyInto(char* raw, const folly::IOBuf& buf);

template <typename InputIterator>
folly::IOBuf concatAll(InputIterator begin, InputIterator end) {
  folly::IOBuf out;
  if (begin == end) {
    return out;
  }

  (*begin)->cloneInto(out);
  ++begin;
  while (begin != end) {
    out.prependChain(std::move((*begin)->clone()));
    ++begin;
  }

  return out;
}

/**
 * Given a coalesced IOBuf and a range of bytes [begin, begin + size) inside it,
 * clones into out IOBuf so that cloned.data() == begin and
 * cloned.length() == size.
 * @return false If the range is invalid.
 */
inline bool cloneInto(folly::IOBuf& out, const folly::IOBuf& source,
                      const uint8_t* begin, size_t size) {
  if (!(begin >= source.data() &&
        begin + size <= source.data() + source.length())) {
    return false;
  }
  source.cloneOneInto(out);
  out.trimStart(begin - out.data());
  out.trimEnd(out.length() - size);
  return true;
}

}}
