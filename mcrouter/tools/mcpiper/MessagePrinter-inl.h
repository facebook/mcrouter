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

#include <folly/Format.h>

#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/AsciiSerialized.h"
#include "mcrouter/lib/network/McSerializedRequest.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/tools/mcpiper/Color.h"
#include "mcrouter/tools/mcpiper/Config.h"
#include "mcrouter/tools/mcpiper/Util.h"

namespace facebook { namespace memcache {

namespace detail {

// Exptime
template <class M>
int32_t getExptime(const TypedThriftRequest<M>& req) {
  return req.exptime();
}
inline int32_t getExptime(const TypedThriftReply<cpp2::McMetagetReply>& reply) {
  return reply->get_exptime() ? *reply->get_exptime() : 0;
}
template <class M>
int32_t getExptime(const TypedThriftReply<M>& reply) {
  return 0;
}

// Lease token
template <class M>
int64_t getLeaseToken(const TypedThriftMessage<M>& msg) {
  return 0;
}
inline int64_t getLeaseToken(
    const TypedThriftReply<cpp2::McLeaseGetReply>& msg) {
  return msg->get_leaseToken() ? *msg->get_leaseToken() : 0;
}
inline int64_t getLeaseToken(
    const TypedThriftRequest<cpp2::McLeaseSetRequest>& msg) {
  return msg->get_leaseToken();
}

// Message
template <class M>
folly::StringPiece getMessage(const TypedThriftRequest<M>& msg) {
  return "";
}
template <class M>
folly::StringPiece getMessage(const TypedThriftReply<M>& msg) {
  if (msg->get_message()) {
    return *msg->get_message();
  }
  return "";
}

} // detail

template <class Request>
void MessagePrinter::requestReady(uint64_t msgId,
                                  Request&& request,
                                  const folly::SocketAddress& from,
                                  const folly::SocketAddress& to,
                                  mc_protocol_t protocol) {
  if (auto out = filterAndBuildOutput(
          msgId,
          request,
          request.fullKey().str(),
          Request::OpType::mc_op,
          mc_res_unknown,
          from,
          to,
          protocol,
          0 /* latency is undefined when request is sent */)) {
    if (options_.raw) {
      printRawRequest(msgId, request, protocol);
    } else {
      printMessage(out.value());
    }
  }
}

template <class Reply>
void MessagePrinter::replyReady(uint64_t msgId,
                                Reply&& reply,
                                std::string key,
                                const folly::SocketAddress& from,
                                const folly::SocketAddress& to,
                                mc_protocol_t protocol,
                                int64_t latencyUs) {
  if (auto out = filterAndBuildOutput(
          msgId,
          reply,
          key,
          Reply::OpType::mc_op,
          reply.result(),
          from,
          to,
          protocol,
          latencyUs)) {
    if (options_.raw) {
      printRawReply(msgId, std::forward<Reply>(reply), protocol);
    } else {
      printMessage(out.value());
    }
  }
}

template <class Message>
folly::Optional<StyledString> MessagePrinter::filterAndBuildOutput(
    uint64_t msgId,
    const Message& message,
    const std::string& key,
    mc_op_t op,
    mc_res_t result,
    const folly::SocketAddress& from,
    const folly::SocketAddress& to,
    mc_protocol_t protocol,
    int64_t latencyUs) {
  if (op == mc_op_end) {
    return folly::none;
  }

  ++totalMessages_;

  if (!matchAddress(from, to)) {
    return folly::none;
  }
  if (filter_.protocol.hasValue() && filter_.protocol.value() != protocol) {
    return folly::none;
  }

  auto value = message.valueRangeSlow();
  if (value.size() < filter_.valueMinSize ||
      value.size() > filter_.valueMaxSize) {
    return folly::none;
  }

  // if latency is 0 and the filter is not set, we let it pass through
  if (latencyUs < filter_.minLatencyUs) {
    return folly::none;
  }

  StyledString out;
  out.append("\n");

  if (options_.printTimeFn) {
    timeval ts;
    gettimeofday(&ts, nullptr);
    out.append(options_.printTimeFn(ts));
  }

  out.append(serializeConnectionDetails(from, to, protocol));
  out.append("\n");

  out.append("{\n", format_.dataOpColor);

  auto msgHeader = serializeMessageHeader(op, result, key);
  if (!msgHeader.empty()) {
    out.append("  ");
    out.append(std::move(msgHeader), format_.headerColor);
  }

  // Msg attributes
  out.append("\n  reqid: ", format_.msgAttrColor);
  out.append(folly::sformat("0x{:x}", msgId), format_.dataValueColor);

  if (latencyUs > 0) { // it is 0 only for requests
    out.append("\n  request/response latency (us): ",
      format_.msgAttrColor);
    out.append(folly::to<std::string>(latencyUs), format_.dataValueColor);
  }

  out.append("\n  flags: ", format_.msgAttrColor);
  out.append(folly::sformat("0x{:x}", message.flags()), format_.dataValueColor);
  if (message.flags()) {
    auto flagDesc = describeFlags(message.flags());
    if (!flagDesc.empty()) {
      out.pushAppendColor(format_.attrColor);
      out.append(" [");
      bool first = true;
      for (auto& s : flagDesc) {
        if (!first) {
          out.append(", ");
        }
        first = false;
        out.append(s);
      }
      out.pushBack(']');
      out.popAppendColor();
    }
  }

  if (detail::getExptime(message)) {
    out.append("\n  exptime: ", format_.msgAttrColor);
    out.append(folly::sformat("{:d}", detail::getExptime(message)),
               format_.dataValueColor);
  }
  if (auto leaseToken = detail::getLeaseToken(message)) {
    out.append("\n  lease-token: ", format_.msgAttrColor);
    out.append(folly::sformat("{:d}", leaseToken), format_.dataValueColor);
  }
  if (!detail::getMessage(message).empty()) {
    out.append("\n  message: ", format_.msgAttrColor);
    out.append(detail::getMessage(message).str(), format_.dataValueColor);
  }
  out.append(getTypeSpecificAttributes(message));

  out.pushBack('\n');

  if (!value.empty()) {
    size_t uncompressedSize;
    auto formattedValue =
      valueFormatter_->uncompressAndFormat(value,
                                           message.flags(),
                                           format_,
                                           uncompressedSize);

    out.append("  value size: ", format_.msgAttrColor);
    if (uncompressedSize != value.size()) {
      out.append(
        folly::sformat("{} uncompressed, {} compressed, {:.2f}% savings",
                       uncompressedSize, value.size(),
                       100.0 - 100.0 * value.size() / uncompressedSize),
        format_.dataValueColor);
    } else {
      out.append(folly::to<std::string>(value.size()), format_.dataValueColor);
    }

    if (!options_.quiet) {
      out.append("\n  value: ", format_.msgAttrColor);
      out.append(formattedValue);
    }
    out.pushBack('\n');
  }

  out.append("}\n", format_.dataOpColor);

  // Match pattern
  if (filter_.pattern) {
    auto matches = matchAll(out.text(), *filter_.pattern);
    auto success = matches.empty() == filter_.invertMatch;

    if (!success && afterMatchCount_ == 0) {
      return folly::none;
    }
    if (!filter_.invertMatch) {
      for (auto& m : matches) {
        out.setFg(m.first, m.second, format_.matchColor);
      }
    }

    // Reset after match
    if (success && options_.numAfterMatch > 0) {
      afterMatchCount_ = options_.numAfterMatch + 1;
    }
  }

  return out;
}

template <class Reply>
void MessagePrinter::printRawReply(uint64_t msgId,
                                   Reply&& reply,
                                   mc_protocol_t protocol) {
  const struct iovec* iovsBegin = nullptr;
  size_t iovsCount = 0;
  UmbrellaSerializedMessage umbrellaSerializedMessage;
  CaretSerializedMessage caretSerializedMessage;
  switch (protocol) {
    case mc_ascii_protocol:
      LOG_FIRST_N(INFO, 1) << "ASCII protocol is not supported for raw data";
      break;
    case mc_umbrella_protocol:
      umbrellaSerializedMessage.prepare(
          std::move(reply), msgId, iovsBegin, iovsCount);
      break;
    case mc_caret_protocol:
      caretSerializedMessage.prepare(
        std::move(reply),
        msgId,
        nullptr, /* codec */
        iovsBegin,
        iovsCount);
      break;
    default:
      CHECK(false);
  }

  printRawMessage(iovsBegin, iovsCount);
}

template <class Request>
void MessagePrinter::printRawRequest(uint64_t msgId,
                                     const Request& request,
                                     mc_protocol_t protocol) {
  if (protocol == mc_ascii_protocol) {
    LOG_FIRST_N(INFO, 1) << "ASCII protocol is not supported for raw data";
    return;
  }
  McSerializedRequest req(request, msgId, protocol, CodecIdRange());

  printRawMessage(req.getIovs(), req.getIovsCount());
}

template <class Message>
StyledString MessagePrinter::getTypeSpecificAttributes(const Message& msg) {
  StyledString out;
  return out;
}
template <>
inline StyledString MessagePrinter::getTypeSpecificAttributes(
    const TypedThriftReply<cpp2::McMetagetReply>& msg) {
  StyledString out;

  out.append("\n  age: ", format_.msgAttrColor);
  out.append(
      msg->get_age() ? folly::sformat("{:d}", *msg->get_age()) : "unknown",
      format_.dataValueColor);

  if (msg->get_ipAddress()) {
    out.append("\n  ip address: ", format_.msgAttrColor);
    out.append(folly::sformat("{}", *msg->get_ipAddress()),
               format_.dataValueColor);
  }

  return out;
}

}} // facebook::memcache
