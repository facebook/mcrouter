/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <arpa/inet.h>

#include <folly/lang/Bits.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/network/MemcacheMessageHelpers.h"

namespace facebook {
namespace memcache {

namespace detail {

template <msg_field_t TagId>
class Tag {};
using CasTag = Tag<msg_cas>;
using DeltaTag = Tag<msg_delta>;
using ErrCodeTag = Tag<msg_err_code>;
using ExptimeTag = Tag<msg_exptime>;
using FlagsTag = Tag<msg_flags>;
using KeyTag = Tag<msg_key>;
using LeaseTokenTag = Tag<msg_lease_id>;
using NumberTag = Tag<msg_number>;
using ResultTag = Tag<msg_result>;
using ValueTag = Tag<msg_value>;

template <class Operation, class Message>
struct TagSet {
  using Tags = List<
      CasTag,
      DeltaTag,
      ErrCodeTag,
      ExptimeTag,
      FlagsTag,
      LeaseTokenTag,
      NumberTag,
      ResultTag,
      ValueTag>;
};

/**
 * TagSet specializations for most memcache operations. Note that we include
 * FlagsTag and ValueTag in every TagSet, sometimes just to be safe.
 */

// Get-like ops
template <>
struct TagSet<McOperation<mc_op_get>, McGetReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_get>, McGetRequest> {
  using Tags = List<FlagsTag, KeyTag>;
};

template <>
struct TagSet<McOperation<mc_op_gets>, McGetsReply> {
  using Tags = List<CasTag, ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_gets>, McGetsRequest> {
  using Tags = List<KeyTag>;
};

template <>
struct TagSet<McOperation<mc_op_metaget>, McMetagetReply> {
  using Tags =
      List<ErrCodeTag, ExptimeTag, FlagsTag, NumberTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_metaget>, McMetagetRequest> {
  using Tags = List<KeyTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_get>, McLeaseGetReply> {
  using Tags = List<ErrCodeTag, FlagsTag, LeaseTokenTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_get>, McLeaseGetRequest> {
  using Tags = List<KeyTag>;
};

// Update-like ops
template <>
struct TagSet<McOperation<mc_op_set>, McSetReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_set>, McSetRequest> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_add>, McAddReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_add>, McAddRequest> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_replace>, McReplaceReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_replace>, McReplaceRequest> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_append>, McAppendReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_append>, McAppendRequest> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_prepend>, McPrependReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_prepend>, McPrependRequest> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_cas>, McCasReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_cas>, McCasRequest> {
  using Tags = List<CasTag, ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_set>, McLeaseSetReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_set>, McLeaseSetRequest> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, LeaseTokenTag, ValueTag>;
};

// Arithmetic-like ops
template <>
struct TagSet<McOperation<mc_op_incr>, McIncrReply> {
  using Tags = List<ErrCodeTag, FlagsTag, DeltaTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_incr>, McIncrRequest> {
  using Tags = List<DeltaTag, KeyTag>;
};

template <>
struct TagSet<McOperation<mc_op_decr>, McDecrReply> {
  using Tags = List<ErrCodeTag, FlagsTag, DeltaTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_decr>, McDecrRequest> {
  using Tags = List<DeltaTag, KeyTag>;
};

// Delete-like ops
template <>
struct TagSet<McOperation<mc_op_delete>, McDeleteReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_delete>, McDeleteRequest> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

// Touch op
template <>
struct TagSet<McOperation<mc_op_touch>, McTouchReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_touch>, McTouchRequest> {
  using Tags = List<ExptimeTag, KeyTag>;
};

// Version op
template <>
struct TagSet<McOperation<mc_op_version>, McVersionReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_version>, McVersionRequest> {
  using Tags = List<>;
};

// Flush
template <>
struct TagSet<McOperation<mc_op_flushall>, McFlushAllReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_flushall>, McFlushAllRequest> {
  using Tags = List<>;
};

template <>
struct TagSet<McOperation<mc_op_flushre>, McFlushReReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_flushre>, McFlushReRequest> {
  using Tags = List<>;
};

// Shutdown op
template <>
struct TagSet<McOperation<mc_op_shutdown>, McShutdownReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_shutdown>, McShutdownRequest> {
  using Tags = List<>;
};

// Quit op
template <>
struct TagSet<McOperation<mc_op_quit>, McQuitReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_quit>, McQuitRequest> {
  using Tags = List<>;
};

// Stats op
template <>
struct TagSet<McOperation<mc_op_stats>, McStatsReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_stats>, McStatsRequest> {
  using Tags = List<KeyTag>;
};

// Exec op
template <>
struct TagSet<McOperation<mc_op_exec>, McExecReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_exec>, McExecRequest> {
  using Tags = List<>;
};

uint32_t const kUmbrellaResToMc[mc_nres] = {
#define UM_RES(mc, um) [um] = mc,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

uint32_t const kUmbrellaOpToMc[UM_NOPS] = {
#define UM_OP(mc, um) [um] = mc,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

template <class Tags>
struct FieldPolicyHandler;

template <class... Tags>
struct FieldPolicyHandler<List<Tags...>> {
  template <class Op, class Message, class Tag>
  static typename std::enable_if<Has<Tag, Tags...>::value, void>::type
  parseField(
      Op,
      Tag,
      Message& message,
      const folly::IOBuf& source,
      const uint8_t* body,
      const um_elist_entry_t& entry) {
    parseFieldImpl(Op(), Tag(), message, source, body, entry);
  }

  template <class Op, class Message, class Tag>
  static typename std::enable_if<!Has<Tag, Tags...>::value, void>::type
  parseField(
      Op,
      Tag,
      Message& /* message */,
      const folly::IOBuf& /* source */,
      const uint8_t* /* body */,
      const um_elist_entry_t& /* entry */) {
    // If we're parsing a field that's not in the tags list, something is wrong
    LOG(ERROR) << "Parsing unexpected field with tag type "
               << typeid(Tag).name() << " for operation " << typeid(Op).name()
               << " and message " << typeid(Message).name();
  }
};

template <class Op, class Message>
void parseFieldImpl(
    Op,
    CasTag,
    Message& message,
    const folly::IOBuf& /* source */,
    const uint8_t* /* body */,
    const um_elist_entry_t& entry) {
  message.casToken() = folly::Endian::big((uint64_t)entry.data.val);
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    DeltaTag,
    Message& message,
    const folly::IOBuf& /* source */,
    const uint8_t* /* body */,
    const um_elist_entry_t& entry) {
  message.delta() = folly::Endian::big((uint64_t)entry.data.val);
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    ErrCodeTag,
    Message& message,
    const folly::IOBuf& /* source */,
    const uint8_t* /* body */,
    const um_elist_entry_t& entry) {
  message.appSpecificErrorCode() = folly::Endian::big((uint64_t)entry.data.val);
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    ExptimeTag,
    Message& message,
    const folly::IOBuf& /* source */,
    const uint8_t* /* body */,
    const um_elist_entry_t& entry) {
  setExptime(message, folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    FlagsTag,
    Message& message,
    const folly::IOBuf& /* source */,
    const uint8_t* /* body */,
    const um_elist_entry_t& entry) {
  setFlags(message, folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    LeaseTokenTag,
    Message& message,
    const folly::IOBuf& /* source */,
    const uint8_t* /* body */,
    const um_elist_entry_t& entry) {
  message.leaseToken() = folly::Endian::big((uint64_t)entry.data.val);
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    NumberTag,
    Message& message,
    const folly::IOBuf& /* source */,
    const uint8_t* /* body */,
    const um_elist_entry_t& entry) {
  message.age() = folly::Endian::big((uint64_t)entry.data.val);
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    ResultTag,
    Message& message,
    const folly::IOBuf& /* source */,
    const uint8_t* /* body */,
    const um_elist_entry_t& entry) {
  auto umResult = folly::Endian::big((uint64_t)entry.data.val);
  if (umResult >= mc_nres) {
    throw std::runtime_error(folly::sformat(
        "Attempt to parse invalid data, received result is {}, "
        "which exceeds the number of known results: {}.",
        umResult,
        static_cast<uint64_t>(mc_nres)));
  }
  message.result() = (mc_res_t)kUmbrellaResToMc[umResult];
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    KeyTag,
    Message& message,
    const folly::IOBuf& source,
    const uint8_t* body,
    const um_elist_entry_t& entry) {
  folly::IOBuf tmp;
  if (!cloneInto(
          tmp,
          source,
          body + folly::Endian::big((uint32_t)entry.data.str.offset),
          folly::Endian::big((uint32_t)entry.data.str.len) - 1)) {
    throw std::runtime_error("Value: invalid offset/length");
  }
  message.key() = std::move(tmp);
}

template <class Op, class Message>
void parseValueFieldImpl(
    Op,
    Message& message,
    const folly::IOBuf& source,
    const uint8_t* body,
    const um_elist_entry_t& entry) {
  // TODO(jmswen) Consider migrating error messages from 'value' field to
  // 'message' field for Carbon replies
  folly::IOBuf tmp;
  if (!cloneInto(
          tmp,
          source,
          body + folly::Endian::big((uint32_t)entry.data.str.offset),
          folly::Endian::big((uint32_t)entry.data.str.len) - 1)) {
    throw std::runtime_error("Value: invalid offset/length");
  }
  setValue(message, std::move(tmp));
}

template <class Op, class Message>
void parseFieldImpl(
    Op,
    ValueTag,
    Message& message,
    const folly::IOBuf& source,
    const uint8_t* body,
    const um_elist_entry_t& entry) {
  parseValueFieldImpl(Op(), message, source, body, entry);
}

inline void parseFieldImpl(
    McOperation<mc_op_metaget>,
    ValueTag,
    McMetagetReply& message,
    const folly::IOBuf& source,
    const uint8_t* body,
    const um_elist_entry_t& entry) {
  const auto valueLen = folly::Endian::big((uint32_t)entry.data.str.len);
  // Avoid wraparound; copyAsString (below) requires that valueLen - 1 > 0
  if (valueLen <= 1) {
    return;
  }
  const auto begin = body + folly::Endian::big((uint32_t)entry.data.str.offset);
  auto valueStr = copyAsString(source, begin, valueLen - 1);
  if (valueStr.empty()) { // range to copy was invalid
    throw std::runtime_error("Value: invalid offset/length");
  }

  // In case of error, 'value' field contains error message
  if (isErrorResult(message.result())) {
    message.message() = std::move(valueStr);
    return;
  }

  // Ignore bad addresses.
  if (valueStr.size() >= INET6_ADDRSTRLEN) {
    return;
  }

  message.ipAddress() = std::move(valueStr);

  char buffer[INET6_ADDRSTRLEN] = {0};
  char scratchBuffer[INET6_ADDRSTRLEN];
  memcpy(buffer, message.ipAddress().data(), message.ipAddress().size());
  if (inet_pton(AF_INET6, buffer, scratchBuffer) > 0) {
    message.ipv() = 6;
  } else if (inet_pton(AF_INET, buffer, scratchBuffer) > 0) {
    message.ipv() = 4;
  } else { // Bad IP address, unset IP-related fields
    message.ipAddress().clear();
  }
}

template <class Message, class Op>
void umbrellaParseMessage(
    Message& message,
    Op,
    const folly::IOBuf& source,
    const uint8_t* header,
    size_t nheader,
    const uint8_t* body,
    size_t /* nbody */) {
  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries]) !=
      header + nheader) {
    throw std::runtime_error("Invalid number of entries");
  }
  using Handler = FieldPolicyHandler<typename TagSet<Op, Message>::Tags>;
  for (size_t i = 0; i < nentries; ++i) {
    // Process entries in the reverse order, since it's easier to handle
    // double fields that way.
    auto& entry = msg->entries[nentries - i - 1];
    size_t tag = folly::Endian::big((uint16_t)entry.tag);
    switch (tag) {
      case msg_cas:
        Handler::parseField(Op(), CasTag(), message, source, body, entry);
        break;
      case msg_delta:
        Handler::parseField(Op(), DeltaTag(), message, source, body, entry);
        break;
      case msg_err_code:
        Handler::parseField(Op(), ErrCodeTag(), message, source, body, entry);
        break;
      case msg_exptime:
        Handler::parseField(Op(), ExptimeTag(), message, source, body, entry);
        break;
      case msg_flags:
        Handler::parseField(Op(), FlagsTag(), message, source, body, entry);
        break;
      case msg_lease_id:
        Handler::parseField(
            Op(), LeaseTokenTag(), message, source, body, entry);
        break;
      case msg_number:
        Handler::parseField(Op(), NumberTag(), message, source, body, entry);
        break;
      case msg_result:
        Handler::parseField(Op(), ResultTag(), message, source, body, entry);
        break;
      case msg_value:
        Handler::parseField(Op(), ValueTag(), message, source, body, entry);
        break;
#ifndef LIBMC_FBTRACE_DISABLE
      case msg_fbtrace:
        // For requests, we should have previously parsed fbtrace.
        break;
#endif
      case msg_key:
        Handler::parseField(Op(), KeyTag(), message, source, body, entry);
        break;
      case msg_op:
        // For requests, we should have previously parsed operation.
        break;
      case msg_reqid:
        // ReqId is parsed prior to calling this function.
        break;
      case msg_stats:
        // We never expect stats.
        break;
      default:;
        // Silently ignore all other fields.
        break;
    }
  }
}

template <class Request>
typename std::enable_if<Request::hasKey, folly::StringPiece>::type getKey(
    const Request& req) {
  return req.key().fullKey();
}

template <class Request>
typename std::enable_if<!Request::hasKey, folly::StringPiece>::type getKey(
    const Request&) {
  return folly::StringPiece();
}

} // detail

template <class Request>
ReplyT<Request> umbrellaParseReply(
    const folly::IOBuf& source,
    const uint8_t* header,
    size_t nheader,
    const uint8_t* body,
    size_t nbody) {
  using namespace detail;
  using Op = McOperation<OpFromType<Request, RequestOpMapping>::value>;

  ReplyT<Request> reply;
  umbrellaParseMessage(reply, Op(), source, header, nheader, body, nbody);
  return reply;
}

template <class Request>
bool UmbrellaSerializedMessage::prepareRequestImpl(
    const Request& request,
    mc_op_t op,
    uint64_t reqid,
    const struct iovec*& iovOut,
    size_t& niovOut) {
  niovOut = 0;

  appendInt(I32, msg_op, umbrella_op_from_mc[op]);
  appendInt(U64, msg_reqid, reqid);
  if (request.flags()) {
    appendInt(U64, msg_flags, request.flags());
  }
  if (request.exptime()) {
    appendInt(U64, msg_exptime, request.exptime());
  }

  // Serialize type-specific fields, e.g., cas token for cas requests
  prepareHelper(request);

  auto key = detail::getKey(request);
  if (key.begin() != nullptr) {
    appendString(
        msg_key, reinterpret_cast<const uint8_t*>(key.begin()), key.size());
  }

  auto valuePtr = carbon::valuePtrUnsafe(request);
  if (valuePtr != nullptr) {
    auto valueRange = coalesceAndGetRange(const_cast<folly::IOBuf&>(*valuePtr));
    appendString(
        msg_value,
        reinterpret_cast<const uint8_t*>(valueRange.begin()),
        valueRange.size());
  }

#ifndef LIBMC_FBTRACE_DISABLE

  auto fbtraceInfo = request.fbtraceInfo();
  if (fbtraceInfo) {
    auto fbtraceLen = strlen(fbtraceInfo->metadata);
    appendString(
        msg_fbtrace,
        reinterpret_cast<const uint8_t*>(fbtraceInfo->metadata),
        fbtraceLen,
        CSTRING);
  }

#endif

  /* NOTE: this check must come after all append*() calls */
  if (error_) {
    return false;
  }

  niovOut = finalizeMessage();
  iovOut = iovs_;
  return true;
}

template <class Reply>
bool UmbrellaSerializedMessage::prepareReplyImpl(
    Reply&& reply,
    mc_op_t op,
    uint64_t reqid,
    const struct iovec*& iovOut,
    size_t& niovOut) {
  niovOut = 0;

  appendInt(I32, msg_op, umbrella_op_from_mc[op]);
  appendInt(U64, msg_reqid, reqid);
  const mc_res_t result = reply.result();

  if (reply.appSpecificErrorCode()) {
    appendInt(I32, msg_err_code, reply.appSpecificErrorCode());
  }

  if (reply.flags()) {
    appendInt(U64, msg_flags, reply.flags());
  }

  // Serialize type-specific fields, e.g., cas token for gets reply
  prepareHelper(reply);
  /* TODO: if we intend to pass chained IOBufs as values,
     we can optimize this to write multiple iovs directly */
  if (auto valuePtr = carbon::valuePtrUnsafe(reply)) {
    // TODO(jmswen) Use 'message' field for error messages instead of 'value'?
    assert(!iobuf_.hasValue());
    iobuf_.emplace(std::move(*valuePtr));
    auto valueRange = coalesceAndGetRange(*iobuf_);
    appendString(
        msg_value,
        reinterpret_cast<const uint8_t*>(valueRange.begin()),
        valueRange.size());
  }

  // It is important that we write msg_result after msg_value. Parsing
  // (which happens in reverse order) depends on this ordering.
  appendInt(I32, msg_result, umbrella_res_from_mc[result]);

  /* NOTE: this check must come after all append*() calls */
  if (error_) {
    return false;
  }

  niovOut = finalizeMessage();
  iovOut = iovs_;
  return true;
}

template <class Request>
Request umbrellaParseRequest(
    const folly::IOBuf& source,
    const uint8_t* header,
    size_t nheader,
    const uint8_t* body,
    size_t nbody,
    uint64_t& reqidOut) {
  using Op = McOperation<OpFromType<Request, RequestOpMapping>::value>;

  Request req;
  reqidOut = 0;
  mc_op_t op = mc_op_unknown;

  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries]) !=
      header + nheader) {
    throw std::runtime_error("Invalid number of entries");
  }

  // In first pass, only look for msg_reqid or msg_op. Then, we'll dispatch
  // to the appropriate field handlers in umbrellaParseMessage().
  for (size_t i = 0; i < nentries; ++i) {
    const auto& entry = msg->entries[i];
    const size_t tag = folly::Endian::big((uint16_t)entry.tag);

    if (tag == msg_reqid) {
      const size_t val = folly::Endian::big((uint64_t)entry.data.val);
      if (val == 0) {
        throw std::runtime_error("invalid reqid");
      }
      reqidOut = val;
    }

    if (tag == msg_op) {
      const size_t val = folly::Endian::big((uint64_t)entry.data.val);
      if (val >= UM_NOPS) {
        throw std::runtime_error("op out of range");
      }
      op = static_cast<mc_op_t>(umbrella_op_to_mc[val]);
      assert(op == Op::mc_op);
    }

#ifndef LIBMC_FBTRACE_DISABLE
    if (tag == msg_fbtrace) {
      const auto off = folly::Endian::big((uint32_t)entry.data.str.offset);
      const auto len = folly::Endian::big((uint32_t)entry.data.str.len) - 1;

      if (len > FBTRACE_METADATA_SZ) {
        throw std::runtime_error("Fbtrace metadata too large");
      }
      if (off + len > nbody || off + len < off) {
        throw std::runtime_error("Fbtrace metadata field invalid");
      }
      auto fbtraceInfo = new_mc_fbtrace_info(0);
      memcpy(fbtraceInfo->metadata, body + off, len);
      req.setFbtraceInfo(fbtraceInfo);
      break;
    }
#endif
  }

  if (op == mc_op_unknown) {
    throw std::runtime_error("Request missing operation");
  }

  if (!reqidOut) {
    throw std::runtime_error("Request missing reqid");
  }

  // Fill in request-specific fields in second pass
  detail::umbrellaParseMessage(req, Op(), source, header, nheader, body, nbody);

  return req;
}
}
} // facebook::memcache
