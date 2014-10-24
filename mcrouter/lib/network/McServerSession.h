/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <folly/IntrusiveList.h>
#include <folly/io/async/DelayedDestruction.h>
#include <thrift/lib/cpp/async/TAsyncTransport.h>

#include "mcrouter/lib/network/AsyncMcServerWorkerOptions.h"
#include "mcrouter/lib/network/McParser.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/WriteBuffer.h"

namespace facebook { namespace memcache {

class McServerOnRequest;

/**
 * A session owns a single transport, and processes the request/reply stream.
 */
class McServerSession :
      public folly::DelayedDestruction,
      private apache::thrift::async::TAsyncTransport::ReadCallback,
      private apache::thrift::async::TAsyncTransport::WriteCallback,
      private McParser::ServerParseCallback {
 private:
  folly::SafeIntrusiveListHook hook_;

 public:
  using Queue = folly::CountedIntrusiveList<McServerSession,
                                            &McServerSession::hook_>;

  /**
   * Creates a new session.  Sessions manage their own lifetime.
   * A session will self-destruct right after an onTerminated() callback
   * call, by which point all of the following must have occured:
   *   1) All outstanding requests have been replied and pending
   *      writes have been completed/errored out.
   *   2) The outgoing connection is closed, either via an explicit close()
   *      call or due to some error.
   * The application may use onTerminated() callback as the point in
   * time after which the session is considered done, and no event loop
   * iteration is necessary.
   * The session will be alive for the duration of onTerminated callback,
   * but this is the last time the application can safely assume that
   * the session is alive.
   *
   * @param transport  Connected transport; transfers ownership inside
   *                   this session.
   */
  static McServerSession& create(
    apache::thrift::async::TAsyncTransport::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    std::function<void(McServerSession&)> onWriteSuccess,
    std::function<void(McServerSession&)> onTerminated,
    std::function<void()> onShutdown,
    AsyncMcServerWorkerOptions options,
    void* userCtxt);

  /**
   * Eventually closes the transport. All pending writes will still be drained.
   * onTerminated() callback will only be called after the last write completes.
   */
  void close();

  /**
   * Returns true if there are some unfinished writes pending to the transport.
   */
  bool writesPending() const {
    return inFlight_ > 0;
  }

  /**
   * Allow clients to pause and resume reading form the sockets.
   * See pause(PauseReason) and resume(PauseReason) below.
   */
  void pause() {
    pause(PAUSE_USER);
  }
  void resume() {
    resume(PAUSE_USER);
  }

  /**
   * Get the user context associated with this session.
   */
  void* userContext() {
    return userCtxt_;
  }

 private:
  apache::thrift::async::TAsyncTransport::UniquePtr transport_;
  std::shared_ptr<McServerOnRequest> onRequest_;
  std::function<void(McServerSession&)> onWriteSuccess_;
  std::function<void(McServerSession&)> onTerminated_;
  std::function<void()> onShutdown_;
  AsyncMcServerWorkerOptions options_;
  void* userCtxt_{nullptr};

  enum State {
    STREAMING,
    CLOSING,
  };
  State state_{STREAMING};

  McParser parser_;

  /* In-order protocol state */

  /* headReqid_ <= tailReqid_.  Since we must output replies sequentially,
     headReqid_ tracks the last reply id we're allowed to sent out.
     Out of order replies are stalled in the blockedReplies_ queue. */
  uint64_t headReqid_{0}; /**< Id of next unblocked reply */
  uint64_t tailReqid_{0}; /**< Id to assign to next request */
  std::unordered_map<
    uint64_t,
    std::pair<McServerRequestContext, McReply>> blockedReplies_;

  /* If non-null, a multi-op operation is being parsed.*/
  std::shared_ptr<MultiOpParent> currentMultiop_;


  /* Batch writing state */

  /**
   * All writes to be written at the end of the loop in a single batch.
   */
  std::deque<std::pair<McServerRequestContext, McReply>> pendingWrites_;

  /**
   * Each entry contains the count of requests with replies already written
   * to the transport, waiting on write success.
   */
  std::deque<size_t> writeBatches_;

  /**
   * Queue of write buffers.
   */
  WriteBufferQueue writeBufs_;

  /**
   * True iff SendWritesCallback has been scheduled.
   */
  bool writeScheduled_{false};

  /**
   * Total number of alive McTransactions in the system.
   */
  size_t inFlight_{0};

  /**
   * Total number of alive McTransactions, excluding subrequests.
   * Used to make throttling decisions.
   * The intention is to count metagets as one request as far as
   * throttling is concerned.
   */
  size_t realRequestsInFlight_{0};

  struct SendWritesCallback : public folly::EventBase::LoopCallback {
    explicit SendWritesCallback(McServerSession& session) : session_(session) {}
    void runLoopCallback() noexcept override;
    McServerSession& session_;
  };

  SendWritesCallback sendWritesCallback_;

  /* OR-able bits of pauseState_ */
  enum PauseReason : uint64_t {
    PAUSE_THROTTLED = 1 << 0,
    PAUSE_WRITE = 1 << 1,
    PAUSE_USER = 1 << 2,
  };

  /* Reads are enabled iff pauseState_ == 0 */
  uint64_t pauseState_{0};

  /**
   * pause()/resume() reads from the socket (TODO: does not affect the already
   * read buffer - requests in it will still be processed).
   *
   * We stop reading from the socket if at least one reason has a pause()
   * without a corresponding resume().
   */
  void pause(PauseReason reason);
  void resume(PauseReason reason);

  /**
   * Flush pending writes to the transport.
   */
  void sendWrites();

  /**
   * Check if no outstanding transactions, and close socket and
   * call onTerminated() if so.
   */
  void checkClosed();

  void reply(McServerRequestContext&& ctx, McReply&& reply);

  /* TAsyncTransport's readCallback */
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override;
  void readDataAvailable(size_t len) noexcept override;
  void readEOF() noexcept override;
  void readError(const apache::thrift::transport::TTransportException& ex)
    noexcept override;

  /* McParser's parseCallback */
  void requestReady(McRequest&& req,
                    mc_op_t operation,
                    uint64_t reqid,
                    mc_res_t result,
                    bool noreply);
  void parseError(McReply reply);

  void queueWrite(McServerRequestContext&& ctx, McReply&& reply);

  void completeWrite();

  /* TAsyncTransport's writeCallback */
  void writeSuccess() noexcept override;
  void writeError(size_t bytesWritten,
                  const apache::thrift::transport::TTransportException& ex)
    noexcept override;

  void onTransactionStarted(bool isSubRequest);
  void onTransactionCompleted(bool isSubRequest);

  McServerSession(
    apache::thrift::async::TAsyncTransport::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    std::function<void(McServerSession&)> onWriteSuccess,
    std::function<void(McServerSession&)> onTerminated,
    std::function<void()> onShutdown,
    AsyncMcServerWorkerOptions options,
    void* userCtxt);

  McServerSession(const McServerSession&) = delete;
  McServerSession& operator=(const McServerSession&) = delete;

  friend class McServerRequestContext;
};


}}  // facebook::memcache
