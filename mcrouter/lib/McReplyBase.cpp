/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McReplyBase.h"

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/IOBufUtil.h"

namespace facebook { namespace memcache {

bool McReplyBase::worseThan(const McReplyBase& other) const {
  return awfulness(result_) > awfulness(other.result_);
}

bool McReplyBase::isError() const {
  return mc_res_is_err(result_);
}

bool McReplyBase::isFailoverError() const {
  switch (result_) {
    case mc_res_busy:
    case mc_res_tko:
    case mc_res_try_again:
    case mc_res_local_error:
    case mc_res_connect_error:
    case mc_res_connect_timeout:
    case mc_res_timeout:
    case mc_res_remote_error:
      return true;
    default:
      return false;
  }
}

bool McReplyBase::isSoftTkoError() const {
  switch (result_) {
    case mc_res_tko:
    case mc_res_timeout:
      return true;
    default:
      return false;
  }
}

bool McReplyBase::isHardTkoError() const {
  switch (result_) {
    case mc_res_busy:
    case mc_res_connect_error:
    case mc_res_connect_timeout:
      return true;
    default:
      return false;
  }
}

void McReplyBase::setValue(folly::IOBuf valueData) {
  valueData_ = std::move(valueData);
}

void McReplyBase::setValue(folly::StringPiece str) {
  valueData_ = folly::IOBuf(folly::IOBuf::COPY_BUFFER, str);
}

void McReplyBase::setResult(mc_res_t res) {
  result_ = res;
}

McReplyBase::McReplyBase() {
}

McReplyBase::McReplyBase(mc_res_t res)
    : result_(res) {
}

McReplyBase::McReplyBase(mc_res_t res, folly::IOBuf val,
                         void (*destructor)(void*), void* ctx)
    : result_(res),
      valueData_(std::move(val)),
      destructor_(CUniquePtr(ctx, destructor)) {
}

McReplyBase::McReplyBase(mc_res_t res, folly::StringPiece val)
    : result_(res),
      valueData_(folly::IOBuf(folly::IOBuf::COPY_BUFFER, val)) {
}

McReplyBase::McReplyBase(mc_res_t res, const char* value)
    : result_(res),
      valueData_(folly::IOBuf(folly::IOBuf::COPY_BUFFER, value,
                              strlen(value))) {
}

McReplyBase::McReplyBase(mc_res_t res, const std::string& value)
    : result_(res),
      valueData_(folly::IOBuf(folly::IOBuf::COPY_BUFFER, value)) {
}

McReplyBase::McReplyBase(mc_res_t res, McMsgRef&& msg)
    : msg_(std::move(msg)),
      result_(res),
      flags_(msg_.get() ? msg_->flags : 0),
      leaseToken_(msg_.get() ? msg_->lease_id : 0),
      delta_(msg_.get() ? msg_->delta : 0),
      cas_(msg_.get() ? msg_->cas : 0),
      errCode_(msg_.get() ? msg_->err_code : 0) {
  if (msg_.get() && msg_->value.str != nullptr) {
    valueData_.emplace(makeMsgValueIOBufStack(msg_));
  }
}

void McReplyBase::dependentMsg(mc_op_t op, mc_msg_t* out) const {
  if (msg_.get() != nullptr) {
    mc_msg_shallow_copy(out, msg_.get());
  }

  auto value = valueRangeSlow();

  out->key.str = nullptr;
  out->key.len = 0;
  out->value.str = const_cast<char*>(value.begin());
  out->value.len = value.size();
  out->op = op;
  out->result = result_;
  out->flags = flags_;
  out->lease_id = leaseToken_;
  out->delta = delta_;
  out->cas = cas_;
  out->err_code = errCode_;
}

McMsgRef McReplyBase::releasedMsg(mc_op_t op) const {
  if (msg_.get() != nullptr &&
      msg_->op == op &&
      msg_->result == result_ &&
      msg_->flags == flags_ &&
      msg_->lease_id == leaseToken_ &&
      msg_->delta == delta_ &&
      msg_->err_code == errCode_ &&
      hasSameMemoryRegion(value(), to<folly::StringPiece>(msg_->value))) {
    return msg_.clone();
  } else {
    auto len = value().computeChainDataLength();
    auto toRelease = createMcMsgRef(len + 1);
    if (msg_.get() != nullptr) {
      mc_msg_shallow_copy(toRelease.get(), msg_.get());
      // TODO: fbtrace?
    }
    toRelease->key.str = nullptr;
    toRelease->key.len = 0;
    toRelease->value.str =
      static_cast<char*>(static_cast<void*>(toRelease.get() + 1));
    copyInto(toRelease->value.str, value());
    toRelease->value.len = len;
    toRelease->op = op;
    toRelease->result = result_;
    toRelease->flags = flags_;
    toRelease->lease_id = leaseToken_;
    toRelease->delta = delta_;
    toRelease->cas = cas_;
    toRelease->err_code = errCode_;

    return std::move(toRelease);
  }
}

}}  // facebook::memcache
