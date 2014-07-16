/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "IOBufUtil.h"

#include "folly/Range.h"
#include "folly/io/IOBuf.h"
#include "mcrouter/lib/McMsgRef.h"

namespace facebook { namespace memcache {

folly::StringPiece getRange(const std::unique_ptr<folly::IOBuf>& buf) {
  if (!buf) {
    return {};
  }
  assert(!buf->isChained());
  return {reinterpret_cast<const char*>(buf->data()), buf->length()};
}

folly::StringPiece coalesceAndGetRange(std::unique_ptr<folly::IOBuf>& buf) {
  if (!buf) {
    return {};
  }

  buf->coalesce();
  return getRange(buf);
}

template <nstring_t mc_msg_t::* F>
std::unique_ptr<folly::IOBuf> makeMsgIOBufHelper(const McMsgRef& msgRef) {
  if (!msgRef.get()) {
    return nullptr;
  }
  auto msg = const_cast<mc_msg_t*>(msgRef.get());
  if (!(msg->*F).len) {
    return nullptr;
  }
  return folly::IOBuf::takeOwnership(
    (msg->*F).str, (msg->*F).len, (msg->*F).len,
    [] (void* buf, void* ctx) {
      auto m = reinterpret_cast<mc_msg_t*>(ctx);
      mc_msg_decref(m);
    },
    mc_msg_incref(msg));
}

std::unique_ptr<folly::IOBuf> makeMsgKeyIOBuf(const McMsgRef& msgRef) {
  return makeMsgIOBufHelper<&mc_msg_t::key>(msgRef);
}

std::unique_ptr<folly::IOBuf> makeMsgValueIOBuf(const McMsgRef& msgRef) {
  return makeMsgIOBufHelper<&mc_msg_t::value>(msgRef);
}

bool hasSameMemoryRegion(const std::unique_ptr<folly::IOBuf>& buf,
                         folly::StringPiece range) {
  if (!buf) {
    return range.empty();
  }
  return !buf->isChained() &&
    range.begin() == reinterpret_cast<const char*>(buf->data()) &&
    range.size() == buf->length();
}

bool hasSameMemoryRegion(const std::unique_ptr<folly::IOBuf>& a,
                         const std::unique_ptr<folly::IOBuf>& b) {
  if (!a && !b) {
    return true;
  }
  if (!a) {
    return !b->isChained() && !b->length();
  }
  if (!b) {
    return !a->isChained() && !a->length();
  }
  return !a->isChained() && !b->isChained() &&
    a->data() == b->data() &&
    a->length() == b->length();
}

void copyInto(char* raw, const folly::IOBuf& buf) {
  auto cur = &buf;
  auto next = cur->next();
  do {
    ::memcpy(raw, cur->data(), cur->length());
    raw += cur->length();
    cur = next;
    next = next->next();
  } while (cur != &buf);
}

}}
