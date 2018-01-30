/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "BigValueRoute.h"

#include <folly/Format.h>
#include <folly/fibers/WhenN.h>

#include "mcrouter/lib/AuxiliaryCPUThreadPool.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

McMetagetReply BigValueRoute::route(const McMetagetRequest& req) const {
  // TODO: Make metaget work with BigValueRoute. One way to make this work well
  // is to add 'flags' to McMetagetReply.
  return ch_->route(req);
}

McLeaseGetReply BigValueRoute::route(const McLeaseGetRequest& req) const {
  // Limit the number of recursive calls since we're on a fiber.
  return doLeaseGetRoute(req, 1 /* retriesLeft */);
}

McLeaseGetReply BigValueRoute::doLeaseGetRoute(
    const McLeaseGetRequest& req,
    size_t retriesLeft) const {
  auto initialReply = ch_->route(req);
  if (!(initialReply.flags() & MC_MSG_FLAG_BIG_VALUE) ||
      !isHitResult(initialReply.result())) {
    return initialReply;
  }

  ChunksInfo chunksInfo(coalesceAndGetRange(initialReply.value()));
  if (!chunksInfo.valid()) {
    // We cannot return mc_res_notfound without a valid lease token. We err on
    // the side of allowing clients to make progress by returning a lease token
    // of -1.
    McLeaseGetReply missReply(mc_res_notfound);
    missReply.leaseToken() = static_cast<uint64_t>(-1);
    return missReply;
  }

  // Send a gets request for the metadata while sending ordinary get requests
  // to fetch the subpieces. We may need to use the returned CAS token to
  // invalidate the metadata piece later on.
  const auto key = req.key().fullKey();
  McGetsRequest getsMetadataReq(key);
  const auto reqs = chunkGetRequests(req, chunksInfo);
  std::vector<std::function<McGetReply()>> fs;
  fs.reserve(reqs.size());

  auto& target = *ch_;
  for (const auto& chunkReq : reqs) {
    fs.push_back([&target, &chunkReq]() { return target.route(chunkReq); });
  }

  McGetsReply getsMetadataReply;
  std::vector<McGetReply> replies;
  std::vector<std::function<void()>> tasks;
  tasks.emplace_back([&getsMetadataReq, &getsMetadataReply, &target]() {
    getsMetadataReply = target.route(getsMetadataReq);
  });
  tasks.emplace_back([ this, &replies, fs = std::move(fs) ]() mutable {
    replies = collectAllByBatches(fs.begin(), fs.end());
  });

  folly::fibers::collectAll(tasks.begin(), tasks.end());
  const auto reducedReply = mergeChunkGetReplies(
      replies.begin(), replies.end(), std::move(initialReply));

  // Return reducedReply on hit or error
  if (!isMissResult(reducedReply.result())) {
    return reducedReply;
  }

  if (isErrorResult(getsMetadataReply.result())) {
    if (retriesLeft > 0) {
      return doLeaseGetRoute(req, --retriesLeft);
    }
    McLeaseGetReply errorReply(getsMetadataReply.result());
    errorReply.message() = std::move(getsMetadataReply.message());
    return errorReply;
  }

  // This is the tricky part with leases. There was a hit on the metadata,
  // but a miss/error on one of the subpieces. One of the clients needs to
  // invalidate the metadata. Then one (possibly the same) client will be able
  // to get a valid lease token.
  // TODO: Consider also firing off async deletes for the subpieces for better
  // cache use.
  if (isHitResult(getsMetadataReply.result())) {
    McCasRequest invalidateReq(key);
    invalidateReq.exptime() = -1;
    invalidateReq.casToken() = getsMetadataReply.casToken();
    auto invalidateReply = ch_->route(invalidateReq);
    if (isErrorResult(invalidateReply.result())) {
      McLeaseGetReply errorReply(invalidateReply.result());
      errorReply.message() = std::move(invalidateReply.message());
      return errorReply;
    }
  }

  if (retriesLeft > 0) {
    return doLeaseGetRoute(req, --retriesLeft);
  }

  McLeaseGetReply reply(mc_res_remote_error);
  reply.message() = folly::sformat(
      "BigValueRoute: exhausted retries for lease-get for key {}", key);
  return reply;
}

