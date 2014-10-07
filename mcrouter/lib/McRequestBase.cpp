/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McRequestBase.h"

#include <folly/io/IOBuf.h>
#include <folly/Memory.h>
#include <folly/Range.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

McRequestBase::McRequestBase(McMsgRef&& msg)
    : msg_(std::move(msg)),
      keyData_(makeMsgKeyIOBufStack(msg_)),
      valueData_(makeMsgValueIOBufStack(msg_)),
      keys_(getRange(keyData_)),
      exptime_(msg_->exptime),
      flags_(msg_->flags),
      delta_(msg_->delta),
      leaseToken_(msg_->lease_id),
      cas_(msg_->cas)
#ifndef LIBMC_FBTRACE_DISABLE
    ,
      fbtraceInfo_(McFbtraceRef::cloneRef(msg_->fbtrace_info))
#endif
{
}

McRequestBase::McRequestBase(folly::StringPiece key)
    : keyData_(folly::IOBuf(folly::IOBuf::COPY_BUFFER, key)) {
  keyData_.coalesce();
  keys_.update(getRange(keyData_));
  auto msg = createMcMsgRef();
  msg->key = to<nstring_t>(getRange(keyData_));
  msg_ = std::move(msg);
}

bool McRequestBase::setKeyFrom(const folly::IOBuf& source,
                               const uint8_t* keyBegin, size_t keySize) {
  if (keySize && cloneInto(keyData_, source, keyBegin, keySize)) {
    keys_ = Keys(getRange(keyData_));
    return true;
  }
  return false;
}

bool McRequestBase::setValueFrom(const folly::IOBuf& source,
                                 const uint8_t* valueBegin, size_t valueSize) {
  return valueSize && cloneInto(valueData_, source, valueBegin, valueSize);
}

McMsgRef McRequestBase::dependentMsg(mc_op_t op) const {
  auto is_key_set =
    hasSameMemoryRegion(keyData_, to<folly::StringPiece>(msg_->key));
  auto is_value_set =
    hasSameMemoryRegion(valueData_, to<folly::StringPiece>(msg_->value));

  if (msg_->op == op &&
      msg_->exptime == exptime_ &&
      msg_->flags == flags_ &&
      msg_->delta == delta_ &&
      msg_->lease_id == leaseToken_ &&
      msg_->cas == cas_ &&
#ifndef LIBMC_FBTRACE_DISABLE
      msg_->fbtrace_info == fbtraceInfo_.get() &&
#endif
      is_key_set &&
      is_value_set) {
    /* msg_ is an object with the same fields we expect.
       In addition, we want to keep routing prefix.
       We can simply return the reference. */
    return msg_.clone();
  } else {
    /* Out of luck.  The best we can do is make the copy
       reference key/value fields from the backing store. */
    auto toRelease = dependentMcMsgRef(msg_);
    toRelease->op = op;
    toRelease->exptime = exptime_;
    toRelease->flags = flags_;
    toRelease->delta = delta_;
    toRelease->lease_id = leaseToken_;
    toRelease->cas = cas_;
#ifndef LIBMC_FBTRACE_DISABLE
    toRelease->fbtrace_info = fbtraceInfo_.clone().release();
#endif
    if (!is_key_set) {
      toRelease->key = to<nstring_t>(getRange(keyData_));
    }
    if (!is_value_set) {
      toRelease->value = to<nstring_t>(
        coalesceAndGetRange(const_cast<folly::IOBuf&>(valueData_)));
    }
    return std::move(toRelease);
  }
}

McMsgRef McRequestBase::dependentMsgStripRoutingPrefix(mc_op_t op) const {
  auto is_key_set =
    keys_.routingPrefix.empty() &&
    hasSameMemoryRegion(keyData_, to<folly::StringPiece>(msg_->key));
  auto is_value_set =
    hasSameMemoryRegion(valueData_, to<folly::StringPiece>(msg_->value));

  if (msg_->op == op &&
      msg_->exptime == exptime_ &&
      msg_->flags == flags_ &&
      msg_->delta == delta_ &&
      msg_->lease_id == leaseToken_ &&
      msg_->cas == cas_ &&
#ifndef LIBMC_FBTRACE_DISABLE
      msg_->fbtrace_info == fbtraceInfo_.get() &&
#endif
      is_key_set &&
      is_value_set) {
    /* msg_ is an object with the same fields we expect.
       In addition, routing prefix is empty anyway.
       We can simply return the reference. */
    return msg_.clone();
  } else {
    /* Out of luck.  The best we can do is make the copy
       reference key/value fields from the backing store. */
    auto toRelease = dependentMcMsgRef(msg_);
    if (!is_key_set) {
      toRelease->key = to<nstring_t>(keys_.keyWithoutRoute);
    }
    if (!is_value_set) {
      toRelease->value = to<nstring_t>(
        coalesceAndGetRange(const_cast<folly::IOBuf&>(valueData_)));
    }
    toRelease->op = op;
    toRelease->exptime = exptime_;
    toRelease->flags = flags_;
    toRelease->delta = delta_;
    toRelease->lease_id = leaseToken_;
    toRelease->cas = cas_;
#ifndef LIBMC_FBTRACE_DISABLE
    toRelease->fbtrace_info = fbtraceInfo_.clone().release();
#endif
    return std::move(toRelease);
  }
}

folly::StringPiece McRequestBase::keyWithoutRoute() const {
  return keys_.keyWithoutRoute;
}

uint32_t McRequestBase::exptime() const {
  return exptime_;
}

uint64_t McRequestBase::flags() const {
  return flags_;
}

McRequestBase::Keys::Keys(folly::StringPiece key) noexcept {
  update(key);
}

void McRequestBase::Keys::update(folly::StringPiece key) {
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

  routingKeyHash = getMemcacheKeyHashValue(routingKey);
}

McRequestBase::McRequestBase(const McRequestBase& other)
    : exptime_(other.exptime_),
      flags_(other.flags_),
      delta_(other.delta_),
      leaseToken_(other.leaseToken_),
      cas_(other.cas_) {
  other.keyData_.cloneInto(keyData_);
  keys_ = Keys(getRange(keyData_));
  other.valueData_.cloneInto(valueData_);

  if (hasSameMemoryRegion(keyData_, other.keyData_) &&
      hasSameMemoryRegion(valueData_, other.valueData_)) {

    msg_ = other.msg_.clone();
  } else {
    msg_ = createMcMsgRef(
      other.msg_, getRange(keyData_),
      coalesceAndGetRange(valueData_));
  }

#ifndef LIBMC_FBTRACE_DISABLE
  if (other.fbtraceInfo_.get()) {
    fbtraceInfo_ = McFbtraceRef::moveRef(
      mc_fbtrace_info_deep_copy(other.fbtraceInfo_.get()));
  }
#endif
}

McRequestBase::~McRequestBase() {
}

}}
