/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/IOBuf.h>
#include <folly/experimental/fibers/FiberManager.h>
#include <folly/experimental/fibers/WhenN.h>

#include "mcrouter/lib/IOBufUtil.h"

namespace facebook { namespace memcache { namespace mcrouter {

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

template <class Operation, class Request>
void BigValueRoute::traverse(
    const Request& req, Operation,
    const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
  t(*ch_, req, Operation());
}

template <class Operation, class Request>
typename ReplyType<Operation, Request>::type BigValueRoute::route(
    const Request& req, Operation,
    typename GetLike<Operation>::Type) const {
  auto initialReply = ch_->route(req, Operation());
  if (!initialReply.isHit() ||
      !(initialReply.flags() & MC_MSG_FLAG_BIG_VALUE)) {
    return initialReply;
  }

  typedef typename ReplyType<Operation, Request>::type Reply;
  auto buf = initialReply.value().clone();
  ChunksInfo chunks_info(coalesceAndGetRange(buf));
  if (!chunks_info.valid()) {
    return Reply(DefaultReply, Operation());
  }

  auto reqs = chunkGetRequests(req, chunks_info, Operation());
  std::vector<std::function<Reply()>> fs;
  fs.reserve(reqs.size());

  auto& target = *ch_;
  for (const auto& req_b : reqs) {
    fs.push_back(
      [&target, &req_b]() {
        return target.route(req_b, ChunkGetOP());
      }
    );
  }

  auto replies = collectAllByBatches(fs);
  return mergeChunkGetReplies(
    replies.begin(), replies.end(), std::move(initialReply));
}

template <class Operation, class Request>
typename ReplyType<Operation, Request>::type BigValueRoute::route(
  const Request& req, Operation,
  typename UpdateLike<Operation>::Type) const {

  typedef typename ReplyType<Operation, Request>::type Reply;
  if (req.value().length() <= options_.threshold_) {
    return ch_->route(req, Operation());
  }

  auto reqs_info_pair = chunkUpdateRequests(req, Operation());
  std::vector<std::function<Reply()>> fs;
  fs.reserve(reqs_info_pair.first.size());

  auto& target = *ch_;
  for (const auto& req_b : reqs_info_pair.first) {
    fs.push_back(
      [&target, &req_b]() {
        return target.route(req_b, ChunkUpdateOP());
      }
    );
  }

  auto replies = collectAllByBatches(fs);

  // reply for all chunk update requests
  auto reducedReply = Reply::reduce(replies.begin(), replies.end());
  if (reducedReply->isStored()) {
    // original key with modified value stored at the back
    auto new_req = req.clone();
    new_req.setFlags(req.flags() | MC_MSG_FLAG_BIG_VALUE);
    new_req.setValue(reqs_info_pair.second.toStringType());
    return ch_->route(std::move(new_req), Operation());
  } else {
    return Reply(reducedReply->result());
  }
}

template <class Operation, class Request>
typename ReplyType<Operation, Request>::type BigValueRoute::route(
  const Request& req, Operation,
  OtherThanT(Operation, GetLike<>, UpdateLike<>)) const {

  return ch_->route(req, Operation());
}

template <class Operation, class Request>
std::pair<std::vector<Request>,
  typename BigValueRoute::ChunksInfo>
BigValueRoute::chunkUpdateRequests(const Request& req, Operation) const {
  int num_chunks =
    (req.value().length() + options_.threshold_ - 1) / options_.threshold_;
  ChunksInfo info(num_chunks);

  // Type for Request and ChunkUpdateRequest is same for now.
  std::vector<Request> big_set_reqs;
  big_set_reqs.reserve(num_chunks);

  auto base_key = req.fullKey();
  size_t i_pos = 0;
  for (int i = 0; i < num_chunks;  i++, i_pos += options_.threshold_) {
    // generate chunk_key and chunk_value
    folly::IOBuf chunk_value;
    req.value().cloneInto(chunk_value);
    chunk_value.trimStart(i_pos);
    chunk_value.trimEnd(chunk_value.length() -
                        std::min(options_.threshold_, chunk_value.length()));
    Request req_big(createChunkKey(base_key, i, info.randSuffix()));
    req_big.setValue(std::move(chunk_value));
    req_big.setExptime(req.exptime());
    big_set_reqs.push_back(std::move(req_big));
  }

  return std::make_pair(std::move(big_set_reqs), info);
}

template<class Operation, class Request>
std::vector<Request>
BigValueRoute::chunkGetRequests(const Request& req,
                                const ChunksInfo& info,
                                Operation) const {
  // Type for Request and ChunkGetRequest is same for now.
  std::vector<Request> big_get_reqs;
  big_get_reqs.reserve(info.numChunks());

  auto base_key = req.fullKey();
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
  Reply&& initial_reply) const {

  auto reduced_reply_it = Reply::reduce(begin, end);
  if (!reduced_reply_it->isHit()) {
      return Reply(reduced_reply_it->result());
  }

  std::vector<std::unique_ptr<folly::IOBuf>> data_vec;
  while (begin != end) {
    data_vec.push_back(begin->value().clone());
    ++begin;
  }

  initial_reply.setValue(concatAll(data_vec.begin(), data_vec.end()));
  initial_reply.setResult(reduced_reply_it->result());
  return std::move(initial_reply);
}

}}}  // facebook::memcache::mcrouter
