/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>

#include <folly/io/IOBuf.h>
#include <folly/Range.h>
#include <folly/Varint.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace folly {
class IOBuf;
} // folly

namespace facebook { namespace memcache {

class McReply;
class McRequest;

constexpr char kCaretMagicByte = '^';
constexpr size_t kMaxAdditionalFields = 3;
constexpr size_t kMaxHeaderLength = 1 /* magic byte */ +
    1 /* GroupVarint header (lengths of 4 ints) */ +
    4 * sizeof(uint32_t) /* body size, typeId, reqId, num additional fields */ +
    2 * kMaxAdditionalFields * folly::kMaxVarintLength64; /* key and value for
                                                          additional fields */

enum class UmbrellaVersion : uint8_t {
  BASIC = 0,
  TYPED_MESSAGE = 1,
};

struct UmbrellaMessageInfo {
  uint32_t headerSize;
  uint32_t bodySize;
  UmbrellaVersion version;
  uint32_t typeId;
  uint32_t reqId;

  // Additional fields
  uint64_t traceId;
  uint64_t supportedCodecsFirstId{0};
  uint64_t supportedCodecsSize{0};
  uint64_t usedCodecId{0};
  uint64_t uncompressedBodySize{0};
};

enum class UmbrellaParseStatus {
  OK,
  MESSAGE_PARSE_ERROR,
  NOT_ENOUGH_DATA,
};

enum class CaretAdditionalFieldType {
  TRACE_ID = 0,

  // Range of supportted codecs
  SUPPORTED_CODECS_FIRST_ID = 1,
  SUPPORTED_CODECS_SIZE = 2,

  // Id of codec used to compress the data.
  USED_CODEC_ID = 3,

  // Size of body after decompression.
  UNCOMPRESSED_BODY_SIZE = 4,
};

UmbrellaParseStatus umbrellaParseHeader(const uint8_t* buf, size_t nbuf,
                                        UmbrellaMessageInfo& infoOut);

/**
 * Parses caret message header
 * and fills up the UmbrellaMessageInfo
 * @param pointer to buffer and length
 * @return parsed status
 */
UmbrellaParseStatus caretParseHeader(const uint8_t* buf,
                                     size_t nbuf,
                                     UmbrellaMessageInfo& info);

/**
 * Prepares the caret message header.
 * @param info          Header info.
 * @param headerBuffer  Pointer to buffer. Buffer must be large enough to
 *                      hold header and extra fields.
 * @return              Number of bytes written to buffer.
 */
size_t caretPrepareHeader(const UmbrellaMessageInfo& info, char* headerBuffer);

uint64_t umbrellaDetermineReqId(const uint8_t* header, size_t nheader);

/**
 * Determines the operation type of an umbrella message.
 *
 * @return  The operation type of the given message.
 * @throw   std::runtime_error  On any parse error.
 */
mc_op_t umbrellaDetermineOperation(const uint8_t* header, size_t nheader);

/**
 * Tells whether the given umbrella message is a reply.
 *
 * @return  True is the message is a reply. False if it is a request.
 * @throw   std::runtime_error  On any parse error.
 */
bool umbrellaIsReply(const uint8_t* header, size_t nheader);

/**
 * Parse an on-the-wire Umbrella reply.
 *
 * @param source           Unchained IOBuf; [body, body + nbody) must point
 *                         inside it.
 * @param header, nheader  [header, header + nheader) must point to a valid
 *                         Umbrella header.
 * @param body, nbody      [body, body + nbody) must point to a valid
 *                         Umbrella body stored in the `source` IOBuf
 * @paramOut opOut         Parsed operation.
 * @paramOut reqidOut      Parsed request ID
 * @return                 Parsed request.
 * @throws                 std::runtime_error on any parse error.
 */
template <class Request>
ReplyT<Request> umbrellaParseReply(const folly::IOBuf& source,
                                   const uint8_t* header, size_t nheader,
                                   const uint8_t* body, size_t nbody);

/**
 * Parse an on-the-wire Umbrella request.
 *
 * @param source           Unchained IOBuf; [body, body + nbody) must point
 *                         inside it.
 * @param header, nheader  [header, header + nheader) must point to a valid
 *                         Umbrella header.
 * @param body, nbody      [body, body + nbody) must point to a valid
 *                         Umbrella body stored in the `source` IOBuf
 * @paramOut opOut         Parsed operation.
 * @paramOut reqidOut      Parsed request ID
 * @return                 Parsed request.
 * @throws                 std::runtime_error on any parse error.
 */
McRequest umbrellaParseRequest(const folly::IOBuf& source,
                               const uint8_t* header, size_t nheader,
                               const uint8_t* body, size_t nbody,
                               mc_op_t& opOut, uint64_t& reqidOut);

/**
 * Similar to above, but allows the user to supply a Request structure that
 * should be filled in by parsing the supplied source IOBuf.
 * req is assumed to be default-constructed. Its existing fields will be
 * overwritten with the newly parsed data.
 */
template <class Request>
void umbrellaParseRequest(Request& req, const folly::IOBuf& source,
                          const uint8_t* header, size_t nheader,
                          const uint8_t* body, size_t nbody,
                          mc_op_t& opOut, uint64_t& reqidOut);

class UmbrellaSerializedMessage {
 public:
  UmbrellaSerializedMessage() noexcept;
  void clear();

