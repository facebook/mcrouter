/*
 *  Copyright (c) 2017, Facebook, Inc.
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
  // Verify that replyValue is of the form version-numChunks-randSuffix,
  // where version, numChunks and randSuffix should be numeric
  uint32_t version;
  int charsRead;
  valid_ &=
      (sscanf(
           replyValue.data(),
           "%u-%u-%u%n",
           &version,
           &numChunks_,
           &randSuffix_,
           &charsRead) == 3);
  valid_ &= (static_cast<size_t>(charsRead) == replyValue.size());
  valid_ &= (version == infoVersion_);
}

BigValueRoute::ChunksInfo::ChunksInfo(uint32_t chunks)
    : infoVersion_(1), numChunks_(chunks), randSuffix_(rand()), valid_(true) {}

folly::IOBuf BigValueRoute::ChunksInfo::toStringType() const {
  return folly::IOBuf(
      folly::IOBuf::COPY_BUFFER,
      folly::sformat("{}-{}-{}", infoVersion_, numChunks_, randSuffix_));
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

BigValueRoute::BigValueRoute(
    std::shared_ptr<MemcacheRouteHandleIf> ch,
    BigValueRouteOptions options)
    : ch_(std::move(ch)), options_(options) {
  assert(ch_ != nullptr);
}

folly::IOBuf BigValueRoute::createChunkKey(
    folly::StringPiece baseKey,
    uint32_t chunkIndex,
    uint64_t randSuffix) const {
  return folly::IOBuf(
      folly::IOBuf::COPY_BUFFER,
      folly::sformat("{}:{}:{}", baseKey, chunkIndex, randSuffix));
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
