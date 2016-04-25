/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Bits.h>

#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/ThriftMessageTraits.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache {

namespace detail {

template <msg_field_t TagId> class Tag{};
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
  using Tags =
    List<CasTag, DeltaTag, ErrCodeTag, ExptimeTag, FlagsTag,
         LeaseTokenTag, NumberTag, ResultTag, ValueTag>;
};

/**
 * TagSet specializations for most memcache operations. Note that we include
 * FlagsTag and ValueTag in every TagSet. McReply uses the value field for error
 * messages, despite the fact that most operations don't logically have a
 * 'value' field. The flags field is used in certain TAO update operations.
 * Otherwise, FlagsTag is included (except for get) just to be safe.
 */

// Get-like ops
template <>
struct TagSet<McOperation<mc_op_get>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_get>, TypedThriftReply<cpp2::McGetReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_get>, TypedThriftRequest<cpp2::McGetRequest>> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag>;
};

template <>
struct TagSet<McOperation<mc_op_gets>, McReply> {
  using Tags = List<CasTag, ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_gets>, TypedThriftReply<cpp2::McGetsReply>> {
  using Tags = List<CasTag, ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_gets>,
              TypedThriftRequest<cpp2::McGetsRequest>> {
  using Tags = List<KeyTag>;
};

template <>
struct TagSet<McOperation<mc_op_metaget>, McReply> {
  using Tags = List<ErrCodeTag, ExptimeTag, FlagsTag, NumberTag, ResultTag,
                    ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_metaget>,
              TypedThriftReply<cpp2::McMetagetReply>> {
  using Tags = List<ErrCodeTag, ExptimeTag, FlagsTag, NumberTag, ResultTag,
                    ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_metaget>,
              TypedThriftRequest<cpp2::McMetagetRequest>> {
  using Tags = List<KeyTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_get>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, LeaseTokenTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_get>,
              TypedThriftReply<cpp2::McLeaseGetReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, LeaseTokenTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_get>,
              TypedThriftRequest<cpp2::McLeaseGetRequest>> {
  using Tags = List<KeyTag>;
};

// Update-like ops
template <>
struct TagSet<McOperation<mc_op_set>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_set>, TypedThriftReply<cpp2::McSetReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_set>, TypedThriftRequest<cpp2::McSetRequest>> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_add>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_add>, TypedThriftReply<cpp2::McAddReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_add>, TypedThriftRequest<cpp2::McAddRequest>> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_replace>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_replace>,
              TypedThriftReply<cpp2::McReplaceReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_replace>,
              TypedThriftRequest<cpp2::McReplaceRequest>> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_append>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_append>,
              TypedThriftReply<cpp2::McAppendReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_append>,
              TypedThriftRequest<cpp2::McAppendRequest>> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_prepend>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_prepend>,
              TypedThriftReply<cpp2::McPrependReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_prepend>,
              TypedThriftRequest<cpp2::McPrependRequest>> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_cas>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_cas>, TypedThriftReply<cpp2::McCasReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_cas>, TypedThriftRequest<cpp2::McCasRequest>> {
  using Tags = List<CasTag, ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_set>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_set>,
              TypedThriftReply<cpp2::McLeaseSetReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_set>,
              TypedThriftRequest<cpp2::McLeaseSetRequest>> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, LeaseTokenTag, ValueTag>;
};

// Arithmetic-like ops
template <>
struct TagSet<McOperation<mc_op_incr>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, DeltaTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_incr>, TypedThriftReply<cpp2::McIncrReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, DeltaTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_incr>,
              TypedThriftRequest<cpp2::McIncrRequest>> {
  using Tags = List<DeltaTag, KeyTag>;
};

template <>
struct TagSet<McOperation<mc_op_decr>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, DeltaTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_decr>, TypedThriftReply<cpp2::McDecrReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, DeltaTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_decr>,
              TypedThriftRequest<cpp2::McDecrRequest>> {
  using Tags = List<DeltaTag, KeyTag>;
};

// Delete-like ops
template <>
struct TagSet<McOperation<mc_op_delete>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_delete>,
              TypedThriftReply<cpp2::McDeleteReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_delete>,
              TypedThriftRequest<cpp2::McDeleteRequest>> {
  using Tags = List<ExptimeTag, FlagsTag, KeyTag, ValueTag>;
};

// Touch op
template <>
struct TagSet<McOperation<mc_op_touch>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_touch>,
              TypedThriftReply<cpp2::McTouchReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_touch>,
              TypedThriftRequest<cpp2::McTouchRequest>> {
  using Tags = List<ExptimeTag, KeyTag>;
};

// Version op
template <>
struct TagSet<McOperation<mc_op_version>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_version>,
              TypedThriftReply<cpp2::McVersionReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_version>,
              TypedThriftRequest<cpp2::McVersionRequest>> {
  using Tags = List<>;
};

// Flush
template <>
struct TagSet<McOperation<mc_op_flushall>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_flushall>,
              TypedThriftReply<cpp2::McFlushAllReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_flushall>,
              TypedThriftRequest<cpp2::McFlushAllRequest>> {
  using Tags = List<>;
};

template <>
struct TagSet<McOperation<mc_op_flushre>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_flushre>,
              TypedThriftReply<cpp2::McFlushReReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_flushre>,
              TypedThriftRequest<cpp2::McFlushReRequest>> {
  using Tags = List<>;
};

// Shutdown op
template <>
struct TagSet<McOperation<mc_op_shutdown>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_shutdown>,
              TypedThriftReply<cpp2::McShutdownReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_shutdown>,
              TypedThriftRequest<cpp2::McShutdownRequest>> {
  using Tags = List<>;
};

// Quit op
template <>
struct TagSet<McOperation<mc_op_quit>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_quit>, TypedThriftReply<cpp2::McQuitReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_quit>,
              TypedThriftRequest<cpp2::McQuitRequest>> {
  using Tags = List<>;
};

// Stats op
template <>
struct TagSet<McOperation<mc_op_stats>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_stats>, TypedThriftReply<cpp2::McStatsReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_stats>,
              TypedThriftRequest<cpp2::McStatsRequest>> {
  using Tags = List<KeyTag>;
};

// Exec op
template <>
struct TagSet<McOperation<mc_op_exec>, McReply> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_exec>, TypedThriftReply<cpp2::McExecReply>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_exec>,
              TypedThriftRequest<cpp2::McExecRequest>> {
  using Tags = List<>;
};


inline void mcReplySetMcMsgRef(McReply& reply, McMsgRef&& msg) {
  reply.msg_ = std::move(msg);
}

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
  parseField(Op, Tag, Message& message, const folly::IOBuf& source,
             const uint8_t* body, const um_elist_entry_t& entry) {
    parseFieldImpl(Op(), Tag(), message, source, body, entry);
  }

  template <class Op, class Message, class Tag>
  static typename std::enable_if<!Has<Tag, Tags...>::value, void>::type
  parseField(Op, Tag, Message& message, const folly::IOBuf& source,
             const uint8_t* body, const um_elist_entry_t& entry) {
    // If we're parsing a field that's not in the tags list, something is wrong
    LOG(ERROR)
      << "Parsing unexpected field with tag type " << typeid(Tag).name()
      << " for operation " << typeid(Op).name()
      << " and message " << typeid(Message).name();
  }
};

template <class Op, class Message>
void parseFieldImpl(Op, CasTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message->set_casToken(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, DeltaTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message->set_delta(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, ErrCodeTag, Message& message,
                    const folly::IOBuf& source, const uint8_t* body,
                    const um_elist_entry_t& entry) {
  message.setAppSpecificErrorCode(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, ExptimeTag, Message& message,
                    const folly::IOBuf& source, const uint8_t* body,
                    const um_elist_entry_t& entry) {
  message->set_exptime(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, FlagsTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message.setFlags(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, LeaseTokenTag, Message& message,
                    const folly::IOBuf& source, const uint8_t* body,
                    const um_elist_entry_t& entry) {
  message->set_leaseToken(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, NumberTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message->set_age(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, ResultTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  auto umResult = folly::Endian::big((uint64_t)entry.data.val);
  if (umResult >= mc_nres) {
    throw std::runtime_error(
      folly::sformat("Attempt to parse invalid data, received result is {}, "
                     "which exceeds the number of known results: {}.", umResult,
                     static_cast<uint64_t>(mc_nres)));
  }
  message.setResult((mc_res_t)kUmbrellaResToMc[umResult]);
}

template <class Op, class ThriftType>
void parseFieldImpl(Op, KeyTag, TypedThriftRequest<ThriftType>& message,
                    const folly::IOBuf& source, const uint8_t* body,
                    const um_elist_entry_t& entry) {
  folly::IOBuf tmp;
  if (!cloneInto(tmp, source,
                 body + folly::Endian::big((uint32_t)entry.data.str.offset),
                 folly::Endian::big((uint32_t)entry.data.str.len) - 1)) {
    throw std::runtime_error("Value: invalid offset/length");
  }
  message.setKey(std::move(tmp));
}

template <class Op, class Message>
void parseValueFieldImpl(Op, Message& message, const folly::IOBuf& source,
                         const uint8_t* body, const um_elist_entry_t& entry) {
  // TODO(jmswen) Consider migrating error messages from 'value' field to
  // 'message' field for TypedThriftReply
  folly::IOBuf tmp;
  if (!cloneInto(tmp, source,
                 body + folly::Endian::big((uint32_t)entry.data.str.offset),
                 folly::Endian::big((uint32_t)entry.data.str.len) - 1)) {
    throw std::runtime_error("Value: invalid offset/length");
  }
  message.setValue(std::move(tmp));
}

template <class Op, class Message>
void parseFieldImpl(Op, ValueTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  parseValueFieldImpl(Op(), message, source, body, entry);
}

inline void parseFieldImpl(McOperation<mc_op_metaget>, ValueTag,
                           McReply& message, const folly::IOBuf& source,
                           const uint8_t* body, const um_elist_entry_t& entry) {
  parseValueFieldImpl(McOperation<mc_op_metaget>(), message, source, body,
                      entry);
  // We need to ensure that it's a contiguous piece of memory.
  auto dataRange = message.valueRangeSlow();

  // Ignore bad addresses.
  if (dataRange.size() >= INET6_ADDRSTRLEN) {
    message.setValue(folly::IOBuf());
    return;
  }

  auto msg = createMcMsgRef();
  char buffer[INET6_ADDRSTRLEN] = {0};
  // Copy the data to ensure it's NULL terminated.
  memcpy(buffer, dataRange.data(), dataRange.size());
  msg->ipv = 0;
  if (inet_pton(AF_INET6, buffer, &msg->ip_addr) > 0) {
    msg->ipv = 6;
  } else if (inet_pton(AF_INET, buffer, &msg->ip_addr) > 0) {
    msg->ipv = 4;
  }
  if (msg->ipv > 0) {
    mcReplySetMcMsgRef(message, std::move(msg));
  } else {
    message.setValue(folly::IOBuf());
  }
}

inline void parseFieldImpl(McOperation<mc_op_metaget>, ValueTag,
                           TypedThriftReply<cpp2::McMetagetReply>& message,
                           const folly::IOBuf& source, const uint8_t* body,
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
  if (message.isError()) {
    message->set_message(std::move(valueStr));
    return;
  }

  // Ignore bad addresses.
  if (valueStr.size() >= INET6_ADDRSTRLEN) {
    return;
  }

  message->set_ipAddress(std::move(valueStr));

  char buffer[INET6_ADDRSTRLEN] = {0};
  char scratchBuffer[INET6_ADDRSTRLEN];
  memcpy(buffer, message->ipAddress.data(), message->ipAddress.size());
  if (inet_pton(AF_INET6, buffer, scratchBuffer) > 0) {
    message->set_ipv(6);
  } else if (inet_pton(AF_INET, buffer, scratchBuffer) > 0) {
    message->set_ipv(4);
  } else { // Bad IP address, unset IP-related fields
    message->ipAddress.clear();
    message->__isset.ipAddress = false;
  }
}

template <class Message, class Op>
void umbrellaParseMessage(Message& message, Op, const folly::IOBuf& source,
                          const uint8_t* header, size_t nheader,
                          const uint8_t* body, size_t nbody) {
  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries])
      != header + nheader) {
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
        Handler::parseField(Op(), LeaseTokenTag(), message, source, body,
                            entry);
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

}  // detail

template <class Request>
ReplyT<Request> umbrellaParseReply(const folly::IOBuf& source,
                                   const uint8_t* header, size_t nheader,
                                   const uint8_t* body, size_t nbody) {
  using namespace detail;
  using Op = typename Request::OpType;

  ReplyT<Request> reply;
  umbrellaParseMessage(reply, Op(), source, header, nheader, body, nbody);
  return reply;
}

template <class Request>
bool UmbrellaSerializedMessage::prepareRequestImpl(const Request& request,
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

  auto key = request.fullKey();
  if (key.begin() != nullptr) {
    appendString(msg_key, reinterpret_cast<const uint8_t*>(key.begin()),
                 key.size());
  }

  auto valueRange = request.valueRangeSlow();
  if (valueRange.begin() != nullptr) {
    appendString(msg_value,
                 reinterpret_cast<const uint8_t*>(valueRange.begin()),
                 valueRange.size());
  }

#ifndef LIBMC_FBTRACE_DISABLE

  auto fbtraceInfo = request.fbtraceInfo();
  if (fbtraceInfo) {
    auto fbtraceLen = strlen(fbtraceInfo->metadata);
    appendString(msg_fbtrace,
                 reinterpret_cast<const uint8_t*>(fbtraceInfo->metadata),
                 fbtraceLen, CSTRING);
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
bool UmbrellaSerializedMessage::prepareReplyImpl(Reply&& reply,
                                                 mc_op_t op,
                                                 uint64_t reqid,
                                                 const struct iovec*& iovOut,
                                                 size_t& niovOut) {
  niovOut = 0;
  iobuf_.clear();
  auxString_.clear();

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
  if (reply.hasValue()) {
    // TODO(jmswen) Use 'message' field for error messages instead of 'value'?
    assert(!iobuf_.hasValue());
    iobuf_.emplace(std::move(*reply.valuePtrUnsafe()));
    auto valueRange = coalesceAndGetRange(*iobuf_);
    appendString(msg_value,
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
void umbrellaParseRequest(Request& req, const folly::IOBuf& source,
                          const uint8_t* header, size_t nheader,
                          const uint8_t* body, size_t nbody,
                          uint64_t& reqidOut) {
  using Op = typename Request::OpType;

  reqidOut = 0;
  mc_op_t op = mc_op_unknown;

  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries])
      != header + nheader) {
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
}

}} // facebook::memcache