BigValueRoute::ChunksInfo::ChunksInfo(folly::StringPiece replyValue)
    : infoVersion_(1), valid_(true) {
  // Verify that replyValue is of the form version-numChunks-suffix,
  // where version, numChunks and suffix should be numeric
  uint32_t version;
  int charsRead;
  valid_ &=
      (sscanf(
           replyValue.data(),
           "%u-%u-%lu%n",
           &version,
           &numChunks_,
           &suffix_,
           &charsRead) == 3);
  valid_ &= (static_cast<size_t>(charsRead) == replyValue.size());
  valid_ &= (version == infoVersion_);
}

BigValueRoute::ChunksInfo::ChunksInfo(uint32_t chunks, uint64_t suffix__)
    : infoVersion_(1), numChunks_(chunks), suffix_(suffix__), valid_(true) {}

folly::IOBuf BigValueRoute::ChunksInfo::toStringType() const {
  return folly::IOBuf(
      folly::IOBuf::COPY_BUFFER,
      folly::sformat("{}-{}-{}", infoVersion_, numChunks_, suffix_));
}

uint32_t BigValueRoute::ChunksInfo::numChunks() const {
  return numChunks_;
}

uint64_t BigValueRoute::ChunksInfo::suffix() const {
  return suffix_;
}

bool BigValueRoute::ChunksInfo::valid() const {
  return valid_;
}

BigValueRoute::BigValueRoute(
    std::shared_ptr<MemcacheRouteHandleIf> ch,
    BigValueRouteOptions options)
    : ch_(std::move(ch)), options_(options) {
  assert(ch_ != nullptr);
}

folly::IOBuf BigValueRoute::createChunkKey(
    folly::StringPiece baseKey,
    uint32_t chunkIndex,
    uint64_t suffix) const {
  return folly::IOBuf(
      folly::IOBuf::COPY_BUFFER,
      folly::sformat("{}:{}:{}", baseKey, chunkIndex, suffix));
}

namespace {

// Hashes value on a separate CPU thread pool, preempts fiber until hashing is
// complete.
uint64_t hashBigValue(const folly::IOBuf& value) {
  if (auto singleton = AuxiliaryCPUThreadPoolSingleton::try_get_fast()) {
    auto& threadPool = singleton->getThreadPool();
    return folly::fibers::await([&](folly::fibers::Promise<uint64_t> promise) {
      threadPool.add([ promise = std::move(promise), &value ]() mutable {
        auto hash = folly::IOBufHash()(value);
        // Note: for compatibility with old code running in production we're
        // using only 32-bits of hash.
        promise.setValue(hash & ((1ull << 32) - 1));
      });
    });
  }
  throwRuntime(
      "Mcrouter CPU Thread pool is not running, cannot calculate hash for big "
      "value!");
}

} // anonymous

std::pair<std::vector<McSetRequest>, BigValueRoute::ChunksInfo>
BigValueRoute::chunkUpdateRequests(
    folly::StringPiece baseKey,
    const folly::IOBuf& value,
    int32_t exptime) const {
  int numChunks = (value.computeChainDataLength() + options_.threshold - 1) /
      options_.threshold;
  ChunksInfo info(numChunks, hashBigValue(value));

  std::vector<McSetRequest> bigSetReqs;
  bigSetReqs.reserve(numChunks);

  folly::IOBuf chunkValue;
  folly::io::Cursor cursor(&value);
  for (int i = 0; i < numChunks; ++i) {
    cursor.cloneAtMost(chunkValue, options_.threshold);
    bigSetReqs.emplace_back(createChunkKey(baseKey, i, info.suffix()));
    bigSetReqs.back().value() = std::move(chunkValue);
    bigSetReqs.back().exptime() = exptime;
  }

  return std::make_pair(std::move(bigSetReqs), info);
}

McrouterRouteHandlePtr makeBigValueRoute(
    McrouterRouteHandlePtr rh,
    BigValueRouteOptions options) {
  return std::make_shared<McrouterRouteHandle<BigValueRoute>>(
      std::move(rh), std::move(options));
}

} // mcrouter
} // memcache
} // facebook
