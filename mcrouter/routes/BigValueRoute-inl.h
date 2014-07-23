/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "folly/io/IOBuf.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/fibers/WhenN.h"

namespace facebook { namespace memcache {

template <class RouteHandleIf>
BigValueRoute<RouteHandleIf>::ChunksInfo::ChunksInfo(
    folly::StringPiece reply_value)
  : infoVersion_(1),
    valid_(true) {
  // Verify that reply_value is of the form version-numchunks-randSuffix,
  // where version, numchunks and randsuffix should be numeric
  int version, chars_read;
  valid_ &= (sscanf(reply_value.data(), "%d-%u-%u%n",
        &version, &numChunks_, &randSuffix_, &chars_read) == 3);
  valid_ &= (chars_read == reply_value.size());
  valid_ &= (version == infoVersion_);
}

template <class RouteHandleIf>
BigValueRoute<RouteHandleIf>::ChunksInfo::ChunksInfo(uint32_t num_chunks)
  : infoVersion_(1),
    numChunks_(num_chunks),
    randSuffix_(rand()),
    valid_(true) {}

template <class RouteHandleIf>
folly::IOBuf
BigValueRoute<RouteHandleIf>::ChunksInfo::toStringType() const {
  return folly::IOBuf(
    folly::IOBuf::COPY_BUFFER,
    folly::format("{}-{}-{}", infoVersion_, numChunks_, randSuffix_).str()
  );
}

template <class RouteHandleIf>
uint32_t BigValueRoute<RouteHandleIf>::ChunksInfo::numChunks() const {
  return numChunks_;
}

template <class RouteHandleIf>
uint32_t BigValueRoute<RouteHandleIf>::ChunksInfo::randSuffix() const {
  return randSuffix_;
}

template <class RouteHandleIf>
bool BigValueRoute<RouteHandleIf>::ChunksInfo::valid() const {
  return valid_;
}

template <class RouteHandleIf>
template <class Operation, class Request>
std::vector<std::shared_ptr<RouteHandleIf>>
BigValueRoute<RouteHandleIf>::couldRouteTo(const Request& req,
                                           Operation) const {
  return {ch_};
}

template <class RouteHandleIf>
BigValueRoute<RouteHandleIf>::BigValueRoute(std::shared_ptr<RouteHandleIf> ch,
                                            BigValueRouteOptions options)
    : ch_(std::move(ch)), options_(options) {

  assert(ch_ != nullptr);
}

template <class RouteHandleIf>
template <class Operation, class Request>
typename ReplyType<Operation, Request>::type
BigValueRoute<RouteHandleIf>::route(const Request& req,
                                    Operation,
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
    return NullRoute<RouteHandleIf>::route(req, Operation());
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

  auto replies = fiber::whenAll(fs.begin(), fs.end());
  return mergeChunkGetReplies(
    replies.begin(), replies.end(), std::move(initialReply));
}

template <class RouteHandleIf>
template <class Operation, class Request>
typename ReplyType<Operation, Request>::type
BigValueRoute<RouteHandleIf>::route(
  const Request& req,
  Operation,
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

  auto replies = fiber::whenAll(fs.begin(), fs.end());

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

template <class RouteHandleIf>
template <class Operation, class Request>
typename ReplyType<Operation, Request>::type
BigValueRoute<RouteHandleIf>::route(
  const Request& req,
  Operation,
  OtherThanT(Operation, GetLike<>, UpdateLike<>)) const {

  return ch_->route(req, Operation());
}

template <class RouteHandleIf>
template <class Operation, class Request>
std::pair<std::vector<typename ChunkUpdateRequest<Request>::type>,
  typename BigValueRoute<RouteHandleIf>::ChunksInfo>
BigValueRoute<RouteHandleIf>::chunkUpdateRequests(const Request& req,
                                                  Operation) const {
  int num_chunks =
    (req.value().length() + options_.threshold_ - 1) / options_.threshold_;
  ChunksInfo info(num_chunks);

  // Type for Request and ChunkUpdateRequest is same for now.
  std::vector<typename ChunkUpdateRequest<Request>::type> big_set_reqs;
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
    auto req_big = createEmptyRequest(ChunkUpdateOP(), req);
    req_big.setKey(createChunkKey(base_key, i, info.randSuffix()));
    req_big.setValue(std::move(chunk_value));
    req_big.setExptime(req.exptime());
    big_set_reqs.push_back(std::move(req_big));
  }

  return std::make_pair(std::move(big_set_reqs), info);
}

template <class RouteHandleIf>
template<class Operation, class Request>
std::vector<typename ChunkGetRequest<Request>::type>
BigValueRoute<RouteHandleIf>::chunkGetRequests(const Request& req,
                                               const ChunksInfo& info,
                                               Operation) const {
  // Type for Request and ChunkGetRequest is same for now.
  std::vector<typename ChunkGetRequest<Request>::type> big_get_reqs;
  big_get_reqs.reserve(info.numChunks());

  auto base_key = req.fullKey();
  for (int i = 0; i < info.numChunks(); i++) {
    // override key with chunk keys
    auto req_big = createEmptyRequest(ChunkGetOP(), req);
    auto chunk_key = createChunkKey(base_key, i, info.randSuffix());
    req_big.setKey(std::move(chunk_key));
    big_get_reqs.push_back(std::move(req_big));
  }

  return big_get_reqs;
}

template <class RouteHandleIf>
template<typename InputIterator, class Reply>
Reply BigValueRoute<RouteHandleIf>::mergeChunkGetReplies(
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

template <class RouteHandleIf>
folly::IOBuf BigValueRoute<RouteHandleIf>::createChunkKey(
    folly::StringPiece base_key,
    uint32_t chunk_index,
    uint64_t rand_suffix) const {

  return folly::IOBuf(
    folly::IOBuf::COPY_BUFFER,
    folly::format("{}:{}:{}", base_key, chunk_index, rand_suffix).str()
  );
}

}}  // facebook::memcache
