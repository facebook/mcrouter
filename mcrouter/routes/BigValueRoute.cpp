/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "BigValueRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

BigValueRoute::ChunksInfo::ChunksInfo(
    folly::StringPiece reply_value)
  : infoVersion_(1),
    valid_(true) {
  // Verify that reply_value is of the form version-numchunks-randSuffix,
  // where version, numchunks and randsuffix should be numeric
  uint32_t version;
  int chars_read;
  valid_ &= (sscanf(reply_value.data(), "%u-%u-%u%n",
        &version, &numChunks_, &randSuffix_, &chars_read) == 3);
  valid_ &= (static_cast<size_t>(chars_read) == reply_value.size());
  valid_ &= (version == infoVersion_);
}

BigValueRoute::ChunksInfo::ChunksInfo(uint32_t num_chunks)
  : infoVersion_(1),
    numChunks_(num_chunks),
    randSuffix_(rand()),
    valid_(true) {}

folly::IOBuf BigValueRoute::ChunksInfo::toStringType() const {
  return folly::IOBuf(
    folly::IOBuf::COPY_BUFFER,
    folly::sformat("{}-{}-{}", infoVersion_, numChunks_, randSuffix_)
  );
}

uint32_t BigValueRoute::ChunksInfo::numChunks() const {
  return numChunks_;
}

uint32_t BigValueRoute::ChunksInfo::randSuffix() const {
  return randSuffix_;
}

bool BigValueRoute::ChunksInfo::valid() const {
  return valid_;
}

BigValueRoute::BigValueRoute(McrouterRouteHandlePtr ch,
                             BigValueRouteOptions options)
    : ch_(std::move(ch)), options_(options) {

  assert(ch_ != nullptr);
}

folly::IOBuf BigValueRoute::createChunkKey(
    folly::StringPiece base_key,
    uint32_t chunk_index,
    uint64_t rand_suffix) const {

  return folly::IOBuf(
    folly::IOBuf::COPY_BUFFER,
    folly::sformat("{}:{}:{}", base_key, chunk_index, rand_suffix)
  );
}

McrouterRouteHandlePtr makeBigValueRoute(McrouterRouteHandlePtr rh,
                                         BigValueRouteOptions options) {
  return std::make_shared<McrouterRouteHandle<BigValueRoute>>(
    std::move(rh),
    std::move(options));
}

}}}
