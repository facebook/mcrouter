#pragma once

#include <memory>

#include "mcrouter/lib/McMsgRef.h"

namespace folly {
class IOBuf;
template <class T> class Range;
using StringPiece = Range<const char*>;
}

namespace facebook { namespace memcache {

folly::StringPiece getRange(const std::unique_ptr<folly::IOBuf>& buf);

folly::StringPiece coalesceAndGetRange(std::unique_ptr<folly::IOBuf>& buf);

std::unique_ptr<folly::IOBuf> makeMsgKeyIOBuf(const McMsgRef& msg);

std::unique_ptr<folly::IOBuf> makeMsgValueIOBuf(const McMsgRef& msg);

bool hasSameMemoryRegion(const std::unique_ptr<folly::IOBuf>& buf,
                         folly::StringPiece range);

bool hasSameMemoryRegion(const std::unique_ptr<folly::IOBuf>& a,
                         const std::unique_ptr<folly::IOBuf>& b);

void copyInto(char* raw, const folly::IOBuf& buf);

template <typename InputIterator>
std::unique_ptr<folly::IOBuf> concatAll(InputIterator begin,
                                        InputIterator end) {
  if (begin == end) {
    return nullptr;
  }

  auto buf_head = (*begin)->clone();
  ++begin;
  while (begin != end) {
    buf_head->prependChain(std::move((*begin)->clone()));
    ++begin;
  }

  return buf_head;
}

}}
