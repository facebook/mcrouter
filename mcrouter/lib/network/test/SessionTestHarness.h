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

#include <deque>

#include <folly/io/async/EventBase.h>
#include <folly/Range.h>
#include <folly/io/async/AsyncTransport.h>

#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/McServerRequestContext.h"

namespace facebook { namespace memcache {

class McServerSession;
class MockAsyncSocket;

class SessionTestHarness {
 public:
  /**
   * Create a SessionTestHarness
   *
   * @param opts                Options to use while creating McServerSession
   * @param onTerminated        The callback to be invoked when session is
   *                            closed.
   * @param onWriteQuiescence   The callback to be invoked when all pending
   *                            writes are flushed out.
   *
   * NOTE: Look at McServerSession.h for info about the above callbacks
   */
  explicit SessionTestHarness(
      AsyncMcServerWorkerOptions opts = AsyncMcServerWorkerOptions(),
      std::function<void(McServerSession&)> onWriteQuiescence = nullptr,
      std::function<void(McServerSession&)> onTerminated = nullptr);

  /**
   * Input packets in order into the socket.
   *
   * We're guaranteed to call readDataAvailable(...) at least once
   * per packet, starting at each packet boundary.
   */
  template <typename... Args>
  void inputPackets(folly::StringPiece p, Args&&... args) {
    inputPacket(p);
    inputPackets(std::forward<Args>(args)...);
  }

  /**
   * Get the current list of writes on the socket.
   *
   * A write is a result of TAsyncTransport::write*().
   *
   * This is stateful: a single write will only be returned by
   * this method once.
   */
  std::vector<std::string> flushWrites() {
    eventBase_.loopOnce();
    auto output = output_;
    output_.clear();
    return output;
  }

  /**
   * Stop replying to incoming requests immediately
   */
  void pause() {
    allowed_ = 0;
  }

  /**
   * Resume replying to all accumulated and new requests immediately
   */
  void resume() {
    allowed_ = -1;
    fulfillTransactions();
    flushSavedInputs();
  }

  /**
   * Reply to n accumulated or new requests; then pause again.
   * This is cumulative, resume(2); resume(2) is the same as resume(4);
   */
  void resume(size_t n) {
    if (allowed_ != -1) {
      allowed_ += n;
    }
    fulfillTransactions();
    flushSavedInputs();
  }

  /**
   * Initiate session close
   */
  void closeSession() {
    session_.close();
  }

  /**
   * Returns the list of currently accumulated paused requests' keys.
   */
  std::vector<std::string> pausedKeys() {
    std::vector<std::string> keys;
    for (auto& t : transactions_) {
      keys.push_back(t.req.fullKey().str());
    }
    return keys;
  }

 private:
  folly::EventBase eventBase_;
  McServerSession& session_;
  std::deque<std::string> savedInputs_;
  std::vector<std::string> output_;
  folly::AsyncTransportWrapper::ReadCallback* read_;

  /* Paused state. -1 means reply to everything; >= 0 means
     reply only to that many requests */
  int allowed_{-1};
  struct Transaction {
    McServerRequestContext ctx;
    McRequest req;
    McReply reply;
    Transaction(McServerRequestContext&& c, McRequest&& r, McReply p)
        : ctx(std::move(c)), req(std::move(r)), reply(std::move(p)) {
    }
  };
  std::deque<Transaction> transactions_;

  void inputPackets() {}
  void flushSavedInputs();
  void inputPacket(folly::StringPiece p);

  /* MockAsyncSocket interface */
  void write(folly::StringPiece out) {
    output_.push_back(out.str());
  }

  void setReadCallback(
      folly::AsyncTransportWrapper::ReadCallback* read) {
    read_ = read;
  }

  folly::AsyncTransportWrapper::ReadCallback* getReadCallback() {
    return read_;
  }

  void fulfillTransactions() {
    while (!transactions_.empty() && (allowed_ == -1 || allowed_ > 0)) {
      auto& t = transactions_.front();
      McServerRequestContext::reply(std::move(t.ctx), std::move(t.reply));
      transactions_.pop_front();
      if (allowed_ != -1) {
        --allowed_;
      }
    }

    /* flush writes on the socket */
    eventBase_.loopOnce();
  }

  template <class Operation>
  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 Operation) {
    auto reply = makeReply(req, Operation());
    transactions_.emplace_back(std::move(ctx), std::move(req),
                               std::move(reply));
    fulfillTransactions();
  }

  template <class Operation>
  McReply makeReply(const McRequest& req, Operation) {
    return McReply(DefaultReply, Operation());
  }

  McReply makeReply(const McRequest& req, McOperation<mc_op_get>) {
    return McReply(mc_res_found,
                   req.fullKey().str() + "_value");
  }

  class OnRequest {
   public:
    explicit OnRequest(SessionTestHarness& harness) :
        harness_(harness) {}

    template <class Operation>
    void onRequest(McServerRequestContext&& ctx,
                   McRequest&& req,
                   Operation) {
      harness_.onRequest(std::move(ctx), std::move(req), Operation());
    }

   private:
    SessionTestHarness& harness_;
  };

  friend class MockAsyncSocket;
};

}}  // facebook::memcache
