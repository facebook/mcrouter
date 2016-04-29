/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/network/McServerSession.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/lib/network/WriteBuffer.h"

namespace facebook { namespace memcache {

template <class Reply>
void McServerRequestContext::reply(
    McServerRequestContext&& ctx,
    Reply&& reply) {
  ctx.replied_ = true;

  // On error, multi-op parent may assume responsiblity of replying
  if (ctx.moveReplyToParent(
        reply.result(), reply.appSpecificErrorCode(),
        std::move(reply->message))) {
    replyImpl(std::move(ctx), Reply());
  } else {
    replyImpl(std::move(ctx), std::move(reply));
  }
}

template <class Reply>
void McServerRequestContext::reply(
    McServerRequestContext&& ctx,
    Reply&& reply,
    DestructorFunc destructor,
    void* toDestruct) {
  ctx.replied_ = true;

  // On error, multi-op parent may assume responsiblity of replying
  if (ctx.moveReplyToParent(
        reply.result(), reply.appSpecificErrorCode(),
        std::move(reply->message))) {
    replyImpl(std::move(ctx), Reply(), destructor, toDestruct);
  } else {
    replyImpl(std::move(ctx), std::move(reply), destructor, toDestruct);
  }
}

template <class Reply>
void McServerRequestContext::replyImpl(
    McServerRequestContext&& ctx,
    Reply&& reply,
    DestructorFunc destructor,
    void* toDestruct) {
  auto session = ctx.session_;
  if (toDestruct != nullptr) {
    assert(destructor != nullptr);
  }
  // Call destructor(toDestruct) on error, or pass ownership to write buffer
  std::unique_ptr<void, void (*)(void*)> destructorContainer(
      toDestruct, destructor);

  if (ctx.noReply(reply.result())) {
    session->reply(nullptr, ctx.reqid_);
    return;
  }

  session->ensureWriteBufs();

  uint64_t reqid = ctx.reqid_;
  auto wb = session->writeBufs_->get();
  if (!wb->prepareTyped(
          std::move(ctx),
          std::move(reply),
          std::move(destructorContainer))) {
    session->transport_->close();
    return;
  }
  session->reply(std::move(wb), reqid);
}

template <class T, class Enable = void>
struct HasDispatchTypedRequest {
  static constexpr std::false_type value{};
};

template <class T>
struct HasDispatchTypedRequest<
  T,
  typename std::enable_if<
    std::is_same<
      decltype(std::declval<T>().dispatchTypedRequest(
                 std::declval<UmbrellaMessageInfo>(),
                 std::declval<folly::IOBuf>(),
                 std::declval<McServerRequestContext>())),
      bool>::value>::type> {
  static constexpr std::true_type value{};
};

template <class OnRequest, class Request>
void McServerOnRequestWrapper<OnRequest, List<Request>>::caretRequestReady(
    const UmbrellaMessageInfo& headerInfo,
    const folly::IOBuf& reqBuf,
    McServerRequestContext&& ctx) {

  dispatchTypedRequestIfDefined(
    headerInfo, reqBuf, std::move(ctx),
    HasDispatchTypedRequest<OnRequest>::value);
}

}}  // facebook::memcache
