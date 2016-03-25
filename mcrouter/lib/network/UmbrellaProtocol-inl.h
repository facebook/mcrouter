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
using LeaseTokenTag = Tag<msg_lease_id>;
using NumberTag = Tag<msg_number>;
using ResultTag = Tag<msg_result>;
using ValueTag = Tag<msg_value>;

template <class Operation>
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
struct TagSet<McOperation<mc_op_get>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_gets>> {
  using Tags = List<CasTag, ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_metaget>> {
  using Tags = List<ErrCodeTag, ExptimeTag, FlagsTag, NumberTag, ResultTag,
                    ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_get>> {
  using Tags = List<ErrCodeTag, FlagsTag, LeaseTokenTag, ResultTag, ValueTag>;
};

// Update-like ops
template <>
struct TagSet<McOperation<mc_op_set>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_add>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_replace>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_append>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_prepend>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_cas>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_set>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

// Arithmetic-like ops
template <>
struct TagSet<McOperation<mc_op_incr>> {
  using Tags = List<ErrCodeTag, FlagsTag, DeltaTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_decr>> {
  using Tags = List<ErrCodeTag, FlagsTag, DeltaTag, ResultTag, ValueTag>;
};

// Delete-like ops
template <>
struct TagSet<McOperation<mc_op_delete>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

// Touch op
template <>
struct TagSet<McOperation<mc_op_touch>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

// Version op
template <>
struct TagSet<McOperation<mc_op_version>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
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
    /* Ignore this field, since it's not in tags list */
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

template <class Op, class Message>
void parseValueFieldImpl(Op, Message& message, const folly::IOBuf& source,
                         const uint8_t* body, const um_elist_entry_t& entry) {
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
  const auto ipAddrLen = folly::Endian::big((uint32_t)entry.data.str.len);
  // Avoid wraparound; copyAsString (below) requires that ipAddrLen - 1 > 0
  if (ipAddrLen <= 1) {
    return;
  }
  const auto begin = body + folly::Endian::big((uint32_t)entry.data.str.offset);
  auto strCopy = copyAsString(source, begin, ipAddrLen - 1);
  if (strCopy.empty()) { // range to copy was invalid
    throw std::runtime_error("Value: invalid offset/length");
  }

  // Ignore bad addresses.
  if (strCopy.size() >= INET6_ADDRSTRLEN) {
    return;
  }

  message->set_ipAddress(std::move(strCopy));

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

template <class Request>
void umbrellaParseMessage(ReplyT<Request>& message, const folly::IOBuf& source,
                          const uint8_t* header, size_t nheader,
                          const uint8_t* body, size_t nbody) {
  using Op = typename Request::OpType;

  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries])
      != header + nheader) {
    throw std::runtime_error("Invalid number of entries");
  }
  using Handler = FieldPolicyHandler<typename TagSet<Op>::Tags>;
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
        // We never expect to see fbtrace from server.
#endif
      case msg_key:
        // We never parse keys in replies.
      case msg_op:
        // Ignore operation.
      case msg_reqid:
        // ReqId is parsed prior to calling this function.
      case msg_stats:
        // We never expect stats.
        break;
      default:;
        // Silently ignore all other fields.
    }
  }
}

}  // detail

template <class Request>
ReplyT<Request> umbrellaParseReply(const folly::IOBuf& source,
                                   const uint8_t* header, size_t nheader,
                                   const uint8_t* body, size_t nbody) {
  using namespace detail;

  ReplyT<Request> reply;
  umbrellaParseMessage<Request>(reply, source, header, nheader, body, nbody);
  return reply;
}

template <class Request>
bool UmbrellaSerializedMessage::prepareRequestImpl(const Request& request,
                                                   mc_op_t op,
                                                   uint64_t reqid,
                                                   struct iovec*& iovOut,
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
bool UmbrellaSerializedMessage::prepareReplyImpl(const Reply& reply,
                                                 mc_op_t op,
                                                 uint64_t reqid,
                                                 struct iovec*& iovOut,
                                                 size_t& niovOut) {
  niovOut = 0;

  appendInt(I32, msg_op, umbrella_op_from_mc[op]);
  appendInt(U64, msg_reqid, reqid);
  appendInt(I32, msg_result, umbrella_res_from_mc[reply.result()]);

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
    auto valueRange = reply.valueRangeSlow();
    appendString(msg_value,
                 reinterpret_cast<const uint8_t*>(valueRange.begin()),
                 valueRange.size());
  }

  /* NOTE: this check must come after all append*() calls */
  if (error_) {
    return false;
  }

  niovOut = finalizeMessage();
  iovOut = iovs_;
  return true;
}

}} // facebook::memcache
