#include "McStringData.h"

#include <string>

#include "folly/io/Cursor.h"
#include "mcrouter/lib/fbi/cpp/util.h"

using folly::IOBuf;
using folly::io::Cursor;
using std::string;
using std::unique_ptr;

namespace facebook { namespace memcache {

McStringData::RelativeRange::RelativeRange(size_t begin, size_t length) :
  begin_(begin), length_(length) {
}

McStringData::RelativeRange McStringData::RelativeRange::substr(
  size_t begin, size_t length) const {

  assert(begin <= length_);
  auto len_from_begin = length_ - begin;
  if (length == std::string::npos || length > len_from_begin) {
    length = len_from_begin;
  }
  return RelativeRange(begin_ + begin, length);
}

size_t McStringData::RelativeRange::length() const {
  return length_;
}

size_t McStringData::RelativeRange::begin() const {
  return begin_;
}

McStringData::McStringData()
    : storageType_(EMPTY), relativeRange_(0, 0) {
}

McStringData::McStringData(SaveMsgKey_t, McMsgRef&& msg) :
  relativeRange_(0, msg.get() ? msg->key.len : 0) {
  if (!relativeRange_.length()) {
    storageType_ = EMPTY;
  } else {
    storageType_ = SAVED_MSG_KEY;
    new (&savedMsg_)McMsgRef(std::move(msg));
  }
}

McStringData::McStringData(SaveMsgValue_t, McMsgRef&& msg) :
  relativeRange_(0, msg.get() ? msg->value.len : 0) {
  if (!relativeRange_.length()) {
    storageType_ = EMPTY;
  } else {
    storageType_ = SAVED_MSG_VALUE;
    new (&savedMsg_)McMsgRef(std::move(msg));
  }
}

McStringData::McStringData(const string& fromString) :
  relativeRange_(0, fromString.size()) {
  if (fromString.empty()) {
    storageType_ = EMPTY;
  } else {
    storageType_ = SAVED_BUF;
    new (&savedBuf_)unique_ptr<folly::IOBuf>(
       IOBuf::copyBuffer(fromString)
    );
  }
}

McStringData::McStringData(std::unique_ptr<folly::IOBuf> fromBuf) :
  relativeRange_(0, fromBuf->computeChainDataLength()) {
  storageType_ = SAVED_BUF;
  new (&savedBuf_)unique_ptr<folly::IOBuf>(fromBuf->clone());
}

McStringData::McStringData(const McStringData& from)
  : storageType_(EMPTY), relativeRange_(0, 0) {
  *this = from;
}

McStringData& McStringData::operator=(const McStringData& from) {
  this->~McStringData();
  relativeRange_ = from.relativeRange_;

  switch (from.storageType_) {
    case EMPTY:
      storageType_ = EMPTY;
      break;

    case SAVED_MSG_KEY:
    case SAVED_MSG_VALUE:
      storageType_ = from.storageType_;
      new (&savedMsg_)McMsgRef(from.savedMsg_.clone());
      break;

    case SAVED_BUF:
      storageType_ = SAVED_BUF;
      new (&savedBuf_)unique_ptr<folly::IOBuf>(from.savedBuf_->clone());
      break;
  }

  return *this;
}

McStringData::McStringData(McStringData&& from) noexcept
  : storageType_(EMPTY), relativeRange_(0, 0) {
  *this = std::move(from);
}

McStringData& McStringData::operator=(McStringData&& from) noexcept {
  this->~McStringData();
  storageType_ = from.storageType_;
  relativeRange_ = from.relativeRange_;

  from.storageType_ = EMPTY;

  switch (storageType_) {
    case EMPTY:
      break;

    case SAVED_MSG_KEY:
    case SAVED_MSG_VALUE:
      new (&savedMsg_)McMsgRef(std::move(from.savedMsg_));
      from.savedMsg_.~McMsgRef();
      break;

    case SAVED_BUF:
      new (&savedBuf_)unique_ptr<folly::IOBuf>(std::move(from.savedBuf_));
      from.savedBuf_.~unique_ptr();
      break;
  }

  return *this;
}

McStringData::~McStringData() noexcept {
  switch (storageType_) {
    case EMPTY:
      break;

    case SAVED_MSG_KEY:
    case SAVED_MSG_VALUE:
      savedMsg_.~McMsgRef();
      break;

    case SAVED_BUF:
      savedBuf_.~unique_ptr();
      break;
  }

  storageType_ = EMPTY;
}

size_t McStringData::length() const {
  return relativeRange_.length();
}

folly::StringPiece McStringData::dataRange() const noexcept {
  folly::StringPiece backing_sp;
  switch (storageType_) {
    case EMPTY:
      break;
    case SAVED_MSG_KEY:
      backing_sp = to<folly::StringPiece>(savedMsg_->key);
      break;
    case SAVED_MSG_VALUE:
      backing_sp = to<folly::StringPiece>(savedMsg_->value);
      break;
    case SAVED_BUF:
      savedBuf_->coalesce();
      backing_sp = folly::StringPiece(
        reinterpret_cast<const char*>(savedBuf_->data()), savedBuf_->length()
      );
      break;
  }

  return backing_sp.subpiece(relativeRange_.begin(),
      relativeRange_.length());
}

void McStringData::dataCopy(char* dst_buffer) const {
  assert(dst_buffer != nullptr);
  auto buf_unique = asIOBuf();
  if (buf_unique == nullptr) {
    return;
  }

  auto buf = buf_unique.get();
  char * iter_buf = dst_buffer;
  do {
    memcpy(iter_buf,
           buf->data(),
           buf->length());
    iter_buf += buf->length();
    buf = buf->next();
  } while(buf != buf_unique.get());
  return;
}

std::unique_ptr<folly::IOBuf> McStringData::asIOBuf() const {
  switch (storageType_) {
    case EMPTY:
      return nullptr;

    case SAVED_MSG_KEY:
    case SAVED_MSG_VALUE:
      {
        auto msg = savedMsg_.get();
        mc_msg_incref(const_cast<mc_msg_t*>(msg));
        auto sp = dataRange();
        return IOBuf::takeOwnership(
          const_cast<char *>(sp.data()),
          sp.size(),
          sp.size(),
          msgFreeFunction,
          const_cast<mc_msg_t*>(msg)
        );
      }
      break;

    case SAVED_BUF:
      {
        // savedBuf_ is backing IOBuf, need IOBuf with actual data as buffer
        Cursor cursor(savedBuf_.get());
        cursor.skip(relativeRange_.begin());
        std::unique_ptr<IOBuf> buf;
        cursor.clone(buf, relativeRange_.length());
        return buf;
      }
      break;

    default:
      throw std::runtime_error("Unknown storage type");
  }
}

bool McStringData::isChained() const {
  switch(storageType_) {
    case EMPTY:
    case SAVED_MSG_KEY:
    case SAVED_MSG_VALUE:
      return false;
    case SAVED_BUF:
      return savedBuf_->isChained();
    default:
      throw std::runtime_error("Unknown storage type");
  }
}

bool McStringData::hasSameMemoryRegion(folly::StringPiece sp) const {
  if (isChained()) {
    return false;
  }
  return sameMemoryRegion(dataRange(), sp);
}

bool McStringData::hasSameMemoryRegion(const McStringData& other) const {
  if (isChained() != other.isChained()) {
    return false;
  }
  if (!isChained()) {
    return sameMemoryRegion(dataRange(), other.dataRange());
  }
  // storageType_ is SAVED_BUF. this and other both have chained IOBuf
  auto buf = savedBuf_.get();
  auto buf_other = other.savedBuf_.get();
  while(true) {
    if (buf->data() != buf_other->data() ||
        buf->length() != buf_other->length()) {
      return false;
    }

    buf = buf->next();
    buf_other = buf_other->next();

    if (buf == savedBuf_.get() || buf_other == other.savedBuf_.get()) {
      break;
    }
  }
  return (buf == savedBuf_.get()) == (buf_other == other.savedBuf_.get());
}

void McStringData::msgFreeFunction(void* buf, void* userData) {
  mc_msg_decref(reinterpret_cast<mc_msg_t*>(userData));
}

McStringData McStringData::substr(size_t s_pos, size_t length) const {
  McStringData substrdata = *this;
  substrdata.relativeRange_ = relativeRange_.substr(s_pos, length);
  return substrdata;
}

}}  // facebook::memcache
