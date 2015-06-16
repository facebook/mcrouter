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

#include <vector>

#include <folly/Format.h>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/BigValueRouteIf.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
class IOBuf;
}

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * For get-like request:
 * 1. Perform get-like request on child route handle.
 * 2. If the received reply is a reply for big get request, generate chunk
 * getlike requests and forward to child route handle.
 * Merge all the replies and return it.
 * 3. Else return the reply.
 *
 * For update-like request:
 * 1. If value size is below or equal to threshold option,
 * route request to child route handle and return reply
 * 2. If value size is greater than threshold option,
 * generate chunk requests from original request and
 * send them to child route handle. If all of the chunk
 * update is successful, route request with original key and modified value
 * to child route handle and return reply. Else, return worse of the
 * replies for chunk updates
 *
 * Default behavior for other type of operations
 */
class BigValueRoute {
 public:
  static std::string routeName() { return "big-value"; }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const;

  BigValueRoute(McrouterRouteHandlePtr ch,
                BigValueRouteOptions options);

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    typename GetLike<Operation>::Type = 0) const;

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    typename UpdateLike<Operation>::Type = 0) const;

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    OtherThanT(Operation, GetLike<>, UpdateLike<>) = 0) const;

 private:
  const McrouterRouteHandlePtr ch_;
  const BigValueRouteOptions options_;

  class ChunksInfo {
   public:
    explicit ChunksInfo(folly::StringPiece reply_value);
    explicit ChunksInfo(uint32_t num_chunks);

    folly::IOBuf toStringType() const;
    uint32_t numChunks() const;
    uint32_t randSuffix() const;
    bool valid() const;

   private:
    const uint32_t infoVersion_;
    uint32_t numChunks_;
    uint32_t randSuffix_;
    bool valid_;
  };

  typedef McOperation<mc_op_get> ChunkGetOP;
  typedef McOperation<mc_op_set> ChunkUpdateOP;

  template <class Reply>
  std::vector<Reply> collectAllByBatches(
    std::vector<std::function<Reply()>>& fs) const;

  template <class Operation, class Request>
  std::pair<std::vector<Request>, ChunksInfo>
  chunkUpdateRequests(const Request& req, Operation) const;

  template <class Operation, class Request>
  std::vector<Request> chunkGetRequests(const Request& req,
                                        const ChunksInfo& info,
                                        Operation) const;

  template <typename InputIterator, class Reply>
  Reply mergeChunkGetReplies(
      InputIterator begin, InputIterator end, Reply&& init_reply) const;

  folly::IOBuf createChunkKey(
    folly::StringPiece key, uint32_t index, uint64_t suffix) const;
};

}}} // facebook::memcache::mcrouter

#include "BigValueRoute-inl.h"
