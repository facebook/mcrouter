/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

#include <bits/types.h>
#include <gtest/gtest.h>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/network/AsciiSerialized.h"
#include "mcrouter/lib/carbon/Keys.h"

using namespace facebook::memcache;

namespace {

template <class Request> std::string getSerialized(const Request &&req) {
  AsciiSerializedRequest sreq;
  std::ostringstream os;
  const struct iovec *ioVecs;
  size_t nIoVecs;

  sreq.prepare(std::move(req), ioVecs, nIoVecs);

  for (size_t i = 0; i < nIoVecs; i++) {
    os << std::string_view{static_cast<const char *>(ioVecs[i].iov_base),
                           ioVecs[i].iov_len};
  }

  return os.str();
}

template <class Reply>
std::string getSerialized(Reply &&res, folly::Optional<folly::IOBuf> &&key) {
  AsciiSerializedReply sres;
  std::ostringstream os;
  const struct iovec *ioVecs;
  size_t nIoVecs;

  sres.prepare(std::move(res), key, ioVecs, nIoVecs);

  for (size_t i = 0; i < nIoVecs; i++) {
    os << std::string_view{static_cast<const char *>(ioVecs[i].iov_base),
                           ioVecs[i].iov_len};
  }

  return os.str();
}

TEST(AsciiSerializedRequest, largestPossibleMetaCommandsGetRequest) {
  McMetaCommandsGetRequest req("mykey");
  uint64_t allBooleanFlags =
      MC_META_COMMANDS_FLAG_BASE64_ENCODED_KEY |
      MC_META_COMMANDS_FLAG_RETURN_CAS_TOKEN |
      MC_META_COMMANDS_FLAG_RETURN_CLIENT_FLAGS |
      MC_META_COMMANDS_FLAG_RETURN_WAS_HIT | MC_META_COMMANDS_FLAG_RETURN_KEY |
      MC_META_COMMANDS_FLAG_RETURN_TIME_SINCE_LAST_ACCESS |
      MC_META_COMMANDS_FLAG_USE_NOREPLY_SEMANTICS |
      MC_META_COMMANDS_FLAG_RETURN_ITEM_SIZE |
      MC_META_COMMANDS_FLAG_RETURN_ITEM_TTL |
      MC_META_COMMANDS_FLAG_DO_NOT_BUMP_LRU |
      MC_META_COMMANDS_FLAG_RETURN_VALUE;
  req.flags_ref() = allBooleanFlags;
  req.casToken_ref() = std::numeric_limits<unsigned long>::max();
  req.newCasToken_ref() = std::numeric_limits<unsigned long>::max();
  req.vivifyOnMissTTL_ref() = std::numeric_limits<int>::max();
  req.refreshIfTTLLessThan_ref() = std::numeric_limits<int>::max();
  req.newTTL_ref() = std::numeric_limits<int>::max();
  req.opaqueToken_ref() = "401b30e3b8b5d629635a5c613cdb79";

  EXPECT_EQ(getSerialized(std::move(req)),
            "mg mykey b c f h k l q s t u v C18446744073709551615 "
            "E18446744073709551615 N2147483647 R2147483647 T2147483647 "
            "O401b30e3b8b5d629635a5c613cdb79\r\n");
}

TEST(AsciiSerializedReply, largestPossibleMetaCommandsGetReply) {
  McMetaCommandsGetReply res(carbon::Result::FOUND);
  res.value_ref() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "hello");
  res.flags_ref() = MC_META_COMMANDS_FLAG_BASE64_ENCODED_KEY |
                    MC_META_COMMANDS_FLAG_WON_RECACHE |
                    MC_META_COMMANDS_FLAG_ITEM_IS_STALE;
  res.casToken_ref() = std::numeric_limits<unsigned long>::max();
  res.opaqueToken_ref() = "401b30e3b8b5d629635a5c613cdb79";
  res.clientFlags_ref() = std::numeric_limits<unsigned long>::max();
  res.remainingTTL_ref() = std::numeric_limits<int>::max();
  res.lastAccessTime_ref() = std::numeric_limits<int>::max();
  res.wasHitBefore_ref() = 1;
  res.itemSize_ref() = std::numeric_limits<unsigned long>::max();
  res.key_ref() = carbon::Keys<folly::IOBuf>(folly::IOBuf(folly::IOBuf::COPY_BUFFER, "mykey"));

  EXPECT_EQ(getSerialized(std::move(res), {}),
            "VA 5 b W X c18446744073709551615 f18446744073709551615 "
            "s18446744073709551615 t2147483647 h1 l2147483647 "
            "O401b30e3b8b5d629635a5c613cdb79 kmykey\r\nhello\r\n");
}
} // namespace