  bool prepare(McReply&& reply, mc_op_t op, uint64_t reqid,
               const struct iovec*& iovOut, size_t& niovOut) {
    return prepareReplyImpl(std::move(reply), op, reqid, iovOut, niovOut);
  }

  template <class ThriftType>
  bool prepare(TypedThriftReply<ThriftType>&& reply, uint64_t reqid,
               const struct iovec*& iovOut, size_t& niovOut) {
    static constexpr mc_op_t op = OpFromType<ThriftType, ReplyOpMapping>::value;
    return prepareReplyImpl(std::move(reply), op, reqid, iovOut, niovOut);
  }

  template <int op>
  bool prepare(const McRequestWithMcOp<op>& request, uint64_t reqid,
               const struct iovec*& iovOut, size_t& niovOut) {
    return prepareRequestImpl(
        request, static_cast<mc_op_t>(op), reqid, iovOut, niovOut);
  }

  template <class ThriftType>
  bool prepare(const TypedThriftRequest<ThriftType>& request, uint64_t reqid,
               const struct iovec*& iovOut, size_t& niovOut) {
    static constexpr mc_op_t op =
      OpFromType<ThriftType, RequestOpMapping>::value;
    return prepareRequestImpl(request, op, reqid, iovOut, niovOut);
  }

 private:
  static constexpr size_t kMaxIovs = 16;
  struct iovec iovs_[kMaxIovs];

  entry_list_msg_t msg_;
  static constexpr size_t kInlineEntries = 16;
  size_t nEntries_{0};
  um_elist_entry_t entries_[kInlineEntries];

  static constexpr size_t kInlineStrings = 16;
  size_t nStrings_{0};
  folly::StringPiece strings_[kInlineStrings];

  folly::Optional<folly::IOBuf> iobuf_;
  folly::Optional<std::string> auxString_;

  size_t offset_{0};

  bool error_{false};

  void appendInt(entry_type_t type, int32_t tag, uint64_t val);
  void appendString(int32_t tag, const uint8_t* data, size_t len,
                    entry_type_t type = BSTRING);

  template <class Request>
  bool prepareRequestImpl(const Request& request, mc_op_t op, uint64_t reqid,
                          const struct iovec*& iovOut, size_t& niovOut);
  template <class Reply>
  bool prepareReplyImpl(Reply&& reply, mc_op_t op, uint64_t reqid,
                        const struct iovec*& iovOut, size_t& niovOut);

  /**
   * Request and reply helpers used to serialize fields specific to a
   * request/reply type. For example, serialize cas token for McCasRequest
   * and McGetsReply, which are the only types where serializing the cas field
   * makes sense.
   */

