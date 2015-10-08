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

#include <folly/IntrusiveList.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/network/AsyncMcServerWorkerOptions.h"
#include "mcrouter/lib/network/ServerMcParser.h"

namespace facebook { namespace memcache {

class Fifo;
class McServerOnRequest;
class MultiOpParent;
class WriteBuffer;
class WriteBufferQueue;

/**
 * A session owns a single transport, and processes the request/reply stream.
 */
class McServerSession :
      public folly::DelayedDestruction,
      private folly::AsyncTransportWrapper::ReadCallback,
      private folly::AsyncTransportWrapper::WriteCallback {
 private:
  folly::SafeIntrusiveListHook hook_;

 public:
  using Queue = folly::CountedIntrusiveList<McServerSession,
                                            &McServerSession::hook_>;

  /**
   * Creates a new session.  Sessions manage their own lifetime.
   * A session will self-destruct right after an onCloseFinish() callback
   * call, by which point all of the following must have occured:
   *   1) All outstanding requests have been replied and pending
   *      writes have been completed/errored out.
   *   2) The outgoing connection is closed, either via an explicit close()
   *      call or due to some error.
   *
   * onCloseStart() callback marks the beggining of session
   * teardown. The application can inititate any cleanup process. After
   * onCloseStart() the socket is no longer readable, and the application
   * should try to flush out all outstanding requests so that session
   * can be closed.
   *
   * The application may use onCloseFinish() callback as the point in
   * time after which the session is considered done, and no event loop
   * iteration is necessary.
   *
   * The session will be alive for the duration of onCloseFinish callback,
   * but this is the last time the application can safely assume that
   * the session is alive.
   *
   * The onWriteQuiescence() callback is invoked when all pending writes are
   * done, rather than invoking it for each write.
   *
   * @param transport  Connected transport; transfers ownership inside
   *                   this session.
   */
  static McServerSession& create(
    folly::AsyncTransportWrapper::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    std::function<void(McServerSession&)> onWriteQuiescence,
    std::function<void(McServerSession&)> onCloseStart,
    std::function<void(McServerSession&)> onCloseFinish,
    std::function<void()> onShutdown,
    AsyncMcServerWorkerOptions options,
    void* userCtxt,
    Fifo* debugFifo = nullptr);

  /**
   * Eventually closes the transport. All pending writes will still be drained.
   * Please refer create() for info about the callbacks invoked.
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

  /**
   * Get the peer's socket address
   */
  const folly::SocketAddress& getSocketAddress() const noexcept {
    return socketAddress_;
  }

 private:
  folly::AsyncTransportWrapper::UniquePtr transport_;
  folly::SocketAddress socketAddress_;
  std::shared_ptr<McServerOnRequest> onRequest_;
  std::function<void(McServerSession&)> onWriteQuiescence_;
  std::function<void(McServerSession&)> onCloseStart_;
  std::function<void(McServerSession&)> onCloseFinish_;
  std::function<void()> onShutdown_;
  AsyncMcServerWorkerOptions options_;
  void* userCtxt_{nullptr};
  Fifo* debugFifo_{nullptr}; // Debug pipe.

  enum State {
    STREAMING,  /* close() was not called */
    CLOSING,    /* close() was called, waiting on pending requests */
    CLOSED,     /* close() was called and connection was torn down.
                   This is a short lived state to prevent another close()
                   between the first close() and McServerSession destruction
                   from doing anything */
  };
  State state_{STREAMING};

  ServerMcParser<McServerSession> parser_;

  // Pointer to current buffer. Updated by getReadBuffer()
  std::pair<void*, size_t> curBuffer_;

  /* In-order protocol state */

  /* headReqid_ <= tailReqid_.  Since we must output replies sequentially,
     headReqid_ tracks the last reply id we're allowed to sent out.
     Out of order replies are stalled in the blockedReplies_ queue. */
  uint64_t headReqid_{0}; /**< Id of next unblocked reply */
  uint64_t tailReqid_{0}; /**< Id to assign to next request */
  std::unordered_map<uint64_t, std::unique_ptr<WriteBuffer>> blockedReplies_;

  /* If non-null, a multi-op operation is being parsed.*/
  std::shared_ptr<MultiOpParent> currentMultiop_;


  /* Batch writing state */

  /**
   * All writes to be written at the end of the loop in a single batch.
   */
  std::deque<std::unique_ptr<WriteBuffer>> pendingWrites_;

  /**
   * Each entry contains the count of requests with replies already written
   * to the transport, waiting on write success.
   */
  std::deque<size_t> writeBatches_;

  /**
   * Queue of write buffers.
   * Only initialized after we know the protocol (see ensureWriteBufs())
   */
  std::unique_ptr<WriteBufferQueue> writeBufs_;

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
   * call onCloseFinish_() if so.
   */
  void checkClosed();

  void reply(std::unique_ptr<WriteBuffer> wb, uint64_t reqid);

  /**
   * Called on mc_op_end or connection close to close out an in flight
   * multop request.
   */
  void processMultiOpEnd();

  /* TAsyncTransport's readCallback */
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override;
  void readDataAvailable(size_t len) noexcept override;
  void readEOF() noexcept override;
  void readErr(const folly::AsyncSocketException& ex)
    noexcept override;

  /* McParser's parseCallback */
  void requestReady(McRequest&& req,
                    mc_op_t operation,
                    uint64_t reqid,
                    mc_res_t result,
                    bool noreply);
  void parseError(mc_res_t result, folly::StringPiece reason);
  void typedRequestReady(uint32_t typeId,
                         const folly::IOBuf& reqBody,
                         uint64_t reqid);

  /**
   * Must be called after parser has detected the protocol (i.e.
   * at least one request was processed).
   * Closes the session on protocol error
   * @return True if writeBufs_ has value after this call,
   *         False on any protocol error.
   */
  bool ensureWriteBufs();

  void queueWrite(std::unique_ptr<WriteBuffer> wb);

  void completeWrite();

  /* TAsyncTransport's writeCallback */
  void writeSuccess() noexcept override;
  void writeErr(size_t bytesWritten,
                const folly::AsyncSocketException& ex)
    noexcept override;

  void onTransactionStarted(bool isSubRequest);
  void onTransactionCompleted(bool isSubRequest);

  McServerSession(
    folly::AsyncTransportWrapper::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    std::function<void(McServerSession&)> onWriteSuccess,
    std::function<void(McServerSession&)> onCloseStart,
    std::function<void(McServerSession&)> onCloseFinish,
    std::function<void()> onShutdown,
    AsyncMcServerWorkerOptions options,
    void* userCtxt,
    Fifo* debugFifo);

  McServerSession(const McServerSession&) = delete;
  McServerSession& operator=(const McServerSession&) = delete;

  friend class McServerRequestContext;
  friend class ServerMcParser<McServerSession>;
};


}}  // facebook::memcache
