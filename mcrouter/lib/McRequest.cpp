/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McRequest.h"

#include <folly/io/IOBuf.h>
#include <folly/Memory.h>
#include <folly/Range.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

McRequest::McRequest(McMsgRef&& msg)
    : keyData_(makeMsgKeyIOBufStack(msg)),
      valueData_(makeMsgValueIOBufStack(msg)),
      keys_(getRange(keyData_)),
      exptime_(msg->exptime),
      number_(msg->number),
      flags_(msg->flags),
      delta_(msg->delta),
      leaseToken_(msg->lease_id),
      cas_(msg->cas)
#ifndef LIBMC_FBTRACE_DISABLE
    ,
      fbtraceInfo_(McFbtraceRef::cloneRef(msg->fbtrace_info))
#endif
{
}

McRequest::McRequest(folly::StringPiece key)
    : keyData_(folly::IOBuf(folly::IOBuf::COPY_BUFFER, key)) {
  keyData_.coalesce();
  keys_.update(getRange(keyData_));
}

McRequest::McRequest(folly::IOBuf keyData)
    : keyData_(std::move(keyData)) {
  keyData_.coalesce();
  keys_.update(getRange(keyData_));
}

bool McRequest::setKeyFrom(const folly::IOBuf& source,
                           const uint8_t* keyBegin, size_t keySize) {
  if (keySize && cloneInto(keyData_, source, keyBegin, keySize)) {
    keys_ = Keys(getRange(keyData_));
    return true;
  }
  return false;
}

bool McRequest::setValueFrom(const folly::IOBuf& source,
                             const uint8_t* valueBegin, size_t valueSize) {
  return valueSize && cloneInto(valueData_, source, valueBegin, valueSize);
}

McMsgRef McRequest::dependentMsg(mc_op_t op) const {
  auto into = createMcMsgRef();

  into->op = op;
  into->exptime = exptime_;
  into->number = number_;
  into->flags = flags_;
  into->delta = delta_;
  into->lease_id = leaseToken_;
  into->cas = cas_;
#ifndef LIBMC_FBTRACE_DISABLE
  into->fbtrace_info = fbtraceInfo_.clone().release();
#endif
  into->key = to<nstring_t>(getRange(keyData_));
  into->value = to<nstring_t>(valueRangeSlow());

  return std::move(into);
}

folly::StringPiece McRequest::keyWithoutRoute() const {
  return keys_.keyWithoutRoute;
}

int32_t McRequest::exptime() const {
  return exptime_;
}

uint64_t McRequest::flags() const {
  return flags_;
}

McRequest::Keys::Keys(folly::StringPiece key) noexcept {
  update(key);
}

void McRequest::Keys::update(folly::StringPiece key) {
  keyWithoutRoute = key;
  if (!key.empty()) {
    if (*key.begin() == '/') {
      size_t pos = 1;
      for (int i = 0; i < 2; ++i) {
        pos = key.find('/', pos);
        if (pos == std::string::npos) {
          break;
        }
        ++pos;
      }
      if (pos != std::string::npos) {
        keyWithoutRoute.advance(pos);
        routingPrefix.reset(key.begin(), pos);
      }
    }
  }
  routingKey = keyWithoutRoute;
  size_t pos = keyWithoutRoute.find("|#|");
  if (pos != std::string::npos) {
    routingKey.reset(keyWithoutRoute.begin(), pos);
  }
  routingKeyHash_ = 0;
}

uint32_t McRequest::Keys::routingKeyHash() const {
  if (routingKeyHash_ == 0) {
    const_cast<uint32_t&>(routingKeyHash_)
      = getMemcacheKeyHashValue(routingKey);
  }
  return routingKeyHash_;
}

McRequest::McRequest(const McRequest& other)
    : exptime_(other.exptime_),
      number_(other.number_),
      flags_(other.flags_),
      delta_(other.delta_),
      leaseToken_(other.leaseToken_),
      cas_(other.cas_) {
  // Key is always a single piece, so it's safe to do cloneOneInto.
  other.keyData_.cloneOneInto(keyData_);
  assert(hasSameMemoryRegion(keyData_, other.keyData_));
  // it's safe to copy existing StringPieces since we don't copy the data
  keys_ = other.keys_;
  other.valueData_.cloneInto(valueData_);

#ifndef LIBMC_FBTRACE_DISABLE
  if (other.fbtraceInfo_.get()) {
    fbtraceInfo_ = McFbtraceRef::moveRef(
      mc_fbtrace_info_deep_copy(other.fbtraceInfo_.get()));
  }
#endif
}

McRequest::~McRequest() {
}

}}
