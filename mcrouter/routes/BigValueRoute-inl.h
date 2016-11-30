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

#include <folly/fibers/FiberManager.h>
#include <folly/fibers/WhenN.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/Range.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/Reply.h"

namespace facebook { namespace memcache { namespace mcrouter {

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

} // detail

template <class Reply>
std::vector<Reply> BigValueRoute::collectAllByBatches(
  std::vector<std::function<Reply()>>& fs) const {

  auto batchSize = options_.batchSize_;
  if (batchSize == 0) {
    batchSize = fs.size();
  }

  std::vector<Reply> allReplies;
  size_t b = 0;
  size_t e = std::min(fs.size(), batchSize);
  while (b < fs.size()) {
    auto replies = folly::fibers::collectAll(fs.begin() + b, fs.begin() + e);
    for (auto& r : replies) {
      allReplies.emplace_back(std::move(r));
    }
    b = e;
    e = std::min(fs.size(), e + batchSize);
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
ReplyT<Request> BigValueRoute::route(const Request& req, GetLikeT<Request>)
    const {
  auto initialReply = ch_->route(req);
  if (!isHitResult(initialReply.result()) ||
      !(initialReply.flags() & MC_MSG_FLAG_BIG_VALUE)) {
    return initialReply;
  }

  /* McMetagetReply does not have a value field, even though it is get-like. */
  auto* value = const_cast<folly::IOBuf*>(carbon::valuePtrUnsafe(initialReply));
  ChunksInfo chunksInfo(
      value ? coalesceAndGetRange(*value) : folly::StringPiece(""));
  if (!chunksInfo.valid()) {
    return createReply(DefaultReply, req);
  }

  auto reqs = chunkGetRequests<McGetRequest>(req, chunksInfo);
  std::vector<std::function<McGetReply()>> fs;
  fs.reserve(reqs.size());

  auto& target = *ch_;
  for (const auto& req_b : reqs) {
    fs.push_back(
      [&target, &req_b]() {
        return target.route(req_b);
      }
    );
  }

  auto replies = collectAllByBatches(fs);
  return mergeChunkGetReplies(
    replies.begin(), replies.end(), std::move(initialReply));
}

template <class Request>
ReplyT<Request> BigValueRoute::route(const Request& req, UpdateLikeT<Request>)
    const {
  if (req.value().computeChainDataLength() <= options_.threshold_) {
    return ch_->route(req);
  }

  auto reqs_info_pair = chunkUpdateRequests<McSetRequest>(req);
  std::vector<std::function<McSetReply()>> fs;
  fs.reserve(reqs_info_pair.first.size());

  auto& target = *ch_;
  for (const auto& req_b : reqs_info_pair.first) {
    fs.push_back(
      [&target, &req_b]() {
        return target.route(req_b);
      }
    );
  }

  auto replies = collectAllByBatches(fs);

  // reply for all chunk update requests
  auto reducedReply = detail::reduce(replies.begin(), replies.end());
  if (isStoredResult(reducedReply->result())) {
    // original key with modified value stored at the back
    auto new_req = req;
    new_req.flags() = req.flags() | MC_MSG_FLAG_BIG_VALUE;
    new_req.value() = reqs_info_pair.second.toStringType();
    return ch_->route(new_req);
  } else {
    return ReplyT<Request>(reducedReply->result());
  }
}

template <class Request>
ReplyT<Request> BigValueRoute::route(
    const Request& req,
    OtherThanT<Request, GetLike<>, UpdateLike<>>) const {
  return ch_->route(req);
}

template <class ToRequest, class FromRequest>
std::pair<std::vector<ToRequest>,
          typename BigValueRoute::ChunksInfo>
BigValueRoute::chunkUpdateRequests(const FromRequest& req) const {
  int num_chunks =
      (req.value().computeChainDataLength() + options_.threshold_ - 1) /
      options_.threshold_;
  ChunksInfo info(num_chunks);

  std::vector<ToRequest> big_set_reqs;
  big_set_reqs.reserve(num_chunks);

  auto base_key = req.key().fullKey();
  folly::IOBuf chunkValue;
  folly::io::Cursor cursor(&req.value());
  for (int i = 0; i < num_chunks; ++i) {
    cursor.cloneAtMost(chunkValue, options_.threshold_);
    big_set_reqs.emplace_back(createChunkKey(base_key, i, info.randSuffix()));
    big_set_reqs.back().value() = std::move(chunkValue);
    big_set_reqs.back().exptime() = req.exptime();
  }

  return std::make_pair(std::move(big_set_reqs), info);
}

template <class ToRequest, class FromRequest>
std::vector<ToRequest>
BigValueRoute::chunkGetRequests(const FromRequest& req,
                                const ChunksInfo& info) const {
  std::vector<ToRequest> big_get_reqs;
  big_get_reqs.reserve(info.numChunks());

  auto base_key = req.key().fullKey();
  for (uint32_t i = 0; i < info.numChunks(); i++) {
    // override key with chunk keys
    big_get_reqs.emplace_back(createChunkKey(base_key, i, info.randSuffix()));
  }

  return big_get_reqs;
}

template<typename InputIterator, class Reply>
Reply BigValueRoute::mergeChunkGetReplies(
  InputIterator begin,
  InputIterator end,
  Reply&& initialReply) const {

  auto reduced_reply_it = detail::reduce(begin, end);
  if (!isHitResult(reduced_reply_it->result())) {
    return Reply(reduced_reply_it->result());
  }

  std::vector<std::unique_ptr<folly::IOBuf>> data_vec;
  while (begin != end) {
    if (begin->value().hasValue()) {
      data_vec.push_back(begin->value()->clone());
    }
    ++begin;
  }

  if (auto* value =
          const_cast<folly::IOBuf*>(carbon::valuePtrUnsafe(initialReply))) {
    *value = concatAll(data_vec.begin(), data_vec.end());
  }
  initialReply.result() = reduced_reply_it->result();
  return initialReply;
}

}}}  // facebook::memcache::mcrouter
