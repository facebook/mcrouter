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
} // folly

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

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const;

  BigValueRoute(McrouterRouteHandlePtr ch,
                BigValueRouteOptions options);

  template <class Request>
  ReplyT<Request> route(const Request& req, GetLikeT<Request> = 0) const;

  template <class Request>
  ReplyT<Request> route(const Request& req, UpdateLikeT<Request> = 0) const;

  template <class Request>
  ReplyT<Request> route(
    const Request& req,
    OtherThanT<Request, GetLike<>, UpdateLike<>> = 0) const;

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

  /**
   * Type of request to use when chunking get-like requests.
   */
  template <class Request>
  struct ChunkGet;
  template <class M>
  struct ChunkGet<TypedThriftRequest<M>> {
    using type = TypedThriftRequest<cpp2::McGetRequest>;
  };
  template <int op>
  struct ChunkGet<McRequestWithMcOp<op>> {
    using type = McRequestWithMcOp<mc_op_get>;
  };
  template <class Request>
  using ChunkGetT = typename ChunkGet<Request>::type;

  /**
   * Type of request to use when chunking update-like requests.
   */
  template <class Request>
  struct ChunkUpdate;
  template <class M>
  struct ChunkUpdate<TypedThriftRequest<M>> {
    using type = TypedThriftRequest<cpp2::McSetRequest>;
  };
  template <int op>
  struct ChunkUpdate<McRequestWithMcOp<op>> {
    using type = McRequestWithMcOp<mc_op_set>;
  };
  template <class Request>
  using ChunkUpdateT = typename ChunkUpdate<Request>::type;

  template <class Reply>
  std::vector<Reply> collectAllByBatches(
    std::vector<std::function<Reply()>>& fs) const;

  template <class ToRequest, class FromRequest>
  std::pair<std::vector<ToRequest>, ChunksInfo>
  chunkUpdateRequests(const FromRequest& req) const;

  template <class ToRequest, class FromRequest>
  std::vector<ToRequest> chunkGetRequests(const FromRequest& req,
                                          const ChunksInfo& info) const;

  template <typename InputIterator, class Reply>
  Reply mergeChunkGetReplies(
      InputIterator begin, InputIterator end, Reply&& init_reply) const;

  folly::IOBuf createChunkKey(
    folly::StringPiece key, uint32_t index, uint64_t suffix) const;
};

}}} // facebook::memcache::mcrouter

#include "BigValueRoute-inl.h"
