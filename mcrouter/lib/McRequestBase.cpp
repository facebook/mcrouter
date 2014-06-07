#include "McRequestBase.h"

#include "folly/Range.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

McRequestBase::McRequestBase(McMsgRef&& msg)
    : msg_(std::move(msg)),
      keyData_(McStringData::SaveMsgKey, msg_.clone()),
      valueData_(McStringData::SaveMsgValue, msg_.clone()),
      keys_(keyData_.dataRange()),
      exptime_(msg_->exptime),
      flags_(msg_->flags),
      delta_(msg_->delta),
      leaseToken_(msg_->lease_id) {
}

McRequestBase::McRequestBase(const std::string& key)
    : keyData_(key),
      keys_(keyData_.dataRange()) {
  auto msg = createMcMsgRef();
  msg->key = to<nstring_t>(keyData_.dataRange());
  msg_ = std::move(msg);
}


McMsgRef McRequestBase::dependentMsg(mc_op_t op) const {
  auto is_key_set =
    keyData_.hasSameMemoryRegion(to<folly::StringPiece>(msg_->key));
  auto is_value_set =
    valueData_.hasSameMemoryRegion(to<folly::StringPiece>(msg_->value));

  if (msg_->op == op &&
      msg_->exptime == exptime_ &&
      msg_->flags == flags_ &&
      msg_->delta == delta_ &&
      msg_->lease_id == leaseToken_ &&
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
    if (!is_key_set) {
      toRelease->key = to<nstring_t>(keyData_.dataRange());
    }
    if (!is_value_set) {
      toRelease->value = to<nstring_t>(valueData_.dataRange());
    }
    return std::move(toRelease);
  }
}

McMsgRef McRequestBase::dependentMsgStripRoutingPrefix(mc_op_t op) const {
  auto is_key_set =
    keys_.routingPrefix.empty() &&
    keyData_.hasSameMemoryRegion(to<folly::StringPiece>(msg_->key));
  auto is_value_set =
    valueData_.hasSameMemoryRegion(to<folly::StringPiece>(msg_->value));

  if (msg_->op == op &&
      msg_->exptime == exptime_ &&
      msg_->flags == flags_ &&
      msg_->delta == delta_ &&
      msg_->lease_id == leaseToken_ &&
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
      toRelease->value = to<nstring_t>(valueData_.dataRange());
    }
    toRelease->op = op;
    toRelease->exptime = exptime_;
    toRelease->flags = flags_;
    toRelease->delta = delta_;
    toRelease->lease_id = leaseToken_;
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

const McStringData& McRequestBase::value() const {
  return valueData_;
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
    : keyData_(other.keyData_),
      valueData_(other.valueData_),
      keys_(keyData_.dataRange()),
      exptime_(other.exptime_),
      flags_(other.flags_),
      delta_(other.delta_),
      leaseToken_(other.leaseToken_) {

  if (keyData_.hasSameMemoryRegion(other.keyData_) &&
      valueData_.hasSameMemoryRegion(other.valueData_)) {

    msg_ = other.msg_.clone();
  } else {
    msg_ = createMcMsgRef(
        other.msg_, keyData_.dataRange(), valueData_.dataRange());
  }
}

}}
