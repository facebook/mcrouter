/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <iterator>
#include <type_traits>

#include <folly/Range.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/WhenN.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/AuxiliaryCPUThreadPool.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {
template <class InputIterator>
InputIterator reduce(InputIterator begin, InputIterator end) {
  if (begin == end) {
    return end;
  }
  InputIterator worstIt = begin;
  auto worstSeverity = resultSeverity(begin->result());

  for (++begin; begin != end; ++begin) {
    if (resultSeverity(begin->result()) > worstSeverity) {
      worstIt = begin;
      worstSeverity = resultSeverity(begin->result());
    }
  }
  return worstIt;
}

template <class Request>
struct ReducedUpdateType {
  using RequestType = McSetRequest;
  using ReplyType = McSetReply;
};

template <>
struct ReducedUpdateType<McAddRequest> {
  using RequestType = McAddRequest;
  using ReplyType = McAddReply;
};

// Hashes value on a separate CPU thread pool, preempts fiber until hashing is
// complete.
uint64_t hashBigValue(const folly::IOBuf& value);

} // namespace detail

template <class FuncIt>
std::vector<typename std::result_of<
    typename std::iterator_traits<FuncIt>::value_type()>::type>
BigValueRoute::collectAllByBatches(FuncIt beginF, FuncIt endF) const {
  using Reply = typename std::result_of<
      typename std::iterator_traits<FuncIt>::value_type()>::type;

  auto batchSize = options_.batchSize;
  const size_t rangeSize = std::distance(beginF, endF);
  if (batchSize == 0) {
    batchSize = rangeSize;
  }

  std::vector<Reply> allReplies;
  size_t b = 0;
  size_t e = std::min(rangeSize, batchSize);
  while (b < rangeSize) {
    auto replies = folly::fibers::collectAll(beginF + b, beginF + e);
    for (auto& r : replies) {
      allReplies.emplace_back(std::move(r));
    }
    b = e;
    e = std::min(rangeSize, e + batchSize);
  }
  return allReplies;
}

template <class Request>
void BigValueRoute::traverse(
    const Request& req,
    const RouteHandleTraverser<MemcacheRouteHandleIf>& t) const {
  t(*ch_, req);
}

template <class Request>
typename std::enable_if<
    folly::IsOneOf<Request, McGetRequest, McGetsRequest, McGatRequest, McGatsRequest>::value,
    ReplyT<Request>>::type
BigValueRoute::route(const Request& req) const {
  auto initialReply = ch_->route(req);
  if (!isHitResult(initialReply.result()) ||
      !(initialReply.flags() & MC_MSG_FLAG_BIG_VALUE)) {
    return initialReply;
  }

  ChunksInfo chunksInfo(coalesceAndGetRange(initialReply.value()));
  if (!chunksInfo.valid()) {
    return createReply(DefaultReply, req);
  }

  auto reqs = chunkGetRequests(req, chunksInfo);
  std::vector<std::function<McGetReply()>> fs;
  fs.reserve(reqs.size());

  auto& target = *ch_;
  for (const auto& chunkReq : reqs) {
    fs.push_back([&target, &chunkReq]() { return target.route(chunkReq); });
  }

  auto replies = collectAllByBatches(fs.begin(), fs.end());
  return mergeChunkGetReplies(
      replies.begin(), replies.end(), std::move(initialReply));
}

template <class Request>
ReplyT<Request> BigValueRoute::route(
    const Request& req,
    carbon::UpdateLikeT<Request>) const {
  if (req.value().computeChainDataLength() <= options_.threshold) {
    return ch_->route(req);
  }

  // Use 'McSet' for all update requests except for 'McAdd'
  using RequestType = typename detail::ReducedUpdateType<Request>::RequestType;
  using ReplyType = typename detail::ReducedUpdateType<Request>::ReplyType;

  auto reqsInfoPair = chunkUpdateRequests<RequestType>(
      req.key().fullKey(), req.value(), req.exptime());
  std::vector<std::function<ReplyType()>> fs;
  auto& chunkReqs = reqsInfoPair.first;
  fs.reserve(chunkReqs.size());

  auto& target = *ch_;
  for (const auto& chunkReq : chunkReqs) {
    fs.push_back([&target, &chunkReq]() { return target.route(chunkReq); });
  }

  auto replies = collectAllByBatches(fs.begin(), fs.end());

  // reply for all chunk update requests
  auto reducedReply = detail::reduce(replies.begin(), replies.end());
  if (isStoredResult(reducedReply->result())) {
    // original key with modified value stored at the back
    auto newReq = req;
    newReq.flags() = req.flags() | MC_MSG_FLAG_BIG_VALUE;
    newReq.value() = reqsInfoPair.second.toStringType();
    return ch_->route(newReq);
  } else {
    return ReplyT<Request>(reducedReply->result());
  }
}

template <class Request>
ReplyT<Request> BigValueRoute::route(
    const Request& req,
    carbon::OtherThanT<Request, carbon::GetLike<>, carbon::UpdateLike<>>)
    const {
  return ch_->route(req);
}

template <class FromRequest>
std::vector<McGetRequest> BigValueRoute::chunkGetRequests(
    const FromRequest& req,
    const ChunksInfo& info) const {
  std::vector<McGetRequest> bigGetReqs;
  bigGetReqs.reserve(info.numChunks());

  auto baseKey = req.key().fullKey();
  for (uint32_t i = 0; i < info.numChunks(); i++) {
    // override key with chunk keys
    bigGetReqs.emplace_back(createChunkKey(baseKey, i, info.suffix()));
  }

  return bigGetReqs;
}

template <typename InputIterator, class Reply>
Reply BigValueRoute::mergeChunkGetReplies(
    InputIterator begin,
    InputIterator end,
    Reply&& initialReply) const {
  auto reducedReplyIt = detail::reduce(begin, end);
  if (!isHitResult(reducedReplyIt->result())) {
    return Reply(reducedReplyIt->result());
  }

  initialReply.result() = reducedReplyIt->result();

  std::vector<std::unique_ptr<folly::IOBuf>> dataVec;
  while (begin != end) {
    if (begin->value().hasValue()) {
      dataVec.push_back(begin->value()->clone());
    }
    ++begin;
  }

  initialReply.value() = concatAll(dataVec.begin(), dataVec.end());
  return std::move(initialReply);
}

template <class Request>
std::pair<std::vector<Request>, BigValueRoute::ChunksInfo>
BigValueRoute::chunkUpdateRequests(
    folly::StringPiece baseKey,
    const folly::IOBuf& value,
    int32_t exptime) const {
  int numChunks = (value.computeChainDataLength() + options_.threshold - 1) /
      options_.threshold;
  ChunksInfo info(numChunks, detail::hashBigValue(value));

  std::vector<Request> chunkReqs;
  chunkReqs.reserve(numChunks);

  folly::IOBuf chunkValue;
  folly::io::Cursor cursor(&value);
  for (int i = 0; i < numChunks; ++i) {
    cursor.cloneAtMost(chunkValue, options_.threshold);
    chunkReqs.emplace_back(createChunkKey(baseKey, i, info.suffix()));
    chunkReqs.back().value() = std::move(chunkValue);
    chunkReqs.back().exptime() = exptime;
  }

  return std::make_pair(std::move(chunkReqs), info);
}

} // mcrouter
} // memcache
} // facebook
