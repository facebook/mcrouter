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

#include <typeindex>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/network/McSerializedRequest.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"

#include "mcrouter/lib/network/FBTrace.h"

namespace facebook { namespace memcache {

class AsyncMcClientImpl;

enum class ReqState {
  NONE,
  SEND_QUEUE,
  WRITE_QUEUE,
  PENDING_QUEUE,
  COMPLETE,
  CANCELED
};

/**
 * Class for storing per request data that is required for proper requests
 * processing inside of AsyncMcClient.
 */
class McClientRequestContextBase {
 public:
  McSerializedRequest reqContext;
  uint64_t id;
  ReqState state{ReqState::NONE};

  McClientRequestContextBase(const McClientRequestContextBase&) = delete;
  McClientRequestContextBase& operator=(const McClientRequestContextBase& other)
    = delete;
  McClientRequestContextBase(McClientRequestContextBase&&) = delete;
  McClientRequestContextBase& operator=(McClientRequestContextBase&& other)
    = delete;

  /**
   * Entry point for propagating reply to the user.
   *
   * Typechecks the reply and propagates it to the proper subclass.
   *
   * @return false iff the reply type didn't match with expected.
   */
  template <class Reply>
  bool reply(Reply&& r);

  /**
   * Propagate an error to the user.
   */
  virtual void replyError(mc_res_t result) = 0;

  /**
   * Returns fake data (specific to this request and operation) that can be used
   * to simulate a reply from network
   */
  virtual const char* fakeReply() const = 0;

  /**
   * Notify context that request was canceled in AsyncMcClientImpl
   */
  virtual void canceled() = 0;

 protected:
  class Deleter {
   public:
    void operator()(McClientRequestContextBase* ptr) const {
      // Sync requests are allocated on stack, thus we can't delete them.
      if (!ptr->isSync_) {
        delete ptr;
      }
    }
  };

  virtual ~McClientRequestContextBase() {}

  template <class Operation, class Request>
  McClientRequestContextBase(
    Operation, const Request& request, uint64_t reqid, mc_protocol_t protocol,
    std::shared_ptr<AsyncMcClientImpl> client, bool isSync,
    folly::Optional<typename ReplyType<Operation,Request>::type>& replyStorage);

  virtual void forwardReply() = 0;

 public:
  using UniquePtr = std::unique_ptr<McClientRequestContextBase, Deleter>;

  UniquePtr createDummyPtr() {
    assert(isSync_);
    return UniquePtr(this, Deleter());
  }

 private:
  std::shared_ptr<AsyncMcClientImpl> client_;
  std::type_index replyType_;
  UniqueIntrusiveListHook hook_;
  void* replyStorage_;
  bool isSync_{false};

 public:
  using Queue = UniqueIntrusiveList<McClientRequestContextBase,
                                    &McClientRequestContextBase::hook_,
                                    Deleter>;
};

template <class Operation, class Request>
class McClientRequestContext : public McClientRequestContextBase {
 public:
  using Reply = typename ReplyType<Operation, Request>::type;

  McClientRequestContext(
    Operation, const Request& request, uint64_t reqid, mc_protocol_t protocol,
    std::shared_ptr<AsyncMcClientImpl> client);

  void replyError(mc_res_t result) override;
  const char* fakeReply() const override;

  Reply getReply();

  void wait(std::chrono::milliseconds timeout);
  void cancelAndWait();
  void canceled() override;
  void forwardReply() override;
 private:
  Baton baton_;
  folly::Optional<Reply> replyStorage_;

#ifndef LIBMC_FBTRACE_DISABLE
  const mc_fbtrace_info_s* fbtraceInfo_;
#endif
};

}}  // facebook::memcache

#include "McClientRequestContext-inl.h"
