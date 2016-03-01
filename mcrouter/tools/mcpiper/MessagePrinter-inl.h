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

#include "mcrouter/tools/mcpiper/Color.h"
#include "mcrouter/tools/mcpiper/Config.h"
#include "mcrouter/tools/mcpiper/Util.h"

namespace facebook { namespace memcache {

template <class Request>
void MessagePrinter::requestReady(uint64_t msgId,
                                  Request request,
                                  const folly::SocketAddress& from,
                                  const folly::SocketAddress& to) {
  auto keyCopy = request.fullKey().str();
  printMessage(msgId, std::move(request), keyCopy, Request::OpType::mc_op,
               mc_res_unknown, from, to);
}

template <class Request>
void MessagePrinter::replyReady(uint64_t msgId,
                                ReplyT<Request> reply,
                                std::string key,
                                const folly::SocketAddress& from,
                                const folly::SocketAddress& to) {
  auto res = reply.result();
  printMessage(msgId, std::move(reply), key,
               Request::OpType::mc_op, res, from, to);
}

template <class Message>
void MessagePrinter::printMessage(uint64_t msgId,
                                  Message message,
                                  const std::string& key,
                                  mc_op_t op,
                                  mc_res_t result,
                                  const folly::SocketAddress& from,
                                  const folly::SocketAddress& to) {
  if (op == mc_op_end) {
    return;
  }

  ++totalMessages_;

  if (!matchAddress(from, to)) {
    return;
  }

  auto value = message.valueRangeSlow();
  if (value.size() < filter_.valueMinSize) {
    return;
  }

  StyledString out;
  out.append("\n");

  if (options_.printTimeFn) {
    timeval ts;
    gettimeofday(&ts, nullptr);
    out.append(options_.printTimeFn(ts));
  }

  out.append(serializeAddresses(from, to));
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
  if (message.exptime()) {
      out.append("\n  exptime: ", format_.msgAttrColor);
      out.append(folly::sformat("{:d}", message.exptime()),
                 format_.dataValueColor);
  }

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
      return;
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

  targetOut_ << out;
  targetOut_.flush();

  ++printedMessages_;

  if (options_.maxMessages > 0 && printedMessages_ >= options_.maxMessages) {
    assert(options_.stopRunningFn);
    options_.stopRunningFn(*this);
  }

  if (options_.numAfterMatch > 0) {
    --afterMatchCount_;
  }
}

}} // facebook::memcache
