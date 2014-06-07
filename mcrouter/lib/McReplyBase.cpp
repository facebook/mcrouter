#include "McReplyBase.h"

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

bool McReplyBase::worseThan(const McReplyBase& other) const {
  return awfulness(result_) > awfulness(other.result_);
}

McReplyBase McReplyBase::errorReply(McStringData valueToSet) {
  return McReplyBase(mc_res_local_error, std::move(valueToSet));
}

McReplyBase McReplyBase::tkoReply() {
  return McReplyBase(mc_res_tko);
}

bool McReplyBase::isError() const {
  switch (result_) {
    case mc_res_busy:
    case mc_res_tko:
    case mc_res_try_again:
    case mc_res_local_error:
    case mc_res_connect_error:
    case mc_res_connect_timeout:
    case mc_res_timeout:
      return true;

    case mc_res_remote_error:
      /* TAO metadata responses are remote_errors with
         a non-zero error code and a value data, let them through */
      if (msg_.get() == nullptr ||
          msg_->err_code == 0 ||
          msg_->value.len == 0) {
        return true;
      }
      /* fallthrough */

    default:
      return false;
  }
}

void McReplyBase::setValue(McStringData valueData) {
  valueData_ = valueData;
}

void McReplyBase::setResult(mc_res_t res) {
  result_ = res;
}

McReplyBase::McReplyBase(mc_res_t res)
    : result_(res) {
}

McReplyBase::McReplyBase(mc_res_t res, McStringData val)
    : result_(res),
      valueData_(val) {
}

McReplyBase::McReplyBase(mc_res_t res, McMsgRef&& msg)
    : msg_(std::move(msg)),
      result_(res),
      valueData_(McStringData::SaveMsgValue, msg_.clone()),
      flags_(msg_.get() ? msg_->flags : 0),
      leaseToken_(msg_.get() ? msg_->lease_id : 0),
      delta_(msg_.get() ? msg_->delta : 0) {
}

void McReplyBase::dependentMsg(mc_op_t op, mc_msg_t* out) const {
  if (msg_.get() != nullptr) {
    mc_msg_shallow_copy(out, msg_.get());
  }

  auto value = valueData_.dataRange();

  out->key.str = nullptr;
  out->key.len = 0;
  out->value.str = const_cast<char*>(value.begin());
  out->value.len = value.size();
  out->op = op;
  out->result = result_;
  out->flags = flags_;
  out->lease_id = leaseToken_;
  out->delta = delta_;
}

McMsgRef McReplyBase::releasedMsg(mc_op_t op) const {
  if (msg_.get() != nullptr &&
      msg_->op == op &&
      msg_->result == result_ &&
      msg_->flags == flags_ &&
      msg_->lease_id == leaseToken_ &&
      msg_->delta == delta_ &&
      valueData_.hasSameMemoryRegion(to<folly::StringPiece>(msg_->value))) {
    return msg_.clone();
  } else {
    auto toRelease = createMcMsgRef(valueData_.length() + 1);
    if (msg_.get() != nullptr) {
      mc_msg_shallow_copy(toRelease.get(), msg_.get());
      // TODO: fbtrace?
    }
    toRelease->key.str = nullptr;
    toRelease->key.len = 0;
    toRelease->value.str =
      static_cast<char*>(static_cast<void*>(toRelease.get() + 1));
    valueData_.dataCopy(toRelease->value.str);
    toRelease->value.len = valueData_.length();
    toRelease->op = op;
    toRelease->result = result_;
    toRelease->flags = flags_;
    toRelease->lease_id = leaseToken_;
    toRelease->delta = delta_;

    return std::move(toRelease);
  }
}

}}  // facebook::memcache
