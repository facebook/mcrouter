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

#include <string>
#include <utility>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/McRequestList.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

template <class OnRequest, class RequestList>
class McServerOnRequestWrapper;
class McServerSession;
class MultiOpParent;
template <class ThriftType>
class TypedThriftReply;

/**
 * API for users of McServer to send back a reply for a request.
 *
 * Each onRequest callback is provided a context object,
 * which must eventually be surrendered back via a reply() call.
 */
class McServerRequestContext {
 public:
  using DestructorFunc = void (*)(void*);
  /**
   * Notify the server that the request-reply exchange is complete.
   *
   * @param ctx    The original context.
   * @param reply  Reply to hand off.
   */
  static void reply(McServerRequestContext&& ctx, McReply&& reply);
  /**
   * Notify the server that the request-reply exchange is complete.
   *
   * @param ctx         The original context.
   * @param reply       Reply to hand off.
   * @param destructor  Callback to destruct toDestruct, called when all
   *                    data is written to network and is not needed
   *                    anymore, either on success or error. Must not
   *                    be null if toDestruct is not null
   * @param toDestruct  Target to destruct, destructed by destructor callback
   */
  static void reply(
      McServerRequestContext&& ctx,
      McReply&& reply,
      DestructorFunc destructor,
      void* toDestruct);

  template <class Reply>
  static void reply(McServerRequestContext&& ctx, Reply&& reply);

  template <class Reply>
  static void reply(
      McServerRequestContext&& ctx,
      Reply&& reply,
      DestructorFunc destructor,
      void* toDestruct);

  ~McServerRequestContext();

  McServerRequestContext(McServerRequestContext&& other) noexcept;
  McServerRequestContext& operator=(McServerRequestContext&& other);

  /**
   * Get the associated McServerSession
   */
  McServerSession& session();

 private:
  McServerSession* session_;

  /* Pack these together, operation + flags takes one word */
  mc_op_t operation_;
  bool noReply_;
  bool replied_{false};

  uint64_t reqid_;
  struct AsciiState {
    std::shared_ptr<MultiOpParent> parent_;
    folly::Optional<folly::IOBuf> key_;
  };
  std::unique_ptr<AsciiState> asciiState_;

  bool noReply(mc_res_t result) const;

  static void replyImpl(
      McServerRequestContext&& ctx,
      McReply&& reply,
      DestructorFunc destructor = nullptr,
      void* toDestruct = nullptr);

  template <class Reply>
  static void replyImpl(
      McServerRequestContext&& ctx,
      Reply&& reply,
      DestructorFunc destructor = nullptr,
      void* toDestruct = nullptr);

  folly::Optional<folly::IOBuf>& asciiKey() {
    if (!asciiState_) {
      asciiState_ = folly::make_unique<AsciiState>();
    }
    return asciiState_->key_;
  }
  bool hasParent() const {
    return asciiState_ && asciiState_->parent_;
  }
  MultiOpParent& parent() const {
    assert(hasParent());
    return *asciiState_->parent_;
  }
  /**
   * If reply is error, multi-op parent may inform this context that it will
   * assume responsibility for reporting the error. If so, this context should
   * not call McServerSession::reply. Returns true iff parent assumes
   * responsibility for reporting error. If true is returned, errorMessage is
   * moved to parent.
   */
  bool moveReplyToParent(mc_res_t result,
                         uint32_t errorCode,
                         std::string&& errorMessage) const;

  McServerRequestContext(const McServerRequestContext&) = delete;
  const McServerRequestContext& operator=(const McServerRequestContext&)
    = delete;

  /* Only McServerSession can create these */
  friend class McServerSession;
  friend class MultiOpParent;
  friend class WriteBuffer;
  McServerRequestContext(McServerSession& s, mc_op_t op, uint64_t r,
                         bool nr = false,
                         std::shared_ptr<MultiOpParent> parent = nullptr);
};

/**
 * McServerOnRequest is a polymorphic base class used as a callback
 * by AsyncMcServerWorker and McAsciiParser to hand off a request
 * (TypedThriftRequest<...>) to McrouterClient.
 *
 * The complexity in the implementation below is due to the fact that we
 * effectively need templated virtual member functions (which do not really
 * exist in C++).
 */
template <class RequestList>
class McServerOnRequestIf;

/**
 * OnRequest callback interface. This is an implementation detail.
 */
template <class Request>
class McServerOnRequestIf<List<Request>> {
 public:
  virtual void caretRequestReady(const UmbrellaMessageInfo& headerInfo,
                                 const folly::IOBuf& reqBody,
                                 McServerRequestContext&& ctx) = 0;

  virtual void requestReady(McServerRequestContext&& ctx, Request&& req) = 0;

  virtual ~McServerOnRequestIf() = default;
};

template <class Request, class... Requests>
class McServerOnRequestIf<List<Request, Requests...>> :
      public McServerOnRequestIf<List<Requests...>> {

 public:
  using McServerOnRequestIf<List<Requests...>>::requestReady;

  virtual void requestReady(McServerRequestContext&& ctx, Request&& req) = 0;

  virtual ~McServerOnRequestIf() = default;
};

class McServerOnRequest : public McServerOnRequestIf<ThriftRequestList> {
};

/**
 * Helper class to wrap user-defined callbacks in a correct virtual interface.
 * This is needed since we're mixing templates and virtual functions.
 */
template <class OnRequest, class RequestList = ThriftRequestList>
class McServerOnRequestWrapper;

template <class OnRequest, class Request>
class McServerOnRequestWrapper<OnRequest, List<Request>>
    : public McServerOnRequest {
 public:
  using McServerOnRequest::requestReady;

  template <class... Args>
  explicit McServerOnRequestWrapper(Args&&... args)
      : onRequest_(std::forward<Args>(args)...) {
  }

  void requestReady(McServerRequestContext&& ctx, Request&& req) override final {
    this->onRequest_.onRequest(std::move(ctx), std::move(req));
  }

  void caretRequestReady(const UmbrellaMessageInfo& headerInfo,
                         const folly::IOBuf& reqBody,
                         McServerRequestContext&& ctx) override final;

  void dispatchTypedRequestIfDefined(const UmbrellaMessageInfo& headerInfo,
                                     const folly::IOBuf& reqBody,
                                     McServerRequestContext&& ctx,
                                     std::true_type) {
    onRequest_.dispatchTypedRequest(headerInfo, reqBody, std::move(ctx));
  }

  void dispatchTypedRequestIfDefined(const UmbrellaMessageInfo&,
                                     const folly::IOBuf& reqBody,
                                     McServerRequestContext&& ctx,
                                     std::false_type) {
    McServerRequestContext::reply(std::move(ctx),
                                  McReply(mc_res_client_error));
  }

 protected:
  OnRequest onRequest_;
};

template <class OnRequest, class Request, class... Requests>
class McServerOnRequestWrapper<OnRequest, List<Request, Requests...>> :
      public McServerOnRequestWrapper<OnRequest, List<Requests...>> {

 public:
  using McServerOnRequestWrapper<OnRequest, List<Requests...>>::requestReady;

  template <class... Args>
  explicit McServerOnRequestWrapper(Args&&... args)
    : McServerOnRequestWrapper<OnRequest, List<Requests...>>(
        std::forward<Args>(args)...) {
  }

  void requestReady(McServerRequestContext&& ctx, Request&& req)
      override final {
    this->onRequest_.onRequest(std::move(ctx), std::move(req));
  }
};

}} // facebook::memcache

#include "McServerRequestContext-inl.h"
