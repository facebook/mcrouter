/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "IOBufUtil.h"

#include <folly/io/IOBuf.h>
#include <folly/Range.h>

#include "mcrouter/lib/McMsgRef.h"

namespace facebook { namespace memcache {

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

template <nstring_t mc_msg_t::* F>
std::unique_ptr<folly::IOBuf> makeMsgIOBufHelper(const McMsgRef& msgRef,
                                                 bool returnEmpty) {
  if (!msgRef.get()) {
    return returnEmpty ? folly::IOBuf::create(0) : nullptr;
  }
  auto msg = const_cast<mc_msg_t*>(msgRef.get());
  if (!(msg->*F).len) {
    return returnEmpty ? folly::IOBuf::create(0) : nullptr;
  }
  return folly::IOBuf::takeOwnership(
    (msg->*F).str, (msg->*F).len, (msg->*F).len,
    [] (void* buf, void* ctx) {
      auto m = reinterpret_cast<mc_msg_t*>(ctx);
      mc_msg_decref(m);
    },
    mc_msg_incref(msg));
}

template <nstring_t mc_msg_t::* F>
folly::IOBuf makeMsgIOBufStackHelper(const McMsgRef& msgRef) {
  if (!msgRef.get()) {
    return {};
  }
  auto msg = const_cast<mc_msg_t*>(msgRef.get());
  if (!(msg->*F).len) {
    return {};
  }
  return folly::IOBuf(
    folly::IOBuf::TAKE_OWNERSHIP,
    (msg->*F).str, (msg->*F).len, (msg->*F).len,
    [] (void* buf, void* ctx) {
      auto m = reinterpret_cast<mc_msg_t*>(ctx);
      mc_msg_decref(m);
    },
    mc_msg_incref(msg));
}

std::unique_ptr<folly::IOBuf> makeMsgKeyIOBuf(const McMsgRef& msgRef,
                                              bool returnEmpty) {
  return makeMsgIOBufHelper<&mc_msg_t::key>(msgRef, returnEmpty);
}

std::unique_ptr<folly::IOBuf> makeMsgValueIOBuf(const McMsgRef& msgRef,
                                                bool returnEmpty) {
  return makeMsgIOBufHelper<&mc_msg_t::value>(msgRef, returnEmpty);
}

folly::IOBuf makeMsgKeyIOBufStack(const McMsgRef& msgRef) {
  return makeMsgIOBufStackHelper<&mc_msg_t::key>(msgRef);
}

folly::IOBuf makeMsgValueIOBufStack(const McMsgRef& msgRef) {
  return makeMsgIOBufStackHelper<&mc_msg_t::value>(msgRef);
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
    ::memcpy(raw, cur->data(), cur->length());
    raw += cur->length();
    cur = next;
    next = next->next();
  } while (cur != &buf);
}

}}
