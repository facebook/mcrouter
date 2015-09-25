/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McReply.h"

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/IOBufUtil.h"

namespace facebook { namespace memcache {

bool McReply::worseThan(const McReply& other) const {
  return awfulness(result_) > awfulness(other.result_);
}

bool McReply::isSoftTkoError() const {
  switch (result_) {
    case mc_res_timeout:
      return true;
    default:
      return false;
  }
}

bool McReply::isHardTkoError() const {
  switch (result_) {
    case mc_res_connect_error:
    case mc_res_connect_timeout:
    case mc_res_shutdown:
      return true;
    default:
      return false;
  }
}

void McReply::setValue(folly::IOBuf valueData) {
  valueData_ = std::move(valueData);
}

void McReply::setValue(folly::StringPiece str) {
  valueData_ = folly::IOBuf(folly::IOBuf::COPY_BUFFER, str);
}

void McReply::setResult(mc_res_t res) {
  result_ = res;
}

McReply::McReply() {
}

McReply::McReply(mc_res_t res)
    : result_(res) {
}

McReply::McReply(mc_res_t res, folly::IOBuf val)
    : result_(res),
      valueData_(std::move(val)) {
}

McReply::McReply(mc_res_t res, folly::StringPiece val)
    : result_(res),
      valueData_(folly::IOBuf(folly::IOBuf::COPY_BUFFER, val)) {
}

McReply::McReply(mc_res_t res, const char* value)
    : result_(res),
      valueData_(folly::IOBuf(folly::IOBuf::COPY_BUFFER, value,
                              strlen(value))) {
}

McReply::McReply(mc_res_t res, const std::string& value)
    : result_(res),
      valueData_(folly::IOBuf(folly::IOBuf::COPY_BUFFER, value)) {
}

McReply::McReply(mc_res_t res, McMsgRef&& msg)
    : msg_(std::move(msg)),
      result_(res),
      flags_(msg_.get() ? msg_->flags : 0),
      leaseToken_(msg_.get() ? msg_->lease_id : 0),
      delta_(msg_.get() ? msg_->delta : 0),
      cas_(msg_.get() ? msg_->cas : 0),
      errCode_(msg_.get() ? msg_->err_code : 0),
      number_(msg_.get() ? msg_->number : 0),
      exptime_(msg_.get() ? msg_->exptime : 0) {
  if (msg_.get() && msg_->value.str != nullptr) {
    valueData_.emplace(makeMsgValueIOBufStack(msg_));
  }
}

void McReply::dependentMsg(mc_op_t op, mc_msg_t* out) const {
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
  out->number = number_;
  out->exptime = exptime_;
}

McMsgRef McReply::releasedMsg(mc_op_t op) const {
  if (msg_.get() != nullptr &&
      msg_->op == op &&
      msg_->result == result_ &&
      msg_->flags == flags_ &&
      msg_->lease_id == leaseToken_ &&
      msg_->delta == delta_ &&
      msg_->err_code == errCode_ &&
      msg_->number == number_ &&
      // reply exptime is always non-negative and used only for metaget
      static_cast<uint32_t>(msg_->exptime) == exptime_ &&
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
    toRelease->number = number_;
    toRelease->exptime = exptime_;

    return std::move(toRelease);
  }
}

}}  // facebook::memcache
