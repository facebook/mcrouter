/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/fbi/cpp/LogFailure.h"

namespace facebook { namespace memcache {

namespace {
#ifndef LIBMC_FBTRACE_DISABLE

template <class Request>
typename std::enable_if<RequestHasFbTraceInfo<Request>::value,
                        const mc_fbtrace_info_s*>::type
inline getFbTraceInfo(const Request& request) {
  return request.fbtraceInfo();
}

template <class Request>
typename std::enable_if<!RequestHasFbTraceInfo<Request>::value,
                        const mc_fbtrace_info_s*>::type
inline getFbTraceInfo(const Request& request) {
  return nullptr;
}

#endif
}

template <class Reply>
bool McClientRequestContextBase::reply(Reply&& r) {
  if (replyType_ != typeid(Reply)) {
    failure::log("AsyncMcClient", failure::Category::kBrokenLogic,
                 "Attempt to forward a reply of a wrong type. Expected '{}', "
                 "but received '{}'!", replyType_.name(), typeid(Reply).name());

    return false;
  }

  auto* storage = reinterpret_cast<folly::Optional<Reply>*>(replyStorage_);
  assert(!storage->hasValue());
  storage->emplace(std::move(r));
  forwardReply();
  return true;
}

template <class Operation, class Request, class F, class... Args>
McClientRequestContextBase::UniquePtr McClientRequestContextBase::createAsync(
  Operation, const Request& request, F&& f, Args&&... args) {

  using CallbackType = typename std::remove_reference<F>::type;
  using Type = McClientRequestContextAsync<Operation, Request, CallbackType>;
  return McClientRequestContextBase::UniquePtr(
    new Type(Operation(), request, std::forward<Args>(args)...,
             std::forward<F>(f)),
    McClientRequestContextBase::Deleter());
}

template <class Operation, class Request>
McClientRequestContextBase::McClientRequestContextBase(
  Operation, const Request& request, uint64_t reqid, mc_protocol_t protocol,
  std::shared_ptr<AsyncMcClientImpl> client, bool issync,
  folly::Optional<typename ReplyType<Operation, Request>::type>& replyStorage)
  : reqContext(request, Operation(), reqid, protocol),
    id(reqid),
    client_(std::move(client)),
    replyType_(typeid(typename ReplyType<Operation, Request>::type)),
    replyStorage_(reinterpret_cast<void*>(&replyStorage)),
    isSync_(issync) {
}

template <class Operation, class Request>
void McClientRequestContextCommon<Operation, Request>::replyError(
    mc_res_t result) {
  assert(!replyStorage_.hasValue());
  replyStorage_.emplace(result);
  forwardReply();
}

template <class Operation, class Request>
const char*
McClientRequestContextCommon<Operation, Request>::fakeReply() const {
  return "CLIENT_ERROR unsupported operation\r\n";
}

template <class Operation, class Request>
typename McClientRequestContextCommon<Operation, Request>::Reply
McClientRequestContextCommon<Operation, Request>::getReply() {
  assert(replyStorage_.hasValue());
  return std::move(replyStorage_.value());
}

template <class Operation, class Request>
McClientRequestContextCommon<Operation, Request>::McClientRequestContextCommon(
  Operation, const Request& request, uint64_t reqid, mc_protocol_t protocol,
  std::shared_ptr<AsyncMcClientImpl> client, bool issync)
  : McClientRequestContextBase(Operation(), request, reqid, protocol,
                               std::move(client), issync, replyStorage_)
#ifndef LIBMC_FBTRACE_DISABLE
    , fbtraceInfo_(getFbTraceInfo(request))
#endif
{
}

template <class Operation, class Request>
void McClientRequestContextCommon<Operation, Request>::sendTraceOnReply() {
#ifndef LIBMC_FBTRACE_DISABLE
  fbTraceOnReceive(Operation(), fbtraceInfo_, replyStorage_.value());
#endif
}

template <class Operation, class Request>
void McClientRequestContextSync<Operation, Request>::wait(
    std::chrono::milliseconds timeout) {
  if (timeout.count()) {
    baton_.timed_wait(timeout);
  } else {
    baton_.wait();
  }
}

template <class Operation, class Request>
void McClientRequestContextSync<Operation, Request>::cancelAndWait() {
  this->state = ReqState::CANCELED;
  baton_.reset();
  baton_.wait();
}

template <class Operation, class Request>
void McClientRequestContextSync<Operation, Request>::canceled() {
  baton_.post();
}

template <class Operation, class Request, class F>
void McClientRequestContextAsync<Operation, Request, F>::canceled() {
  throw std::logic_error("canceled() should never be called for async mode");
}

template <class Operation, class Request>
void McClientRequestContextSync<Operation, Request>::forwardReply() {
  this->sendTraceOnReply();
  this->state = ReqState::COMPLETE;
  baton_.post();
}

template <class Operation, class Request, class F>
void McClientRequestContextAsync<Operation, Request, F>::forwardReply() {
  this->sendTraceOnReply();
  f_(this->getReply());
}

}}  // facebook::memcache
