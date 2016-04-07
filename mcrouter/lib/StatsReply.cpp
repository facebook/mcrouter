/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "StatsReply.h"

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache {

namespace {

/* Copy from into to, storing data in p,
   which is advanced by from.size() after copying
   and checked p <= limit */
void copy(nstring_t& to, folly::StringPiece from,
          unsigned char*& p,
          const unsigned char* limit) {
  assert(p + from.size() <= limit);
  to.str = reinterpret_cast<char*>(p);
  to.len = from.size();
  memcpy(p, from.data(), from.size());
  p += from.size();
}
} // anonymous

McReply StatsReply::getMcReply() {
  /* In a single allocation, we store:
     |- mc_msg_t --|- stats_array --|- stats_key[0] --|- stats_val[0] --...|
            array --^      strings --^                              limit --^
   */

  size_t arraySize = sizeof(nstring_t) * stats_.size() * 2;
  size_t stringsSize = 0;
  for (const auto& s : stats_) {
    stringsSize += s.first.size() + s.second.size();
  }

  auto packedMsg = createMcMsgRef(arraySize + stringsSize);
  packedMsg->op = mc_op_stats;
  packedMsg->result = mc_res_ok;
  packedMsg->number = stats_.size();

  unsigned char* array = reinterpret_cast<unsigned char*>(packedMsg.get() + 1);
  unsigned char* strings = array + arraySize;
  unsigned char* limit = strings + stringsSize;

  packedMsg->stats = reinterpret_cast<nstring_t*>(array);

  size_t i = 0;
  for (const auto& s : stats_) {
    copy(packedMsg->stats[i++], s.first, strings, limit);
    copy(packedMsg->stats[i++], s.second, strings, limit);
  }
  assert(strings == limit);

  return McReply(mc_res_ok, std::move(packedMsg));
}

TypedThriftReply<cpp2::McStatsReply> StatsReply::getReply() {
  /**
   * In the 'stats' IOBuf, we store the string representation returned to
   * clients, e.g.,
   * "STAT stat1 value1\r\nSTAT stat2 value2\r\n..."
   */

  TypedThriftReply<cpp2::McStatsReply> reply(mc_res_ok);

  size_t stringsSize = 0;
  for (const auto& s : stats_) {
    // Extra room is for 'STAT ', another space and "\r\n"
    stringsSize += s.first.size() + s.second.size() + 8;
  }

  folly::IOBuf statsBuf(folly::IOBuf::CREATE, stringsSize);

  for (const auto& s : stats_) {
    // Write 'STAT'
    char* tail = reinterpret_cast<char*>(statsBuf.writableTail());
    memcpy(tail, "STAT ", 5);
    statsBuf.append(5);
    // Write stat name
    memcpy(statsBuf.writableTail(), s.first.data(), s.first.size());
    statsBuf.append(s.first.size());
    // Write space
    tail = reinterpret_cast<char*>(statsBuf.writableTail());
    tail[0] = ' ';
    statsBuf.append(1);
    // Write stat value
    memcpy(statsBuf.writableTail(), s.second.data(), s.second.size());
    statsBuf.append(s.second.size());
    // Write trailing "\r\n"
    tail = reinterpret_cast<char*>(statsBuf.writableTail());
    tail[0] = '\r';
    tail[1] = '\n';
    statsBuf.append(2);
  }

  assert(stringsSize == statsBuf.length());

  reply->set_stats(std::move(statsBuf));

  return reply;
}

}}  // facebook::memcache
