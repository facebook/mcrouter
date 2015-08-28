/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Bits.h>

#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/McReply.h"

namespace facebook { namespace memcache {

namespace detail {

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

template <class Tags>
class FieldPolicyHandler;

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
  message.setCas(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, DeltaTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message.setDelta(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, ErrCodeTag, Message& message,
                    const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message.setAppSpecificErrorCode(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, ExptimeTag, Message& message,
                    const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message.setExptime(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, FlagsTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message.setFlags(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, LeaseTokenTag, Message& message,
                    const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message.setLeaseToken(folly::Endian::big((uint64_t)entry.data.val));
}

template <class Op, class Message>
void parseFieldImpl(Op, NumberTag, Message& message, const folly::IOBuf& source,
                    const uint8_t* body, const um_elist_entry_t& entry) {
  message.setNumber(folly::Endian::big((uint64_t)entry.data.val));
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

template <class Tags, class Op, class Message>
void umbrellaParseMessage(Message& message, Op,
                          const folly::IOBuf& source,
                          const uint8_t* header, size_t nheader,
                          const uint8_t* body, size_t nbody) {
  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries])
      != header + nheader) {
    throw std::runtime_error("Invalid number of entries");
  }
  using Handler = FieldPolicyHandler<Tags>;
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

template <class Operation>
struct TagSet {
  using Tags =
    List<CasTag, DeltaTag, ErrCodeTag, ExptimeTag, FlagsTag,
         LeaseTokenTag, NumberTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_get>> {
  using Tags = List<ErrCodeTag, FlagsTag, ResultTag, ValueTag>;
};

template <>
struct TagSet<McOperation<mc_op_lease_get>> {
  using Tags = List<FlagsTag, LeaseTokenTag, ResultTag, ValueTag>;
};

}  // detail

template <class Operation, class Request>
typename ReplyType<Operation, Request>::type
umbrellaParseReply(const folly::IOBuf& source,
                   const uint8_t* header, size_t nheader,
                   const uint8_t* body, size_t nbody) {
  using namespace detail;

  typename ReplyType<Operation, Request>::type reply;
  umbrellaParseMessage<typename TagSet<Operation>::Tags>(
    reply, Operation(), source, header, nheader, body, nbody);
  return reply;
}

template<int Op>
bool UmbrellaSerializedMessage::prepare(const McRequest& request,
                                        McOperation<Op>, uint64_t reqid,
                                        struct iovec*& iovOut,
                                        size_t& niovOut) {
  niovOut = 0;

  appendInt(I32, msg_op, umbrella_op_from_mc[Op]);
  appendInt(U64, msg_reqid, reqid);
  if (request.flags()) {
    appendInt(U64, msg_flags, request.flags());
  }
  if (request.exptime()) {
    appendInt(U64, msg_exptime, request.exptime());
  }
  if (request.delta()) {
    appendInt(U64, msg_delta, request.delta());
  }
  if (request.leaseToken()) {
    appendInt(U64, msg_lease_id, request.leaseToken());
  }
  if (request.cas()) {
    appendInt(U64, msg_cas, request.cas());
  }

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

}} // facebook::memcache
