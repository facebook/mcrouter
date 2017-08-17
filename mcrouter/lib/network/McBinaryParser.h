/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <exception>
#include <typeindex>

#include <folly/Optional.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/carbon/Variant.h"
#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/network/McAsciiParser.h"

namespace facebook {
namespace memcache {

namespace detail {
template <class RequestList>
class CallbackBase;
} // detail

class McServerBinaryParser {
 public:
  enum class State {
    // The parser is not initialized to parse any messages.
    UNINIT,
    // Have partial message, and need more data to complete it.
    PARTIAL_HEADER,
    PARTIAL_EXTRA,
    PARTIAL_KEY,
    PARTIAL_VALUE,
    // There was an error on the protocol level.
    ERROR,
    // Complete message had been parsed and ready to be returned.
    COMPLETE,
  };

  McServerBinaryParser() = default;

  McServerBinaryParser(const McServerBinaryParser&) = delete;
  McServerBinaryParser& operator=(const McServerBinaryParser&) = delete;

  template <class Callback>
  McServerBinaryParser(Callback& callback)
      : callback_(
            std::make_unique<detail::CallbackWrapper<Callback, McRequestList>>(
                callback)) {}

  State getCurrentState() const noexcept {
    return state_;
  }

  State consume(folly::IOBuf& buffer);
  std::unique_ptr<detail::CallbackBase<McRequestList>> callback_;

 protected:

  std::string currentErrorDescription_;

  uint64_t currentUInt_{0};

  folly::IOBuf* currentIOBuf_{nullptr};
  size_t remainingIOBufLength_{0};
  State state_{State::UNINIT};
  bool negative_{false};

  const char *sectionStart_;
  uint64_t sectionLength_;

  uint32_t getMagic() {
    return header_->magic;
  }
  uint8_t getOpCode() {
    return header_->opCode;
  }
  uint16_t getKeyLength() {
    return ntohs(header_->keyLen);
  }
  uint8_t getExtrasLength() {
    return header_->extrasLen;
  }
  uint16_t getValueLength() {
    return getTotalBodyLength() - getKeyLength() - getExtrasLength();
  }
  uint8_t getDataType() {
    return header_->dataType;
  }
  uint16_t getVBucketId() {
    return ntohs(header_->vBucketId);
  }
  uint64_t getTotalBodyLength() {
    return ntohl(header_->totalBodyLen);
  }
  uint32_t getOpaque() {
    return ntohl(header_->opaque);
  }
  uint32_t getCAS() {
    return ntohl(header_->cas);
  }

  static constexpr uint64_t HeaderLength = 24;

  typedef struct RequestHeader {
    uint8_t  magic;
    uint8_t  opCode;
    uint16_t keyLen;
    uint8_t  extrasLen;
    uint8_t  dataType;
    uint16_t vBucketId;
    uint32_t totalBodyLen;
    uint32_t opaque;
    uint32_t cas;
  } __attribute__((__packed__)) RequestHeader_t;

  typedef struct SetExtras {
    uint32_t flags;
    uint32_t exptime;
  } __attribute__((__packed__)) SetExtras_t;

  typedef struct ArithExtras {
    uint32_t delta;
    uint32_t initialValue;
    uint32_t exptime;
  } __attribute__((__packed__)) ArithExtras_t;

  typedef struct TouchExtras {
    uint32_t exptime;
  } __attribute__((__packed__)) TouchExtras_t;

  typedef struct FlushExtras {
    uint32_t exptime;
  } __attribute__((__packed__)) FlushExtras_t;

  bool parseHeader(const char* bytes);

  template <class Request>
  void consumeSetLike();

  template <class Request>
  void consumeAppendLike();

  template <class Request>
  void consumeGetLike();

  template <class Request>
  void consumeArithLike();

  template <class Request>
  void consumeUnary();

  void consumeFlush();

  // Network byte-ordered fields
  const RequestHeader_t *header_;

  folly::IOBuf currentExtras_;
  folly::IOBuf currentKey_;
  folly::IOBuf currentValue_;

  using ConsumerFunPtr = void (McServerBinaryParser::*)();
  ConsumerFunPtr consumer_{nullptr};

  using RequestVariant = carbon::makeVariantFromList<McRequestList>;
  RequestVariant currentMessage_;
};

}
} // facebook::memcache