  /**
   * Most request and reply types don't have any type-specific fields that need
   * to be serialized, so we have a catch-all helper that does nothing.
   */
  template <class RequestOrReply>
  void prepareHelper(const RequestOrReply&) {
  }

  template <int op>
  void prepareHelper(const McRequestWithMcOp<op>& request) {
    if (request.delta()) {
      appendInt(U64, msg_delta, request.delta());
    }
    if (request.leaseToken()) {
      appendInt(U64, msg_lease_id, request.leaseToken());
    }
    if (request.cas()) {
      appendInt(U64, msg_cas, request.cas());
    }
  }

  inline void prepareHelper(const McReply& reply) {
    if (reply.delta()) {
      appendInt(U64, msg_delta, reply.delta());
    }
    if (reply.exptime()) {
      appendInt(U64, msg_exptime, reply.exptime());
    }
    if (reply.leaseToken()) {
      appendInt(U64, msg_lease_id, reply.leaseToken());
    }
    if (reply.cas()) {
      appendInt(U64, msg_cas, reply.cas());
    }
    if (reply.number()) {
      appendInt(U64, msg_number, reply.number());
    }
  }

  inline void prepareHelper(
      const TypedThriftRequest<cpp2::McCasRequest>& request) {
    appendInt(U64, msg_cas, request->get_casToken());
  }

  inline void prepareHelper(
      const TypedThriftReply<cpp2::McGetsReply>& reply) {
    if (reply->get_casToken()) {
      appendInt(U64, msg_cas, *reply->get_casToken());
    }
  }

  inline void prepareHelper(
      const TypedThriftRequest<cpp2::McLeaseSetRequest>& request) {
    appendInt(U64, msg_lease_id, request->get_leaseToken());
  }

  inline void prepareHelper(
      const TypedThriftReply<cpp2::McLeaseGetReply>& reply) {
    if (reply->get_leaseToken()) {
      appendInt(U64, msg_lease_id, *reply->get_leaseToken());
    }
  }

  inline void prepareHelper(
      const TypedThriftRequest<cpp2::McIncrRequest>& request) {
    appendInt(U64, msg_delta, request->get_delta());
  }

  inline void prepareHelper(const TypedThriftReply<cpp2::McIncrReply>& reply) {
    if (reply->get_delta()) {
      appendInt(U64, msg_delta, *reply->get_delta());
    }
  }

  inline void prepareHelper(
      const TypedThriftRequest<cpp2::McDecrRequest>& request) {
    appendInt(U64, msg_delta, request->get_delta());
  }

  inline void prepareHelper(const TypedThriftReply<cpp2::McDecrReply>& reply) {
    if (reply->get_delta()) {
      appendInt(U64, msg_delta, *reply->get_delta());
    }
  }

  inline void prepareHelper(
      const TypedThriftReply<cpp2::McMetagetReply>& reply) {
    if (reply->get_exptime()) {
      appendInt(U64, msg_exptime, *reply->get_exptime());
    }
    if (reply->get_age()) {
      appendInt(U64, msg_number, *reply->get_age());
    }
    if (reply->get_ipAddress()) {
      assert(!auxString_.hasValue());
      // TODO(jmswen) Move, not copy. Can't move here since reply is const ref.
      auxString_.emplace(*reply->get_ipAddress());
      appendString(msg_value,
                   reinterpret_cast<const uint8_t*>(auxString_->data()),
                   auxString_->size());
    }
  }

  /**
   * Put message header and all added entries/strings into iovecs.
   *
   * @return  number of iovecs that contain a complete message.
   */
  size_t finalizeMessage();

  UmbrellaSerializedMessage(const UmbrellaSerializedMessage&) = delete;
  UmbrellaSerializedMessage& operator=(
    const UmbrellaSerializedMessage&) = delete;
  UmbrellaSerializedMessage(UmbrellaSerializedMessage&&) noexcept = delete;
  UmbrellaSerializedMessage& operator=(UmbrellaSerializedMessage&&) = delete;
};

}} // facebook::memcache

#include "UmbrellaProtocol-inl.h"
